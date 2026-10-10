"""Offline live checks for the full stat editor. Leaves only the owner's five requested bonuses.

Run after `playerread` confirms Falor level 8 in a zone. Uses IPC only, backs up saves,
and records every assertion/reply in research/live/full-stat-editor-check.json.
"""
from __future__ import annotations
import json
import re
from pathlib import Path
import le_session as le


def run() -> None:
    results: list[dict] = []

    def send(command: str) -> str:
        reply = le.send(command, timeout=15)
        results.append({"command": command, "reply": reply})
        (le.OUT / "full-stat-editor-check.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
        return reply

    def field(reply: str, name: str) -> float:
        match = re.search(r"(?:^|[ ;])" + re.escape(name) + r"=([-+0-9.eE]+)", reply)
        assert match, (name, reply)
        return float(match[1])

    identity = send("playerread")
    assert "name=Falor level=8 CharacterData.IsOffline=true" in identity, identity
    print("backup:", le.backup_saves(), flush=True)
    # Reset is scoped to EpochPact-owned entries and never edits equipment entries.
    assert "removed" in send("statreset")
    baseline = {sp: send(f"statraw {sp} 0 0 0") for sp in range(134)}
    assert all("refused" not in reply and "guarded" not in reply for reply in baseline.values())
    # Exercise real constructor, typed List.Add/Remove, recalculation and getters for
    # every enum property using neutral values. This does not claim combat testing.
    for sp in range(134):
        reply = send(f"statraw {sp} 0 0 0 added 0")
        assert reply.startswith("EpochPact added="), (sp, reply)
    for sp, before in baseline.items():
        after = send(f"statraw {sp} 0 0 0")
        for name in ["added", "increased", "moreMultiplier", "entries", "applicableAdded", "applicableIncreased", "applicableMore"]:
            assert abs(field(before, name) - field(after, name)) < 1e-5, (sp, name, before, after)
    print("134 properties: neutral insertion/removal and baseline preservation passed", flush=True)

    for command in ["statraw 134 0 0 0 added 1", "statraw 1 0 256 0 added 1", "statraw 1 0 -1 0 added 1",
                    "statraw 1 2147483648 0 0 added 1", "statraw 1 0 0 0 more -1.1", "statraw 1 0 0 0 added nan",
                    "statraw 1 0 0 0 added 1e-500", "stat strength 1e-500",
                    "statraw 1 0 0 0 added 1 extra", "stat strength 2 extra"]:
        assert "refused" in send(command), command

    # Distinct ailments must not overwrite one another; minion/ability/player scopes
    # and more modifiers must survive getter calls and be removable without residue.
    cases = [("1 512 1 0", "added", .2), ("1 512 2 0", "added", .3),
             ("0 8192 0 0", "increased", .25), ("0 256 0 0", "more", .1),
             ("98 636 0 0", "added", .002), ("58 782 0 0", "added", .01)]
    for key, mode, value in cases:
        assert send(f"statraw {key} {mode} {value}").startswith("EpochPact ")
    for key, mode, value in cases:
        reply = send(f"statraw {key}")
        own = reply.split("EpochPact ", 1)[1]
        assert abs(field(own, mode) - value) < 1e-6, (key, reply)
    for key, _, _ in cases:
        assert "modifier reset" in send(f"statraw {key} reset")
        assert "EpochPact added=" not in send(f"statraw {key}")
    print("ailment, minion, PlayerProperty, AbilityProperty and more/reset passed", flush=True)

    assert "sheetopen: open" in send("sheetopen 1")
    catalogue = send("sheetstats")
    assert catalogue.startswith("sheetstats:"), catalogue
    rows = [line for line in catalogue.splitlines() if re.match(r"\d+ name=", line)]
    assert len(rows) >= 187, len(rows)
    (le.OUT / "character-sheet-catalog.txt").write_text(catalogue, encoding="utf-8")
    seen: set[tuple[int, int, int, int]] = set()
    for line in rows:
        match = re.search(r"SP=(\d+) \([^)]*\) tags=(-?\d+) special=(\d+) extra=(-?\d+)", line)
        assert match, line
        key = tuple(map(int, match.groups()))
        rowid = line.split(" ", 1)[0]
        assert "refused" not in send(f"sheetstat {rowid}"), line
        if key not in seen:
            assert "EpochPact added=" in send(f"sheetstat {rowid} added 0"), line
            seen.add(key)
        if "modifier={" in line:
            assert "refused" not in send(f"sheetstat {rowid} modifier"), line
    print(f"{len(rows)} sheet rows / {len(seen)} distinct base keys passed", flush=True)
    assert "removed 0" in send("statreset")

    requested = [("allres", .65), ("bowattackspeed", 5), ("meleeattackspeed", 5), ("parry", .5), ("reflect", 10)]
    for name, value in requested:
        assert send(f"stat {name} {value}").startswith("EpochPact ")
        # Repeating a setting must replace the contribution, not accumulate it.
        assert send(f"stat {name} {value}").startswith("EpochPact ")
    labels = send("sheetread")
    assert "FireRes=65%" in labels and "ColdRes=65%" in labels, labels
    send("sheetstats")
    print("requested bonuses restored; resistance labels:", labels, flush=True)
    print("PASS; evidence:", le.OUT / "full-stat-editor-check.json", flush=True)


if __name__ == "__main__":
    run()
