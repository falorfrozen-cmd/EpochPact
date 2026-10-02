r"""Live check of the experience multiplier, on an offline character that is already in the world.

    py -3 tools/live_xp_check.py [--out research-out/live-xp-<time>.txt]

Before running: the research build is installed (tools/le_session.py install), the game was
started with tools/le_session.py launch (it backs the saves up), and an OFFLINE character
has been loaded into the world by hand. The check refuses unless the gate reads offline.

Steps, each through the command channel:
  1. gate and tracker (xpread)
  2. baseline: xpgain 1000 at x1                    -> gain A
  3. xp 3, xpgain 1000                               -> gain B, expected 3 x A
  4. xp 1 (hook removed), xpgain 1000               -> gain C, expected A
  5. the real kill path, when an enemy has spawned: xpkill at x1 and at x3
Afterwards close the game (le_session.py close) and put the saves back
(le_session.py restore-saves <the backup launch printed>), so the test leaves no trace.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from le_session import OUT, send  # noqa: E402

XP = re.compile(r"experience (-?\d+) / (-?\d+)")


def experience(line: str) -> int | None:
    m = XP.findall(line or "")
    return int(m[-1][0]) if m else None


def gain(reply: str | None) -> int | None:
    """xpgain/xpkill replies carry 'before ... experience X / N; after ... experience Y / N'."""
    m = XP.findall(reply or "")
    return int(m[1][0]) - int(m[0][0]) if len(m) >= 2 else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(OUT / f"live-xp-{_dt.datetime.now():%Y%m%d-%H%M%S}.txt"))
    args = ap.parse_args()
    lines: list[str] = []

    def say(text: str) -> None:
        print(text)
        lines.append(text)

    status = send("status") or ""
    say(status)
    if "gate: offline play" not in status:
        say("REFUSED: the gate does not read offline play; load an offline character first")
        return 2
    say(send("xpread") or "xpread: no reply")

    send("xp 1")
    a = gain(send("xpgain 1000", 15))
    say(f"x1 baseline: xpgain 1000 gave {a}")
    say(send("xp 3") or "")
    b = gain(send("xpgain 1000", 15))
    say(f"x3: xpgain 1000 gave {b}")
    say(send("xp 1") or "")
    c = gain(send("xpgain 1000", 15))
    say(f"x1 again (hook removed): xpgain 1000 gave {c}")

    verdicts = []
    if a and b is not None and c is not None:
        verdicts.append(("x3 is three times x1", abs(b - 3 * a) <= max(2, a // 100)))
        verdicts.append(("x1 after removal matches the baseline", abs(c - a) <= max(1, a // 100)))
    else:
        verdicts.append(("experience moved at all (max level characters gain nothing)", False))

    enemies = send("enemies") or ""
    say(enemies)
    if re.search(r"enemies: [1-9]", enemies):
        k1 = gain(send("xpkill", 15))
        say(send("xp 3") or "")
        k3 = gain(send("xpkill", 15))
        say(send("xp 1") or "")
        say(f"kill path: x1 gave {k1}, x3 gave {k3}")
        if k1 and k3 is not None:
            verdicts.append(("a real enemy's kill experience is tripled at x3", abs(k3 - 3 * k1) <= max(2, k1 // 100)))
    else:
        say("kill path: no live enemy captured (stand in a zone with monsters to include it)")

    say(send("status") or "")
    for name, ok in verdicts:
        say(f"{'pass' if ok else 'FAIL'} {name}")
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"written: {args.out}")
    return 0 if verdicts and all(ok for _, ok in verdicts) else 1


if __name__ == "__main__":
    sys.exit(main())
