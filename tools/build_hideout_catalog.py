"""Make ui/hideout-catalog.tsv from a scan of the game's bundles (bosstest assetprefabs in a test game).

    py -3 tools/build_hideout_catalog.py <prefabs.jsonl> <bundle count>

One line per thing a player can place: bundle index, asset Guid, category, name. The list fits one game
build: hideout.cpp compares the bundle count and says so if it does not fit.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LEAVE_OUT = re.compile(r"^(ANIM|VFX|FX_|MODEL_)|_LOD[1-9]|LOD[1-9]$|helmet|boots|gloves|bodyarmor|_back_|weapon|sword|shield|qxl|vfx|spawner|acolyte|primalist|rogue|sentinel|^mage|armou?r|_low_|player|"
                       r"tentacle|@|^MTX_Portal|^Wrongwarp|^DropPlane|forearm|dagger|spear\d*$|^arrow|wand$|ground item|^\d+H|^\d+h |\baxe\b|\bmace\b|\bbow\b|\bstaff\b|\brig\b|^\[|^A\d+", re.I)
CATEGORIES = (
    ("c", re.compile(r"^v_|^MTX_Pet|boss|harbinger|^Enemy|creature|spider|wraith|skeleton|zombie|dragon|golem|lich|lizard", re.I)),
    ("b", re.compile(r"wall|house|roof|tower|stair|floor|bridge|gate|door|arch|column|pillar|fence|build|ruin|castle|road|platform|temple|shrine_|shed|tent|hut|dome|balcony|tile|brick", re.I)),
    ("n", re.compile(r"tree|plant|grass|rock|stone|cliff|mushroom|vine|bush|fern|flower|crystal|stalag|root|trunk|branch|moss|coral|ice|snow|boulder|leaf|leaves", re.I)),
    ("p", re.compile(r"chest|stash|cache|table|crate|vase|barrel|torch|brazier|statue|fountain|book|banner|anvil|altar|lantern|lamp|candle|bones?|skull|chair|bench|cart|pot|sack|rug|carpet|flag|sculpt|forge|cage|cauldron|plank", re.I)),
)


def category(name: str) -> str:
    return next((key for key, words in CATEGORIES if words.search(name)), "o")


def main() -> int:
    source, bundles = Path(sys.argv[1]), int(sys.argv[2])
    rows = [json.loads(line) for line in source.read_text(encoding="utf-8").splitlines() if line.strip()]
    keep, seen = [], set()
    for r in rows:
        name = r["n"].replace("\t", " ").strip()
        if not (r.get("m") or r.get("s")) or LEAVE_OUT.search(name) or name in seen:
            continue
        seen.add(name)
        keep.append((name.lower(), r["f"], r["g"], category(name), name))
    keep.sort()
    out = ROOT / "ui/hideout-catalog.tsv"
    lines = ["# made by tools/build_hideout_catalog.py: bundle, asset, category, name", f"#bundles\t{bundles}"]
    lines += [f"{f}\t{g}\t{c}\t{n}" for _, f, g, c, n in keep]
    out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    counts = {key: sum(1 for row in keep if row[3] == key) for key in "bnpco"}
    print(len(keep), counts, out.stat().st_size // 1024, "KB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
