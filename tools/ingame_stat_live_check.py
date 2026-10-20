"""Read-only Character/Summons view checks in an already isolated ingame_live_session.

No keyboard/mouse input and no stat/save writes. Leaves the last modal open deliberately so
ingame_live_session finish also exercises cleanup of its managed roots during normal shutdown.
This does not replace real input, visual comparison, mode selection or apply/reset tests.
"""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
from tools import le_session as le


def run(folder: Path):
    state = json.loads((folder / "session.json").read_text(encoding="utf-8"))
    if state["phase"] != "running" or le.game_pids() != [state["pid"]]:
        raise RuntimeError("This is not the running isolated test session.")
    process = state["process"]
    if le.process_identity(state["pid"]) != process:
        raise RuntimeError("The game process changed.")

    def request(command):
        reply = le.send(command, timeout=8, expected_process=process)
        return json.loads(reply)

    identity = request("identityread")
    player = identity.get("player") or {}
    if not identity.get("offline") or player.get("name") != "EpFeedbackTest" or str(player.get("id")) != state["cloneId"]:
        raise RuntimeError("Wrong character. No view commands were sent.")

    results = {"identity": identity, "inputAutomation": False, "statWrites": False, "saveWrites": False, "views": []}
    catalog = json.loads((Path(__file__).resolve().parents[1] / "ui/catalog.json").read_text(encoding="utf-8"))
    minions = [i for i, row in enumerate(catalog["characterStats"]["aliases"])
               if row["name"].startswith(("minion", "maxcompanions"))]
    if not minions:
        raise RuntimeError("The desktop catalog has no minion shortcuts to verify.")

    def healthy(panel):
        if not panel.get("ok") or panel.get("faults") or panel.get("broken"):
            raise RuntimeError(f"UI failure: {panel.get('error', panel)}")
        return panel

    def ready_modal():
        end = time.monotonic() + 30
        while time.monotonic() < end:
            panel = healthy(request("panel read"))
            apply = next((h for h in panel.get("hits", []) if h["name"] == "stat-apply"), None)
            if panel["dialog"] and apply and apply["enabled"]:
                return panel
            time.sleep(0.2)
        raise RuntimeError("The stat view did not obtain a valid current contribution.")

    healthy(request("panel 1"))
    browser = healthy(request("panel page:stats"))
    if browser["page"] != "stats":
        raise RuntimeError("Character page did not open.")
    results["browser"] = {"page": browser["page"], "faults": browser["faults"],
                          "tabs": [h["name"] for h in browser["hits"] if h["name"].startswith("stat-tab-")]}
    for address in ["sheet:0", "aliases:0", "raw:0"] + [f"aliases:{index}" for index in minions]:
        healthy(request("panel stat:" + address))
        panel = ready_modal()
        inputs = [i for i in panel["inputs"] if i["section"] == -4]
        if len(inputs) != 1 or inputs[0]["name"] != "stat-value":
            raise RuntimeError("A modal rebuild leaked or lost its watched input.")
        modes = [h for h in panel["hits"] if h["name"].startswith("stat-mode-")]
        if len(modes) == 2 and abs(modes[0]["w"] - modes[1]["w"]) > 0.1:
            raise RuntimeError("Basic calculation buttons have unequal widths.")
        results["views"].append({"address": address, "faults": panel["faults"], "inputs": inputs,
                                 "modes": modes, "applyEnabled": True})

    refused = request("panel stat:sheet:0:apply")
    if refused.get("ok"):
        raise RuntimeError("A non-view stat address was accepted.")
    healthy(request("panel dismiss"))
    if any(i["section"] == -4 for i in request("panel read")["inputs"]):
        raise RuntimeError("Closing the modal left a watched input behind.")
    summons = healthy(request("panel page:summons"))
    buttons = [h["name"] for h in summons["hits"] if h["name"].startswith("summon-stat-")]
    if sorted(buttons) != sorted(f"summon-stat-{i}" for i in minions):
        raise RuntimeError(f"Unexpected minion shortcuts: {buttons}")
    results["summonShortcuts"] = buttons
    results["measure"] = request("panel measure")
    if not results["measure"].get("total") or results["measure"].get("drift"):
        raise RuntimeError("The measured and drawn text line counts differ.")
    healthy(request(f"panel stat:aliases:{minions[0]}"))
    ready_modal()
    results["shutdownWithStatModalOpen"] = True
    (folder / "stat-view-checks.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"views": len(results["views"]), "summonShortcuts": len(buttons),
                      "inputAutomation": False, "statWrites": False, "faults": 0,
                      "measurementDrift": results["measure"].get("drift")}), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    run(parser.parse_args().folder.resolve())
