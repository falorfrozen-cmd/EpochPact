r"""Measure how far the player actually walks over a window, with the game's own numbers.

    py -3 tools/speed_check.py [--seconds 4] [--interval 0.5]

Samples `posread` (the player's world position through PlayerFinder + Transform) and prints
the total path length. Run it while the owner walks; compare x1 and x5, WASD and mouse.
"""

from __future__ import annotations

import argparse
import math
import re
import time

from le_session import send

POS = re.compile(r"posread:\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)")


def position() -> tuple[float, float, float] | None:
    reply = send("posread", 6)
    if not reply:
        return None
    m = POS.search(reply)
    return tuple(float(v) for v in m.groups()) if m else None  # type: ignore[return-value]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--seconds", type=float, default=4.0)
    ap.add_argument("--interval", type=float, default=0.5)
    args = ap.parse_args()

    start = position()
    if start is None:
        print("no player position (is the game in a zone with EpochPact running?)")
        return 1

    total = 0.0
    last = start
    t0 = time.monotonic()
    while time.monotonic() - t0 < args.seconds:
        time.sleep(args.interval)
        now = position()
        if now is None:
            print("lost the player position mid-run")
            return 1
        total += math.dist(last, now)
        last = now
    elapsed = time.monotonic() - t0
    print(f"walked {total:.2f} units in {elapsed:.2f} s ({total / elapsed:.2f} u/s); start {start}, end {last}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
