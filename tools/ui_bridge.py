"""Catalog-driven UI adapter. Game behavior remains in the existing native/backends.

All entry points share the progression IPC lock. The HTTP layer has one worker.
No mutations are run by construction, bootstrap, catalog/profile/theme loading.
"""
from __future__ import annotations

import copy
import json
import math
import re
import time
from pathlib import Path

from . import cof_backend as cof, monolith_backend as mono, collection_backend as collection
from . import progression_backend as progression, le_session as le
from .ui_language import english_exception
from .app_paths import resource_root

ROOT = resource_root()


def load_catalog():
    return json.loads((ROOT / "ui/catalog.json").read_text(encoding="utf-8"))


def finite(value, spec):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("Enter a finite number.")
    if spec.get("type") in ("integer", "optional_integer", "integer_or_none") and int(value) != value:
        raise ValueError("Enter a whole number.")
    if value < spec.get("minimum", -math.inf) or value > spec.get("maximum", math.inf):
        raise ValueError(f"Enter a value between {spec.get('minimum', '−∞')} and {spec.get('maximum', '∞')}.")
    return int(value) if spec.get("type") in ("integer", "optional_integer", "integer_or_none") else value


def key_id(key):
    return ":".join(str(key[x]) for x in ("sp", "tags", "special", "extra"))


def raw_command(key):
    return "statraw " + " ".join(str(key[x]) for x in ("sp", "tags", "special", "extra"))


def text_ok(reply):
    if not reply:
        raise RuntimeError("The game did not respond. The action was not automatically retried.")
    # Telemetry contains 'refused online 0'; this is not an error.
    if re.search(r"(?i)(?:^|\n)[^\n]*?:\s*(?:refused:|error:|failed:)", reply) or re.search(
        r"(?i)(?:panel lookup unavailable|expected one active|select the offline tab|character sheet unavailable|invalid panel count)", reply
    ):
        raise RuntimeError(reply)
    return {"ok": True, "text": reply}


class UiBridge:
    def __init__(self, *, preview=False, catalog=None):
        self.catalog = catalog or load_catalog()
        self.controls = {x["id"]: x for x in self.catalog["controls"]}
        self.preview = preview
        self.history = []
        self.actor = {}  # Only settings explicitly applied in THIS UI session.
        self.actor_owner = None
        self.cache = {}
        self.temporary_resets = {}  # Explicit session writes only; never save actions.
        self.demo = copy.deepcopy(self.catalog["liveState"]) if preview else {}
        self.demo_values = {x["id"]: x["default"] for x in self.catalog["controls"]
                            if x["lifetime"] in ("session", "actor") and x["widget"] in ("number", "toggle")}

    def public_catalog(self):
        # Never send timestamped player snapshots as live startup values.
        result = copy.deepcopy({k: self.catalog[k] for k in ("schemaVersion", "groups", "controls", "contract",
                "characterStats", "optionCatalogs", "counts", "researchOnly", "generatedAt")})
        # The generated catalog is the command contract; translate presentation only.
        locale = json.loads((ROOT / "ui/locale-en.json").read_text(encoding="utf-8"))
        for group in result["groups"]:
            group["label"] = locale["groups"][group["id"]]
        for control in result["controls"]:
            control["label"], control["description"] = locale["controls"][control["id"]]
        for feature in result["researchOnly"]:
            feature["label"], feature["details"] = locale["researchOnly"][feature["id"]]
        result["language"] = locale["language"]
        return result

    def _send(self, command):
        return text_ok(le.send(command, timeout=15))

    def _json(self, command):
        result = progression.request(command)
        if not result.get("ok"):
            raise RuntimeError(result.get("error", "Could not read game data."))
        return result

    def _session(self):
        if not le.game_pids():
            raise RuntimeError("Last Epoch is not running. The interface is ready; the game is disconnected.")
        return self._json("sessionread")

    def _offline(self, session):
        if session.get("state") != "InGame" or session.get("transitioning") is not False:
            raise RuntimeError("Load an offline character and wait for the area transition to finish.")
        identity = self._send("playerread")["text"]
        if "CharacterData.IsOffline=true" not in identity:
            raise RuntimeError("Could not verify that the loaded character is offline.")

    def connection(self):
        with progression._ipc_lock:
            if self.preview:
                return {"ok": True, "connected": True, "offline": True, "state": "InGame",
                        "transitioning": False, "preview": True, "text": "Preview; the game is unchanged.",
                        "values": self.demo_values}
            try:
                session = self._session()
                status = self._send("status")["text"]
                offline = "gate: offline play" in status
                player = None
                values = {}
                for name in ("xp", "gold", "drops", "density", "rarity", "speed", "cooldown"):
                    match = re.search(rf"(?m)^{name}: x([\d.eE+-]+)", status)
                    if match:
                        values[name] = float(match[1])
                match = re.search(r"(?m)^autopickup: (on|off)", status)
                if match:
                    values["autopickup"] = int(match[1] == "on")
                if session.get("state") == "InGame" and session.get("transitioning") is False and offline:
                    player = self._json("identityread")["player"]
                    stat = self._send("statraw 9 0 0 0")["text"]
                    owned = re.search(r"EpochPact added=[\d.eE+-]+ increased=([\d.eE+-]+) more=[\d.eE+-]+ attached=1", stat)
                    values["speed"] = 1 + float(owned[1]) if owned else 1
                return {**session, "connected": True, "offline": offline, "values": values,
                        "text": status, "preview": False, "player": player}
            except (RuntimeError, OSError) as exc:
                return {"ok": False, "connected": False, "offline": False, "error": english_exception(exc), "preview": False}

    def _parameters(self, control, supplied, operation):
        allowed = {x["name"] for x in control["parameters"]} - {"save_id"}
        allowed |= {"expected_id", "expected_name", "unit", "catalog_index", "secondary", "switch_from_merchant"}
        if "expected_name" in supplied and (not isinstance(supplied["expected_name"], str) or len(supplied["expected_name"]) > 200):
            raise ValueError("Invalid expected character name.")
        if set(supplied) - allowed:
            raise ValueError("Unknown parameter.")
        args = {}
        for spec in control["parameters"]:
            name, kind = spec["name"], spec["type"]
            if operation == "reset" and control["widget"] != "stat_editor":
                required = re.findall(r"\{(\w+)\}", control.get("resetCommand") or "")
                if name not in required:
                    continue
            if name == "save_id" or (name == "row" and control["id"] == "sheet_row"):
                continue
            if name == "value" and operation in ("read", "reset"):
                continue
            if name not in supplied:
                if kind == "optional_integer":
                    continue
                raise ValueError(f"Missing parameter: {name}")
            value = supplied[name]
            if kind in ("number", "integer", "optional_integer"):
                # Stat UI percentages can be 100x the native float range.
                bounds = {"type": kind} if name == "value" and control["widget"] == "stat_editor" else spec
                value = finite(value, bounds)
            elif kind == "number_or_reset":
                value = "reset" if value in (None, "reset") else finite(value, spec)
            elif kind == "integer_or_none":
                value = None if value is None else finite(value, spec)
            elif kind == "single_token":
                limit = spec.get("maximumLength", 100)
                if not isinstance(value, str) or len(value) > limit or not re.fullmatch(r"[^\s\x00-\x1f]+", value):
                    raise ValueError("Enter the exact character name without spaces.")
                if control["id"] == "loot_affixes" and value != "none":
                    if not re.fullmatch(r"\d+(?:,\d+)*", value) or len(value.split(",")) > 64:
                        raise ValueError("Enter up to 64 comma-separated affix IDs, or none.")
            elif kind == "enum":
                choices = spec.get("choices")
                if control["id"] == "stat_alias" and name == "name":
                    choices = [x["name"] for x in self.catalog["characterStats"]["aliases"]]
                if not isinstance(value, str) or value not in (choices or []):
                    raise ValueError("Choose a valid catalog option.")
            elif kind == "path":
                if not isinstance(value, str):
                    raise ValueError("Choose a backup.")
            args[name] = value
        return args

    def _stat(self, cid, args, supplied, operation):
        stats = self.catalog["characterStats"]
        if cid == "stat_alias":
            alias = next(x for x in stats["aliases"] if x["name"] == args["name"])
            key, mode = alias["key"], alias["mode"]
            unit = supplied.get("unit", "percent" if alias["unit"] == "fraction" else "raw")
            percent_allowed = alias["unit"] == "fraction"
        elif cid == "sheet_row":
            index = supplied.get("catalog_index")
            if type(index) is not int or not 0 <= index < len(stats["sheetRows"]):
                raise ValueError("Choose a character sheet row from the catalog.")
            row = stats["sheetRows"][index]
            secondary = supplied.get("secondary", False)
            if type(secondary) is not bool:
                raise ValueError("Invalid secondary stat selection.")
            key = row["modifierKey"] if secondary else row["key"]
            if key is None:
                raise ValueError("This row has no secondary stat.")
            mode = args["mode"]
            unit = supplied.get("unit", "raw")
            percent_allowed = mode != "added" or any(x["sharedKey"] == key_id(key) and x["unit"] == "fraction" and x["mode"] == mode for x in stats["aliases"])
        else:
            key = {x: args[x] for x in ("sp", "tags", "special", "extra")}
            if key["sp"] not in [x["id"] for x in stats["properties"]]:
                raise ValueError("Choose a supported stat type.")
            mode = args["mode"]
            unit = supplied.get("unit", "raw")
            percent_allowed = mode != "added" or any(x["sharedKey"] == key_id(key) and x["unit"] == "fraction" and x["mode"] == mode for x in stats["aliases"])
        if unit not in ("raw", "percent") or (unit == "percent" and not percent_allowed):
            raise ValueError("This stat's Added contribution uses raw game units and cannot be converted from a percentage.")
        command = raw_command(key)
        value = None
        if operation == "reset":
            command += " reset"
        elif operation != "read":
            value = args["value"] / 100 if unit == "percent" else args["value"]
            value = finite(value, stats["modeLimits"][mode])
            command += f" {mode} {value:.17g}"
        else:
            value = None
        if self.preview:
            reply = {"ok": True, "text": f"Preview: {command}"}
            if operation == "read":
                modes = self.actor.get(key_id(key), {}).get("modes", {})
                reply["text"] += "; EpochPact " + " ".join(
                    f"{name}={modes.get(name, 0):.17g}" for name in ("added", "increased", "more")
                ) + " attached=1"
        else:
            reply = self._send(command)
        if reply.get("ok") and operation != "read":
            identity = key_id(key)
            if operation == "reset":
                self.actor.pop(identity, None)
            else:
                entry = self.actor.setdefault(identity, {"key": key, "modes": {}})
                entry["modes"][mode] = value
        return {**reply, "sharedKey": key_id(key), "mode": mode, "rawValue": value,
                "actorValues": copy.deepcopy(self.actor)}

    def available_history(self):
        boundary = progression.BACKUPS.resolve()
        self.history = [item for item in self.history if
                        Path(item['backup']).resolve().parent != boundary or
                        (Path(item['backup']) / 'manifest.json').is_file()]
        return [dict(item) for item in self.history]

    def _remember(self, result, cid):
        backup = result.get("backup")
        if backup:
            self.history.append({"id": len(self.history), "control": cid, "backup": backup,
                                 "time": time.time(), "ok": result.get("ok", False)})
        self.history = self.history[-100:]
        return {**result, "history": self.available_history(), "actorValues": copy.deepcopy(self.actor)}

    def _actor_identity(self):
        player = self._json("identityread")["player"]
        return (player["id"], player["name"])

    def execute(self, cid, supplied=None, operation="set"):
        with progression._ipc_lock:
            if cid not in self.controls or operation not in ("set", "read", "reset"):
                raise ValueError("Invalid control or operation.")
            control = self.controls[cid]
            supplied = supplied or {}
            if not isinstance(supplied, dict):
                raise ValueError("Expected a parameter object.")
            if control["availability"] != "implemented":
                raise ValueError("This feature is not implemented.")
            if operation == "reset" and not control.get("resetCommand") and cid != "stat_alias":
                raise ValueError("This action cannot be reset.")
            if operation == "read" and control["widget"] not in ("read", "preview", "stat_editor"):
                raise ValueError("This control has no separate read command.")
            args = self._parameters(control, supplied, operation)
            if cid in ("sheet_row", "raw_stat", "stat_alias"):
                # Validate stat metadata and converted bounds BEFORE any IPC.
                original_preview = self.preview
                self.preview = True
                original_actor = copy.deepcopy(self.actor)
                try:
                    self._stat(cid, args, supplied, operation)
                finally:
                    self.preview = original_preview
                    self.actor = original_actor
            if self.preview:
                return self._demo_execute(cid, args, supplied, operation)
            if cid in ("sheet_row", "raw_stat", "stat_alias"):
                # Native identityread enforces the settled offline session. No
                # quest/map refresh or full player snapshot for a single stat.
                identity = self._actor_identity()
                if self.actor_owner != identity:
                    self.actor.clear()
                    self.actor_owner = identity
                return self._stat(cid, args, supplied, operation)
            session = self._session()
            req = control["requirements"]
            if "offline" in req:
                self._offline(session)
            for state in ("Login", "CharacterSelect"):
                if f"{state}_and_not_transitioning" in req and (session.get("state") != state or session.get("transitioning") is not False):
                    raise RuntimeError(f"Wait for the transition to finish on the {state} screen before using this action.")
            if cid == "undo":
                if args["backup"] not in [x["backup"] for x in self.available_history()]:
                    raise ValueError("Choose a backup from this interface's operation history.")
                return progression.undo(Path(args["backup"]))
            save_id = None
            identity = None
            if control["group"] == "general" and control["widget"] in ("number", "toggle") and "expected_id" in supplied:
                # Delayed auto-applies belong to the actor present at the edit,
                # never to a character loaded while the request was queued.
                expected_id = supplied["expected_id"]
                if not isinstance(expected_id, str) or not re.fullmatch(r"[0-9]+", expected_id):
                    raise ValueError("Invalid expected character identity.")
                identity = self._actor_identity()
                if identity[0] != expected_id or supplied.get("expected_name") and identity[1] != supplied["expected_name"]:
                    raise RuntimeError("The loaded character has changed. Refresh the connection and edit the setting again.")
            if any(p["name"] == "save_id" for p in control["parameters"]):
                # Echo commands validate the expected actor themselves. A reward
                # read must not open/refresh the world map as a side effect.
                save_id = supplied.get("expected_id") if cid in ("echo_read", "echo_focus") else self._json("identityread")["player"]["id"]
                if not isinstance(save_id, str) or not re.fullmatch(r"[0-9]+", save_id):
                    raise RuntimeError("The current save identity is invalid.")
                if supplied.get("expected_id") != save_id:
                    raise RuntimeError("The loaded character has changed. Refresh this section and try again.")
            if cid in ("speed", "stat_reset"):
                identity = identity or self._actor_identity()
                if self.actor_owner != identity:
                    self.actor.clear()
                    self.actor_owner = identity
            if control["group"] == "monolith" and control["lifetime"] == "game_save":
                live = mono.read()
                if live.get("player", {}).get("id") != save_id or live.get("editable") is not True:
                    raise RuntimeError("Monolith editing requires the End of Time, MonolithHub or M_Rest area.")
                if "timeline" in args:
                    timeline = next((x for x in live["timelines"] if x["id"] == args["timeline"]), None)
                    if timeline is None:
                        raise ValueError("This timeline is not in the current catalog.")
                    difficulty = next((x for x in timeline["difficulties"] if x["index"] == (args["difficulty"] == "empowered")), None)
                    if difficulty is None or not difficulty.get("unlocked"):
                        raise RuntimeError("This timeline / difficulty is not unlocked yet.")
                    if cid in ("corruption", "stability"):
                        bounds = {"minimum": difficulty["minCorruption"] if cid == "corruption" else 0,
                                  "maximum": difficulty["maxCorruption"] if cid == "corruption" else difficulty["maxStability"], "type": "integer"}
                        finite(args["value"], bounds)
            if control["group"] == "cof" and cid != "cof_read":
                live = cof.read()
                if save_id is not None and live.get("player", {}).get("id") != save_id:
                    raise RuntimeError("The CoF character has changed. Refresh the section.")
                if "cof_member_to_enable" in req and args.get("value") not in (control["default"], "reset") and not live["cof"]["member"]:
                    raise RuntimeError("Circle of Fortune membership is required to enable this multiplier.")
                if cid in ("cofpreview", "cofprophecy", "cof_charges"):
                    slot = next((x for x in live["slots"] if x["index"] == args["slot"]), None)
                    if slot is None or slot.get("locked"):
                        raise RuntimeError("This prophecy slot is locked by rank.")
                if cid in ("cofpreview", "cofprophecy"):
                    if args["reward"] is not None and not any(x["id"] == args["reward"] and x.get("available") for x in live["rewards"]):
                        raise ValueError("This reward is not currently available.")
                    if args["lens"] is not None and not any(x["id"] == args["lens"] and x.get("purchased") and x["rankRequired"] <= live["cof"]["rank"] for x in live["lenses"]):
                        raise ValueError("The lens must be purchased and allowed by the current rank.")
                    if cid == "cofprophecy":
                        preview = cof.prophecy(save_id, args["slot"], args["reward"], args["lens"], preview=True)
                        if not preview.get("ok"):
                            return preview
            if control["lifetime"] == "session" and operation != "read" and control.get("resetCommand"):
                reset = control["resetCommand"].format(**args)
                self.temporary_resets[(cid, args.get("category"))] = reset
            result = self._dispatch(cid, args, supplied, operation, save_id)
            if cid in ("craft_read", "craft_forge") and "preview" in result:
                # Native forge data and the UI's simulation flag are separate.
                result = dict(result)
                result["forgePreview"] = result.pop("preview")
            if result.get("ok") and cid == "stat_reset":
                self.actor.clear()
            if result.get("ok") and cid == "speed":
                value = 1 if operation == "reset" else args["value"]
                entry = self.actor.setdefault("9:0:0:0", {"key": {"sp": 9, "tags": 0, "special": 0, "extra": 0}, "modes": {}})
                entry["modes"]["increased"] = value - 1
            if result.get("ok") and control["widget"] == "read":
                self.cache[cid] = result
            return self._remember(result, cid)

    def restore_session(self):
        """Release temporary contributions on an explicit desktop close.

        Keep game-save changes and rewards. Never unload a DLL or close the game.
        Commands use the existing serialized IPC and native reset implementations.
        """
        with progression._ipc_lock:
            if self.preview or not (self.temporary_resets or self.actor):
                return {"ok": True, "restored": 0}
            if not le.game_pids():
                self.temporary_resets.clear(); self.actor.clear()
                return {"ok": True, "restored": 0, "gameClosed": True}
            live = self._json("identityread")
            if live.get("offline") is not True:
                raise RuntimeError("Temporary settings could not be restored: load the offline character, then close EpochPact again.")
            player = live["player"]
            same_actor = self.actor_owner == (player["id"], player["name"])
            restored = 0

            def reset(command):
                reply = self._send(command)
                text = reply.get("text", "")
                if text.startswith("{"):
                    result = json.loads(text)
                    if result.get("ok") is not True:
                        raise RuntimeError(result.get("error", "The game rejected temporary setting cleanup."))

            for marker, command in list(self.temporary_resets.items()):
                if marker[0] != "speed" or same_actor:
                    reset(command); restored += 1
                del self.temporary_resets[marker]
            if same_actor:
                if "9:0:0:0" in self.actor:
                    # Clear the speed control's own displayed factor as well as
                    # its shared stat contribution; raw reset alone leaves the
                    # legacy status meter reporting the old multiplier.
                    reset("speed 1")
                for identity, entry in list(self.actor.items()):
                    reset(raw_command(entry["key"]) + " reset")
                    del self.actor[identity]; restored += 1
            else:
                self.actor.clear()  # Do not reset a different loaded character.
            return {"ok": True, "restored": restored}

    def _dispatch(self, cid, a, supplied, operation, save_id):
        control = self.controls[cid]
        value = control["default"] if operation == "reset" else a.get("value")
        mapping = {
            "craft_forge": lambda: progression.request(f"craftforge {save_id}"),
            "atlas_read": collection.atlas, "stash_read": collection.stash,
            "echo_read": lambda: mono.echoes(save_id, a["timeline"], a["difficulty"] == "empowered"),
            "echo_focus": lambda: mono.focus_echo(save_id, a["timeline"], a["difficulty"] == "empowered", a["index"]),
            "progress_read": progression.read, "monolith_read": mono.read, "cof_read": cof.read,
            "factions_read": lambda: progression.request("factionread"),
            "campaign_complete": lambda: progression.apply("questscomplete", save_id),
            "waypoints_unlock": lambda: progression.apply("waypointsunlock", save_id),
            "monolith_unlock": lambda: mono.unlock(save_id),
            "monolith_select": lambda: mono.select(save_id, a["timeline"], a["difficulty"] == "empowered"),
            "corruption": lambda: mono.corruption(save_id, a["timeline"], a["difficulty"] == "empowered", value),
            "stability": lambda: mono.stability(save_id, a["timeline"], a["difficulty"] == "empowered", value),
            "stability_multiplier": lambda: mono.multiplier(value),
            "cof_join": lambda: cof.join(save_id, switch_from_merchant=supplied.get("switch_from_merchant", False)),
            "cof_rank": lambda: cof.rank(save_id, value), "cof_favor": lambda: cof.favor(save_id, value),
            "cof_reputation_grant": lambda: cof.reputation(save_id, value),
            "cof_lenses_unlock": lambda: cof.unlock_lenses(save_id),
            "cofpreview": lambda: cof.prophecy(save_id, a["slot"], a["reward"], a["lens"], preview=True),
            "cofprophecy": lambda: cof.prophecy(save_id, a["slot"], a["reward"], a["lens"], preview=False),
            "cof_charges": lambda: cof.charges(save_id, a["slot"], value),
            "cof_favor_multiplier": lambda: cof.favor_multiplier(value),
            "cof_reputation_multiplier": lambda: cof.reputation_multiplier(value),
            "cof_charge_multiplier": lambda: cof.charge_multiplier(value),
            "cof_reward_multiplier": lambda: cof.reward_multiplier(value),
            "cof_exalted_multiplier": lambda: cof.exalted_multiplier(value),
            "cof_t7_multiplier": lambda: cof.t7_multiplier(value),
            "cof_lp_multiplier": lambda: cof.lp_multiplier(value),
        }
        for name in ("celerity", "charity", "duplication"):
            mapping["cof_lens_" + name] = lambda name=name: cof.lens_multiplier(name, value)
        for name in ("enemy", "echo"):
            mapping["cof_double_" + name] = lambda name=name: cof.double_drop_chance(name, None if value in (None, "reset") else value)
        if cid in mapping:
            return mapping[cid]()
        formatted = {k: f"{v:.9g}" if isinstance(v, (int, float)) else v for k, v in a.items()}
        formatted.setdefault("level", "")
        if operation == "reset":
            command = control["resetCommand"].format(**formatted)
        else:
            command = control["commandTemplate"].format(**formatted).strip()
        return self._json(command) if control["responseFormat"] == "json" else self._send(command)

    def _demo_execute(self, cid, a, supplied, operation):
        control = self.controls[cid]
        if cid in ("atlas_read", "stash_read", "echo_read", "echo_focus"):
            raise RuntimeError("This feature needs a loaded offline character. Preview does not invent collection or Echo data.")
        if cid in ("sheet_row", "raw_stat", "stat_alias"):
            return {**self._stat(cid, a, supplied, operation), "preview": True}
        if cid == "craft_forge":
            raise RuntimeError("Preview cannot forge a real item. Load an offline character and select an item in the game forge.")
        if cid in ("map_read", "map_reveal"):
            state = self.demo.setdefault("mapread", {"ok": True, "enabled": False, "revealed": False, "renderOnly": True})
            if cid == "map_reveal": state.update(enabled=bool(a["value"]), revealed=bool(a["value"]))
            self.demo_values["map_reveal"] = int(state["enabled"])
            return {**state, "preview": True}
        if cid.startswith(("loot_", "craft_")) and cid not in ("loot_read", "craft_read"):
            key = "lootread" if cid.startswith("loot_") else "craftread"
            state = self.demo[key]
            if cid == "loot_reset":
                state.update(mode="all", minimumLP=2, t7=True, respectFilter=True,
                             materials=True, affixIds=[], categories={x: True for x in state["categories"]})
            elif cid == "craft_reset":
                state.update(fpFactor=1, hopePercent=None, despairPercent=None, preserveShards=False, preserveRunes=False, preserveGlyphs=False, bypassLevel=False)
            elif cid == "loot_mode":
                state["mode"] = "all" if operation == "reset" else a["mode"]
            elif cid == "loot_affixes":
                ids = "none" if operation == "reset" else a["ids"]
                selected = [] if ids == "none" else list(dict.fromkeys(map(int, ids.split(","))))
                if set(selected) - {x["id"] for x in state["affixes"]}:
                    raise ValueError("Choose an affix from the current catalog.")
                state["affixIds"] = selected
            elif cid == "loot_category":
                value = True if operation == "reset" else bool(a["value"])
                if a["category"] == "materials": state["materials"] = value
                else: state["categories"][a["category"]] = value
            else:
                fields = {"loot_lp": "minimumLP", "loot_t7": "t7", "loot_filter": "respectFilter",
                          "craft_fp": "fpFactor", "craft_shards": "preserveShards", "craft_runes": "preserveRunes", "craft_glyphs": "preserveGlyphs", "craft_level": "bypassLevel",
                          "craft_hope": "hopePercent", "craft_despair": "despairPercent"}
                value = control["default"] if operation == "reset" else a["value"]
                if control["widget"] == "toggle": value = bool(value)
                state[fields[cid]] = None if value == "reset" else value
            sources = {"loot_lp": ("lootread", "minimumLP"), "loot_t7": ("lootread", "t7"),
                       "loot_filter": ("lootread", "respectFilter"), "craft_fp": ("craftread", "fpFactor"),
                       "craft_shards": ("craftread", "preserveShards"), "craft_runes": ("craftread", "preserveRunes"), "craft_glyphs": ("craftread", "preserveGlyphs"), "craft_level": ("craftread", "bypassLevel")}
            for name, (source, field) in sources.items(): self.demo_values[name] = self.demo[source].get(field, self.controls[name]["default"])
            return self._demo_result(key, state)
        reads = {"session_read": "sessionread", "progress_read": "progressread", "monolith_read": "monolithread", "cof_read": "cofread", "factions_read": "factionread", "loot_read": "lootread", "craft_read": "craftread"}
        if cid in reads:
            return self._demo_result(reads[cid], self.demo[reads[cid]])
        if cid == "undo":
            raise ValueError("Real backups and recovery are unavailable in preview.")
        if control["lifetime"] in ("session", "actor") and "value" in a:
            self.demo_values[cid] = control["default"] if operation == "reset" else a["value"]
        if cid == "stat_reset":
            self.actor.clear()
        if cid == "speed":
            entry = self.actor.setdefault("9:0:0:0", {"key": {"sp": 9, "tags": 0, "special": 0, "extra": 0}, "modes": {}})
            entry["modes"]["increased"] = (1 if operation == "reset" else a["value"]) - 1
        return {"ok": True, "preview": True, "text": "Preview: action simulated; the game and saves were unchanged.", "actorValues": copy.deepcopy(self.actor)}

    @staticmethod
    def _demo_result(key, state):
        result = copy.deepcopy(state)
        if key == "craftread" and "preview" in result:
            result["forgePreview"] = result.pop("preview")
        return {**result, "preview": True, "snapshot": True}

    def reconcile(self):
        """Explicit reconciliation; never replay game_save, persisted profiles or other actors."""
        with progression._ipc_lock:
            applied = []
            if not self.preview and self.actor:
                self._offline(self._session())
                if self.actor_owner != self._actor_identity():
                    raise RuntimeError("These contributions belong to another character. Load that character or explicitly apply a new contribution.")
            for entry in self.actor.values():
                for mode, value in entry["modes"].items():
                    command = f"{raw_command(entry['key'])} {mode} {value:.17g}"
                    if not self.preview:
                        self._offline(self._session())
                        self._send(command)
                    applied.append(command)
            return {"ok": True, "preview": self.preview, "text": f"Reconciled {len(applied)} actor contributions.", "commands": applied}
