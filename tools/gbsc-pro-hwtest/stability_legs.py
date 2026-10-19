#!/usr/bin/env python3
"""Pool `picture_stability.py` legs per state and say what actually separates.

    python3 tools/gbsc-pro-hwtest/stability_legs.py <dir>

Reads the legs a run left in <dir>, named `stability-dlt<VALUE>-r<ROUND>.log`.

**POISSON IS THE WRONG TEST HERE.** The counts are overdispersed 2.2 to 2.9
leg to leg, so the square root of a pooled count understates the error and
publishes levers that are not there: SP_DLT_REG 512 reads 3.7 sigma worse than
192 pooled and t 1.3, not separated, on what its legs did. Both are printed,
and the leg-to-leg one is the answer.

**A SINGLE EMPTY LEG IS NOT A STATE.** Five 300 s legs returned zero across one
night at three different thresholds, so a zero arm gets a Poisson upper bound
rather than a verdict.

**LOST FRAMES ARE PART OF THE READING.** A leg that lost every frame reports
zero displaced, which is the cleanest-looking result a run can produce; the
loud end does exactly that.

docs/investigations/the-large-composite-jump-is-a-torn-frame.md
"""
import glob
import math
import os
import re
import sys

LINE = re.compile(r"(\d+) of (\d+) frames displaced.*?worst (-?\d+) rows, (\d+) lost")

legs = {}
where = sys.argv[1] if len(sys.argv) > 1 else "."
for path in sorted(glob.glob(os.path.join(
        os.path.expanduser(where), "stability-dlt*.log"))):
    name = os.path.basename(path)
    m = re.match(r"stability-dlt(\d+)-r(\d+)\.log", name)
    if not m:
        continue
    held = int(m.group(1))
    hit = LINE.search(open(path).read())
    if not hit:
        continue
    legs.setdefault(held, []).append(
        (int(hit.group(1)), int(hit.group(2)), int(hit.group(3)),
         int(hit.group(4))))

# A LEG THAT LOST MOST OF ITS FRAMES IS NOT A MEASUREMENT. One at the loud end
# lost all 18003 and reported 0 displaced, which averages into a state as the
# cleanest leg of the run. They are reported and then left out.
VOID_LOST_SHARE = 0.05
void = {}
for held in list(legs):
    kept = [r for r in legs[held] if r[3] <= VOID_LOST_SHARE * r[1]]
    dropped = [r for r in legs[held] if r[3] > VOID_LOST_SHARE * r[1]]
    if dropped:
        void[held] = dropped
    if kept:
        legs[held] = kept
    else:
        del legs[held]

if void:
    print("legs left out, having lost more than "
          f"{VOID_LOST_SHARE:.0%} of their frames:")
    for held in sorted(void):
        for d, frames, _worst, lost in void[held]:
            print(f"  {held:>6}: {d} displaced of {frames}, {lost} lost "
                  f"({lost / frames:.0%})")
    print()

if not legs:
    print("no legs yet")
    sys.exit(0)

print(f"{'held':>6}  {'legs':>4}  {'events':>6}  {'frames':>7}  "
      f"{'per 300s':>8}  {'+/-':>5}  {'worst':>6}  {'lost':>6}  legs")
rows = {}
for held in sorted(legs):
    runs = legs[held]
    events = sum(r[0] for r in runs)
    frames = sum(r[1] for r in runs)
    worst = max((abs(r[2]) for r in runs), default=0)
    lost = sum(r[3] for r in runs)
    per = events / len(runs)
    err = math.sqrt(events) / len(runs)
    rows[held] = (events, len(runs), per, err)
    flag = "  LOST FRAMES" if lost else ""
    print(f"{held:>6}  {len(runs):>4}  {events:>6}  {frames:>7}  "
          f"{per:>8.1f}  {err:>5.1f}  {worst:>6}  {lost:>6}  "
          f"{','.join(str(r[0]) for r in runs)}{flag}")

def spread(xs):
    """Leg-to-leg sd, which is what a state is judged on. The counts are
    overdispersed, so Poisson understates the error."""
    n = len(xs)
    if n < 2:
        return None
    m = sum(xs) / n
    return math.sqrt(sum((x - m) ** 2 for x in xs) / (n - 1))


# A DRIFT SHARED BY EVERY STATE IS WHAT THE INTERLEAVING ABSORBS; one confined
# to a single state would mean its legs are not a state at all. Slopes pointing
# in opposite directions on counts this small are noise, not a trend.
print("\ndrift across rounds (count against round number, least squares):")
for held in sorted(legs):
    xs = [r[0] for r in legs[held]]
    n = len(xs)
    if n < 3:
        print(f"  {held:>6}: needs three rounds")
        continue
    mx = (n - 1) / 2.0
    my = sum(xs) / n
    den = sum((i - mx) ** 2 for i in range(n))
    slope = sum((i - mx) * (x - my) for i, x in enumerate(xs)) / den if den else 0.0
    print(f"  {held:>6}: {slope:+7.1f} per round   {xs}")

print("\noverdispersion check, per threshold:")
for held in sorted(legs):
    xs = [r[0] for r in legs[held]]
    sd = spread(xs)
    m = sum(xs) / len(xs)
    poisson = math.sqrt(m) if m else 0.0
    if sd is None:
        print(f"  {held:>6}: one leg, no spread yet")
    else:
        ratio = sd / poisson if poisson else float("inf")
        print(f"  {held:>6}: legs sd {sd:5.1f}  poisson {poisson:5.1f}  "
              f"ratio {ratio:4.1f}" + ("  OVERDISPERSED" if ratio > 1.5 else ""))

base = rows.get(192)
if base and len(rows) > 1:
    print("\nagainst the engine's own 192, per 300 s:")
    for held in sorted(rows):
        if held == 192:
            continue
        e, n, per, err = rows[held]
        d = per - base[2]
        # A ZERO ARM HAS NO SYMMETRIC SIGMA. sqrt(0) is 0, which makes a single
        # empty leg read as a firm separation; the honest statement is the
        # Poisson upper bound on a rate that produced no events.
        if e == 0:
            hi = 3.0 / n
            sep = "better" if hi < base[2] and n >= 3 else "needs more legs"
            print(f"  {held:>6}: {d:+7.1f}   0 events in {n} leg(s), "
                  f"95% upper bound {hi:.1f}/300s   {sep}")
            continue
        sd = math.sqrt(base[0] / base[1] ** 2 + e / n ** 2)
        sig = abs(d) / sd if sd else 0.0
        enough = base[1] >= 3 and n >= 3
        verdict = (("WORSE" if d > 0 else "better") if sig >= 3 and enough
                   else ("not separated" if enough else "needs more legs"))
        print(f"  {held:>6}: {d:+7.1f}  {sig:4.1f} sigma   {verdict}")
    print("\nagainst 192 on the LEG-TO-LEG spread, which is the honest test")
    print("while the counts stay overdispersed (Welch on the per-leg counts):")
    bl = [r[0] for r in legs[192]]
    for held in sorted(legs):
        if held == 192:
            continue
        xs = [r[0] for r in legs[held]]
        sa, sb = spread(xs), spread(bl)
        if sa is None or sb is None:
            print(f"  {held:>6}: needs two legs on both arms")
            continue
        se = math.sqrt(sa ** 2 / len(xs) + sb ** 2 / len(bl))
        d = sum(xs) / len(xs) - sum(bl) / len(bl)
        t = d / se if se else 0.0
        enough = len(xs) >= 3 and len(bl) >= 3
        call = (("WORSE" if d > 0 else "better") if abs(t) >= 2.5 and enough
                else ("not separated" if enough else "needs more legs"))
        print(f"  {held:>6}: {d:+7.1f}  t {t:5.1f}   {call}")

    print("\nA zero leg is not a zero rate: Poisson on 0 events in n legs puts")
    print("the 95% upper bound at about 3.0/n per 300 s. Five empty 300 s legs")
    print("have occurred at 192, 512 and 1024, so one is no evidence.")
