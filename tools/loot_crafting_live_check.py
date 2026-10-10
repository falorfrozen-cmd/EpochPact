"""Prepare and restore an isolated offline character for loot/crafting checks."""
from __future__ import annotations

import argparse
import json
import math
import time

try:
    from . import monolith_live_check as isolated
except ImportError:
    import monolith_live_check as isolated

isolated.MANIFEST = isolated.le_session.OUT / "loot-crafting-test-manifest.json"


def verify() -> dict:
    manifest = json.loads(isolated.MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("restored"):
        raise RuntimeError("Prepare the isolated clone first.")
    save_id = manifest["id"]
    transcript = []
    report_path = isolated.le_session.OUT / "loot-crafting-live-check.json"

    def command(text, *, okay=True, plain=False):
        reply = isolated.le_session.send(text, timeout=30)
        result = {"text": reply} if plain else json.loads(reply or "null")
        transcript.append({"command": text, "response": result})
        report_path.write_text(json.dumps(transcript, indent=2), encoding="utf-8")
        if plain:
            if not reply or "refused:" in reply: raise RuntimeError(reply)
        elif not isinstance(result, dict) or result.get("ok") is not okay:
            raise RuntimeError(f"{text}: {result}")
        return result

    def check(condition, label):
        if not condition: raise AssertionError(label)
        print("PASS", label, flush=True)

    def probe(action): return command(f"crafttest {save_id} {action}")
    def ground(): return command(f"loottest {save_id} ground")["items"]
    def settled(predicate):
        deadline = time.monotonic() + 15
        while True:
            items = ground()
            if predicate(items): return items
            if time.monotonic() > deadline: raise AssertionError("ground state did not settle")
            time.sleep(.5)

    initial = command("craftread")
    deadline = time.monotonic() + 15
    while initial.get("preview") is None and time.monotonic() < deadline:
        time.sleep(.5)
        initial = command("craftread")
    check(initial["preview"]["player"] == {"id": save_id, "name": "EpCraftTest"}, "isolated identity")
    try:
        command("autopickup 0", plain=True)
        command("craftreset"); command("lootreset")
        probe("clear")
        command("crafttest 0 setup", okay=False)
        for invalid in ("craftfp nan", "craftfp 1.1", "crafthope 101", "craftdespair -1", "craftshards 2", "lootlp 5", "lootaffixes 65535"):
            command(invalid, okay=False)
        check(True, "invalid identity/values rejected")

        for percent in (100, 0):
            probe("setup"); probe("supporthope"); command(f"crafthope {percent}")
            data = probe("forge")
            before, after = data["before"], data["after"]
            check((after["forgingPotential"] == before["forgingPotential"]) if percent == 100 else
                  (after["forgingPotential"] < before["forgingPotential"]), f"Hope {percent}% real forge")
            probe("clear")
        command("crafthope reset")

        for percent in (0, 100):
            probe("setup"); probe("supportdespair"); command(f"craftdespair {percent}")
            data = probe("forge")
            check(data["after"]["sealedAffixCount"] == (1 if percent == 100 else 0), f"Despair {percent}% real forge")
            probe("clear")
        command("craftdespair reset")

        for factor, preserve in ((0, 1), (.5, 0), (1, 0)):
            probe("setup"); command(f"craftfp {factor}"); command(f"craftshards {preserve}")
            data = probe("forge"); before, after = data["before"], data["after"]
            telemetry = command("craftread")["telemetry"]
            if factor == 0: check(before["forgingPotential"] == after["forgingPotential"], "zero FP loss")
            elif factor == .5: check(telemetry["lastEffectiveFPLoss"] == math.ceil(telemetry["lastNormalFPLoss"]*.5), "actual FP loss halved")
            check(after["shardsAvailable"] == before["shardsAvailable"] - (0 if preserve else 1), f"shard preservation {preserve}")
            probe("clear")
        reset = command("craftreset")
        check(not any(reset["hooks"].values()), "neutral craft removes every hook")
        check(reset["telemetry"]["faults"] == 0, "no craft hook faults")

        filters = command(f"loottest {save_id} filterprobe")
        check(filters["show"] is True and filters["hide"] is False, "native filter SHOW/HIDE")
        command("lootmode materials")
        command(f"loottest {save_id} drop"); command(f"loottest {save_id} materials")
        items = settled(lambda rows: len([i for i in rows if i["material"]]) >= 8)
        equipment_ids = {i["id"] for i in items if not i["material"]}
        check(equipment_ids and all(i["selected"] == i["material"] for i in items), "materials reject equipment")
        command("autopickup 1", plain=True)
        items = settled(lambda rows: not any(i["material"] for i in rows))
        check({i["id"] for i in items} == equipment_ids, "real material pickup preserves ground equipment")
        command("autopickup 0", plain=True)

        command("lootmode quality"); command("lootfilter 0"); command("loott7 0"); command("lootlp 2")
        probe("unique0"); probe("unique2")
        items = settled(lambda rows: any(i["lp"] == 2 and i["unique"] for i in rows))
        check(any(i["unique"] and i["lp"] == 0 and not i["selected"] for i in items), "LP0 rejected")
        check(any(i["unique"] and i["lp"] == 2 and i["selected"] for i in items), "LP2 accepted")
        probe("t7")
        items = settled(lambda rows: any(i["t7Ids"] for i in rows))
        t7_item = next(i for i in items if i["t7Ids"])
        check(not t7_item["selected"], "T7 toggle off rejects Exalted")
        command("loott7 1"); command(f"lootaffixes {t7_item['t7Ids'][0]}")
        check(any(i["id"] == t7_item["id"] and i["selected"] for i in ground()), "wanted T7 ID accepted")
        command("lootaffixes none"); command("lootmode filter")
        items = ground(); check(all(i["selected"] == i["filterPass"] for i in items), "current native filter decisions")
        command("autopickup 1", plain=True)
        before_ids = {i["id"] for i in items}
        time.sleep(2)
        # Normal inventory capacity is authoritative. Leftover visible items
        # are allowed; no new/deleted items are fabricated to force a pickup.
        remaining = ground()
        check({i["id"] for i in remaining}.issubset(before_ids), "normal pickup respects inventory capacity")
        command("autopickup 0", plain=True)
        telemetry = command("lootread")["telemetry"]
        check(telemetry["faults"] == 0, "no loot faults")
        return {"ok": True, "gameVersion": initial["gameVersion"], "transcript": str(report_path)}
    finally:
        command("autopickup 0", plain=True); command("craftreset"); command("lootreset")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "verify", "restore"))
    action = parser.parse_args().action
    result = isolated.prepare("EpCraftTest") if action == "prepare" else verify() if action == "verify" else isolated.restore()
    if action == "prepare":
        result = {k: result[k] for k in ("id", "backup", "testName")}
    print(json.dumps(result, indent=2))
