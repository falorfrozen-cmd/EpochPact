"""Verify actual crafting and render bindings on the isolated EpCraftTest only.

Requires native/build.bat test-core. No automatic owner loading or save restore.
The caller prepares/restores the clone using monolith_live_check safeguards.
"""
from __future__ import annotations
import argparse
import json
import shutil
import time
from pathlib import Path
from . import le_session as le
from . import progression_backend as recovery

OUT = Path(__file__).resolve().parents[1] / "research/live/ce-features-20261009"


def verify(kind: str) -> dict:
    manifest = json.loads((OUT / "test-manifest.json").read_text(encoding="utf-8"))
    if manifest.get("restored"): raise RuntimeError("The isolated clone was already restored.")
    save_id = manifest["id"]
    transcript, checks = [], []
    target = OUT / f"{kind}-live.json"

    def record():
        target.write_text(json.dumps({"checks": checks, "transcript": transcript}, indent=2), encoding="utf-8")

    def command(text, okay=True):
        if text.startswith("craftforge ") and okay: command("framereset")
        started=time.perf_counter()
        result = json.loads(le.send(text, timeout=20) or "null")
        transcript.append({"command": text, "result": result,"elapsedMs":round((time.perf_counter()-started)*1000,3)}); record()
        if not isinstance(result, dict) or result.get("ok") is not okay: raise RuntimeError(f"{text}: {result}")
        if text.startswith("craftforge ") and okay: command("frameread")
        return result

    def check(condition, label):
        if not condition: raise AssertionError(label)
        checks.append(label); record(); print("PASS", label, flush=True)

    deadline = time.monotonic() + 30
    while True:
        identity = command("identityread")["player"]
        if identity.get("scene"): break
        if time.monotonic() >= deadline: raise RuntimeError("Area did not finish loading.")
        time.sleep(.5)
    check(identity["id"] == save_id and identity["name"] == "EpCraftTest", "isolated character identity")

    def probe(action): return command(f"crafttest {save_id} {action}")

    def capture(name):
        screenshot = le.GAME / "EpochPact/logs/map-check.png"
        previous = screenshot.stat().st_mtime_ns if screenshot.exists() else None
        command("mapcapture")
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            if screenshot.exists() and screenshot.stat().st_mtime_ns!=previous:
                try:
                    shutil.copy2(screenshot, OUT / name)
                    return
                except PermissionError: pass
            time.sleep(.2)
        raise RuntimeError("The native rendered screenshot did not finish.")

    try:
        command("craftreset"); probe("clear")
        if kind == "map":
            before=command("mapreveal 1")
            destinations=command("progressread")["waypoints"]
            choices=[w["scene"] for w in destinations if w["unlocked"] and w["active"] and not w["noWaypoint"] and w["scene"]!=identity["scene"]]
            destination=next((s for s in ("Z52","Z72","B10") if s in choices),None)
            if not destination:raise RuntimeError("No verified active test waypoint is available.")
            probe("visit:"+destination)
            deadline=time.monotonic()+30
            while time.monotonic()<deadline:
                now=command("identityread"); session=command("sessionread")
                if now.get("player",{}).get("scene")==destination and not session["transitioning"]:
                    after=command("mapread")
                    if after["hasMap"]:break
                time.sleep(.3)
            else:raise RuntimeError("The target zone map did not finish loading.")
            check(after["revealed"] and after["bindings"]>before["bindings"] and after["faults"]==0, "reveal automatically binds the newly loaded zone")
            capture("map-after-transition.png")
            normal=command("mapreveal 0")
            check(not normal["revealed"] and not any(normal["hooks"].values()), "new zone restores normal fog without retained hooks")
            capture("map-after-transition-off.png")
        elif kind == "materials":
            command("mapreveal 0")
            check(not any(command("mapread")["hooks"].values()), "neutral map has no hooks")
            capture("map-normal.png")
            revealed = command("mapreveal 1")
            check(revealed["revealed"] and revealed["renderOnly"] and revealed["faults"] == 0, "actual fog shader binds reveal texture")
            capture("map-revealed.png")
            command("mapreveal 1")
            normal = command("mapreveal 0")
            check(not normal["enabled"] and not normal["revealed"] and not any(normal["hooks"].values()), "off restores native fog and removes both hooks")
            for text in ("mapreveal 2", "mapreveal nan", "craftrunes 2", "craftglyphs -1", "craftlevel .5", "craftforge 0"):
                command(text, okay=False)
            check(True, "invalid values and wrong forge identity rejected")
            for enabled in (0, 1):
                probe("setup"); probe("modifierrune"); command(f"craftrunes {enabled}")
                before = probe("materials")["runeCount"]
                forged = command(f"craftforge {save_id}")
                after = probe("materials")["runeCount"]
                check(after == before - (1 - enabled), f"real Rune of Refinement consumption, preserve={enabled}")
                check(recovery.validate_backup(Path(forged["backup"]))["manifest"]["operation"] == "forge-one-item", "recoverable forge snapshot")
                probe("clear")
            command("craftrunes 0")
            for enabled in (0, 1):
                probe("setup"); probe("supporthope"); command("crafthope 0"); command(f"craftglyphs {enabled}")
                before = probe("materials")["glyphCount"]
                forged = command(f"craftforge {save_id}")
                after = probe("materials")["glyphCount"]
                check(after == before - (1 - enabled), f"real Glyph of Hope consumption, preserve={enabled}")
                probe("clear")
            command("craftreset")
            probe("setup"); probe("supporthope")
            command("craftglyphs 1"); command("craftshards 1"); command("craftfp 0")
            before = probe("materials")["glyphCount"]
            forged = command(f"craftforge {save_id}")
            check(probe("materials")["glyphCount"] == before, "combined free FP / shard / glyph controls do not duplicate glyphs")
            check(forged["crafting"]["telemetry"]["faults"] == 0, "no crafting preparation or refund faults")
            probe("clear")
        elif kind == "level":
            probe("lowsetup")
            before = command("craftread")["preview"]
            check(not before["canForge"] and "level" in before["message"].lower(), "native affix level requirement blocks low-level clone")
            command("craftlevel 1")
            allowed = command("craftread")
            # Read currently serializes counters before obtaining the preview;
            # a second read observes the hook call made by the first preview.
            counters = command("craftread")["telemetry"]
            check(allowed["preview"]["canForge"] and counters["levelChecks"] > 0, "scoped level bypass enables the same craft")
            forged = command(f"craftforge {save_id}")
            check(forged["preview"]["affixes"] != before["affixes"], "actual affix tier changes on low-level clone")
            command("craftlevel 0"); probe("clear")
        else: raise ValueError(kind)
        reset = command("craftreset")
        check(not any(reset["hooks"].values()), "reset removes all seven crafting hooks")
        check(reset["telemetry"]["faults"] == 0, "native crafting fault counter remains zero")
        record(); return {"ok": True, "checks": len(checks), "evidence": str(target)}
    finally:
        command("mapreveal 0"); command("craftreset")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("kind", choices=("materials", "level", "map"))
    print(json.dumps(verify(parser.parse_args().kind), indent=2))
