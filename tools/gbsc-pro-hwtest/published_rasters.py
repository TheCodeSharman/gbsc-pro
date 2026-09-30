"""Which published raster the scaler places a source from, and what that costs.

    python3 tools/gbsc-pro-hwtest/published_rasters.py path/to/Monitor.mdf

The rows are Tv5725::SourceTiming's own, read out of SourceTiming.cpp so they
cannot drift from the firmware, and matched by its rule: the earliest row in
CEA -> DMT -> Acorn order agreeing on frame lines, field rate within 5%, sync
duty within 0.015 and the polarity pair; where none agrees on the polarity, the
earliest agreeing on the first three. A mode file entry's key is what the chip
would measure of it, and the difference between the entry's own picture area and
the row's is the placement the scaler's default costs that mode, in the mode's
own pixels and lines -- the expected `dCapture` a sweep record is judged against.
"""

import argparse
import os
import re
import sys

import mdf_modes

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE_TIMING = os.path.join(HERE, "..", "..", "GBSC-Pro-Source code", "gbs-control",
                             "src", "tv5725", "SourceTiming.cpp")

# SourceTiming.cpp's tolerances.
SYNC_DUTY_TOLERANCE = 0.015
RATE_DEVIATION = 0.05

# Axis.cpp's envelope, for a source no row answers.
ENVELOPE = dict(name="envelope", authority="Axis", h_start=0.117, h_extent=0.864,
                v_start=0.061, v_extent=0.933)

# How close a framing proportion has to sit to a row's fractions to have come
# from it, in ten-thousandths: the DMT rows measured right to four decimals.
PROPORTION_TOLERANCE = 40

TABLE = re.compile(r"SourceTiming::(Cea|Dmt|Acorn)\[\] = \{(.*?)\};", re.S)
ROW = re.compile(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),"
                 r"\s*(\d+),\s*(\d+),\s*([PN]),\s*([PN])\s*\},?\s*//\s*(\S+)")


def rows(path=SOURCE_TIMING):
    """Every row of the three tables, in the order the lookup searches them."""
    text = open(path, encoding="utf-8").read()
    found = []
    for authority, body in TABLE.findall(text):
        for m in ROW.finditer(body):
            frame, rate, total, sync, start, active, vsync, vstart, vactive = (
                int(v) for v in m.groups()[:9])
            found.append(dict(authority=authority, name=m.group(12), frame=frame, rate=rate,
                              total=total, sync=sync, start=start, active=active,
                              vsync=vsync, vstart=vstart, vactive=vactive,
                              hpol=m.group(10), vpol=m.group(11),
                              h_start=start / total, h_extent=active / total,
                              v_start=vstart / frame, v_extent=vactive / frame))
    return found


def key_of(entry):
    """What the chip measures of a mode file entry, and where its picture sits.

    h_timings and v_timings are sync, back porch, border, display, border, front
    porch; sync_pol bit 0 inverts hsync and bit 1 vsync over an active-high
    default, so a set bit is a negative pulse.
    """
    h = [int(v) for v in entry["h_timings"].split(",")]
    v = [int(v) for v in entry["v_timings"].split(",")]
    htotal, vtotal = sum(h), sum(v)
    pol = int(entry.get("sync_pol", "0"))
    return dict(frame=vtotal, rate=int(entry["pixel_rate"]) * 1000.0 / htotal / vtotal,
                duty=h[0] / htotal, hpol="N" if pol & 1 else "P", vpol="N" if pol & 2 else "P",
                h_total=htotal, v_total=vtotal,
                h_start=(h[0] + h[1] + h[2]) / htotal, h_extent=h[3] / htotal,
                v_start=(v[0] + v[1] + v[2]) / vtotal, v_extent=v[3] / vtotal)


def _on_key(row, key):
    return (row["frame"] == key["frame"]
            and abs(key["rate"] - row["rate"]) <= RATE_DEVIATION * row["rate"]
            and abs(row["sync"] / row["total"] - key["duty"]) <= SYNC_DUTY_TOLERANCE)


def match(table, key):
    """The row SourceTiming::lookUp() answers with, or None."""
    for row in table:
        if _on_key(row, key) and (row["hpol"], row["vpol"]) == (key["hpol"], key["vpol"]):
            return row
    for row in table:
        if _on_key(row, key):
            return row
    return None


def expected_offsets(entry, row):
    """Where the row's placement lands against the entry's own picture, in the
    entry's pixels (left, right) and lines (top, bottom). Positive: the capture
    opens or closes AFTER the picture's edge -- picture clipped at the left,
    border shown at the right; negative the reverse. Zero on every edge where
    the row's active area is the entry's."""
    key = key_of(entry)
    placement = row if row is not None else ENVELOPE
    return dict(left=(placement["h_start"] - key["h_start"]) * key["h_total"],
                right=((placement["h_start"] + placement["h_extent"])
                       - (key["h_start"] + key["h_extent"])) * key["h_total"],
                top=(placement["v_start"] - key["v_start"]) * key["v_total"],
                bottom=((placement["v_start"] + placement["v_extent"])
                        - (key["v_start"] + key["v_extent"])) * key["v_total"])


def tier_answered(table, proportions):
    """The row a /geometry framing proportion came from, the envelope, or None
    for a framing that is neither -- a tuned one."""
    def near(candidate):
        return all(abs(proportions[field] - round(candidate[fraction] * 10000)) <= PROPORTION_TOLERANCE
                   for field, fraction in (("poh", "h_start"), ("peh", "h_extent"),
                                           ("pov", "v_start"), ("pev", "v_extent")))
    for row in table:
        if near(row):
            return row
    return ENVELOPE if near(ENVELOPE) else None


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mdf", help="the monitor definition the source is running")
    args = parser.parse_args()
    table = rows()
    print(f"{'mode':>10} {'frame':>5} {'Hz':>6} {'duty':>6} pol  answered by            "
          f"{'L':>6} {'R':>6} {'T':>5} {'B':>5}   (the mode's own px / lines)")
    for entry in mdf_modes.parse(args.mdf):
        try:
            key = key_of(entry)
        except (KeyError, ValueError):
            continue
        row = match(table, key)
        cost = expected_offsets(entry, row)
        who = f"{row['authority']} {row['name']}" if row else "envelope"
        print(f"{entry['x_res']:>5}x{entry['y_res']:<4} {key['frame']:5} {key['rate']:6.2f} "
              f"{key['duty']:6.4f} {key['hpol']}{key['vpol']}   {who:22} "
              f"{cost['left']:+6.1f} {cost['right']:+6.1f} {cost['top']:+5.1f} {cost['bottom']:+5.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
