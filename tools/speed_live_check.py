r"""Wait until the player actually walks, then measure x1 vs x5 walking speed.

    py -3 tools/speed_live_check.py [--wait 600]

Samples the player's world position (posread) twice a second. As soon as movement is
detected it runs three phases: 8 s at x1, 8 s at x5, then back to x1, reporting the
distance walked in each. The owner only has to walk; run it in the background and read
the result when it finishes. The script always restores x1 at the end.
"""

from __future__ import annotations

import argparse
import math
import re
import time

from le_session import send

POS = re.compile(r"posread:\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)")


def position() -> tuple[float, float, float] | None:
    reply = send("posread", 8)
    if not reply:
        return None
    m = POS.search(reply)
    return tuple(float(v) for v in m.groups()) if m else None  # type: ignore[return-value]


def collect(seconds: float) -> tuple[float, float] | None:
    """Distance walked and elapsed time over the window."""
    prev = position()
    if prev is None:
        return None
    total = 0.0
    samples = 0
    t0 = time.monotonic()
    while time.monotonic() - t0 < seconds:
        time.sleep(0.4)
        now = position()
        if now is None:
            continue
        total += math.dist(prev, now)
        prev = now
        samples += 1
    return total, time.monotonic() - t0, samples


def speed(numbers: tuple[float, float, float] | None) -> float:
    return 0.0 if not numbers or numbers[1] <= 0 else numbers[0] / numbers[1]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--wait", type=float, default=600, help="seconds to wait for movement")
    args = ap.parse_args()

    # Wait for the owner to start walking.
    prev = position()
    deadline = time.monotonic() + args.wait
    while time.monotonic() < deadline:
        time.sleep(0.5)
        now = position()
        if now and prev and math.dist(prev, now) > 0.5:
            break
        prev = now
    else:
        print("movement never detected; nothing measured")
        return 1

    print("movement detected; measuring (keep walking ~20 s)")
    send("speed 1")
    time.sleep(0.4)
    r1 = collect(8)
    send("speed 5")
    time.sleep(0.4)
    read5 = send("speedread") or ""
    r5 = collect(8)
    send("speed 1")
    time.sleep(0.4)
    read1 = send("speedread") or ""

    if not r1 or not r5:
        print("could not sample the player; is the game still running?")
        return 1
    s1, s5 = speed(r1), speed(r5)
    ratio = s5 / s1 if s1 else 0.0
    print(f"x1: {r1[0]:.2f} units in {r1[1]:.2f} s = {s1:.2f} u/s  ({r1[2]} samples)")
    print(f"x5: {r5[0]:.2f} units in {r5[1]:.2f} s = {s5:.2f} u/s  ({r5[2]} samples)")
    print(f"ratio: {ratio:.2f}x")
    print(f"agent after x5: {read5.strip()}")
    print(f"agent restored: {read1.strip()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
