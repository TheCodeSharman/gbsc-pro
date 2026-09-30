#!/usr/bin/env python3
"""Turn a framing_sweep run into the attribution table and the window fits.

    python3 tools/gbsc-pro-hwtest/framing_report.py tools/gbsc-pro-hwtest/sweeps/framing-<UTC>.jsonl
    ... --markdown          for a docs investigation

One row per state: the raster, the slope predicted and measured, the black at
each edge, the four terms at each end of the line, the expected capture
offset the mode's own file predicts, and the verdict. Then, per output mode,
the fits the campaign is run for: the window's measured start against the
mode's sync + porch, as a function of the raster total and of the clock (a
duration is flat in T, a fraction grows with it), and the window's width
against the mode's fraction of the line.
"""

import argparse
import json
import sys

import numpy as np

import framing_decomposition as fd

TERMS = ("dEncoder", "dModelApplied", "dPlace", "dCapture")


def load(path):
    with open(path) as handle:
        records = [json.loads(line) for line in handle if line.strip()]
    for record in records:
        if "positions" in record:
            record["residuals"], record["verdict"] = fd.rejudge(record)
    return records


def number(value, width=6, decimals=1):
    if value is None:
        return " " * (width - 1) + "-"
    return f"{value:+{width}.{decimals}f}"


def expected_units(record):
    """The mode file's expected capture offset per edge, in raster units: the
    mode's own pixels scaled by the units the line was captured into and the
    magnification the picture was scaled by."""
    expected = record.get("expected")
    if not expected or not record.get("geometry"):
        return None
    key, px = expected["key"], expected["offsets_px"]
    h = record["geometry"]["ch"] / key["h_total"] * record["positions"]["h"]["m"]
    v = record["geometry"]["cv"] / key["v_total"] * record["positions"]["v"]["m"]
    return dict(left=px["left"] * h, right=px["right"] * h, top=px["top"] * v, bottom=px["bottom"] * v)


def terms(edge):
    return " ".join(number(edge.get(term), 6, 1) for term in TERMS)


HEADER = (f"{'mode':24} {'output':6} {'T':>5} {'Hz':>6}  {'slope pred/near/far':21}  "
          f"{'black L R T B':>16}   {'L: dEnc dModel dPlace dCap':>28}   {'R: dEnc dModel dPlace dCap':>28}  "
          f"{'exp L R':>13}  verdict")


def format_row(record):
    if "skipped" in record:
        return f"{record['source']:24} {record['output']:6} SKIP  {record['skipped']}"
    window, slope, black = record["window"], record["slope"], record["black_cols"]
    rate = record["geometry"]["lineRateHz"] / (record["registers"]["STATUS_SYNC_PROC_VTOTAL"] + 1)
    h = record["residuals"]["h"]
    expected = expected_units(record)
    exp = (f"{number(expected['left'], 6, 1)} {number(expected['right'], 6, 1)}" if expected
           else f"{'-':>13}")
    slopes = "/".join("-" if slope.get(k) is None else f"{slope[k]:.3f}"
                      for k in ("h_pred", "h_near", "h_far"))
    return (f"{record['source']:24} {record['output']:6} {window['T']:5} {rate:6.2f}  {slopes:21}  "
            f"{black['left']:4}{black['right']:4}{black['top']:4}{black['bottom']:4}   "
            f"{terms(h['near']):>28}   {terms(h['far']):>28}  {exp:>13}  {record['verdict']}")


def sync_porch_units(record):
    """The mode's sync + back porch as OutputMode::solve() converts them, so
    the fits ask about the delay after them rather than about the whole start."""
    mode = fd.MODES[record["output"]]
    clock_hz = record["window"]["clock_hz"]
    scaled = lambda px: max(0, int(round(px * clock_hz / mode.standard_hz)))
    return max(1, scaled(mode.sync_px)) + scaled(mode.back_porch_px)


def fit(xs, ys):
    xs, ys = np.array(xs, float), np.array(ys, float)
    if xs.size < 3 or np.ptp(xs) == 0:
        return None
    slope, intercept = np.polyfit(xs, ys, 1)
    residual = float(np.abs(ys - (slope * xs + intercept)).max())
    return dict(slope=float(slope), intercept=float(intercept), worst=residual, points=int(xs.size))


def window_fits(records):
    """Per output mode, the measured window against the model's terms."""
    by_output = {}
    for record in records:
        window = record.get("window")
        if not window or window.get("E0") is None or window.get("E1") is None:
            continue
        by_output.setdefault(record["output"], []).append(record)
    fits = {}
    for output, group in by_output.items():
        mode = fd.MODES[output]
        totals = [r["window"]["T"] for r in group]
        clocks = [r["window"]["clock_hz"] for r in group]
        delays = [r["window"]["E0"] - sync_porch_units(r) for r in group]
        widths = [r["window"]["E1"] - r["window"]["E0"] for r in group]
        fractions = [r["window"]["T"] * mode.carried_px / mode.total_px for r in group]
        fits[output] = dict(points=len(group),
                            start_vs_total=fit(totals, delays),
                            start_vs_clock=fit(clocks, delays),
                            width_vs_fraction=fit(fractions, widths),
                            delays=delays, widths_off=[w - f for w, f in zip(widths, fractions)])
    return fits


def describe_fit(name, got):
    if got is None:
        return f"    {name:18} too few distinct points"
    return (f"    {name:18} slope {got['slope']:+.4f}  intercept {got['intercept']:+.2f}  "
            f"worst {got['worst']:.2f}  over {got['points']}")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("path")
    parser.add_argument("--markdown", action="store_true")
    args = parser.parse_args()
    records = load(args.path)

    if args.markdown:
        print("| mode | output | T | Hz | black L/R/T/B | L: dEnc dModel dPlace dCap | "
              "R: dEnc dModel dPlace dCap | expected L/R | verdict |")
        print("|---|---|---|---|---|---|---|---|---|")
        for record in records:
            if "skipped" in record:
                print(f"| {record['source']} | {record['output']} | | | | | | | SKIP {record['skipped']} |")
                continue
            black = record["black_cols"]
            h = record["residuals"]["h"]
            rate = record["geometry"]["lineRateHz"] / (record["registers"]["STATUS_SYNC_PROC_VTOTAL"] + 1)
            expected = expected_units(record)
            exp = f"{expected['left']:+.1f}/{expected['right']:+.1f}" if expected else "-"
            print(f"| {record['source']} | {record['output']} | {record['window']['T']} | {rate:.2f} | "
                  f"{black['left']}/{black['right']}/{black['top']}/{black['bottom']} | "
                  f"{terms(h['near'])} | {terms(h['far'])} | {exp} | {record['verdict']} |")
    else:
        print(HEADER)
        for record in records:
            print(format_row(record))

    fits = window_fits(records)
    if fits:
        print("\nthe encoder's window, measured, against the model's terms")
        for output, got in fits.items():
            print(f"  {output}: {got['points']} state(s); delay after sync+porch "
                  + ", ".join(f"{d:+.1f}" for d in got["delays"])
                  + "; width against the fraction "
                  + ", ".join(f"{w:+.1f}" for w in got["widths_off"]))
            print(describe_fit("start vs T", got["start_vs_total"]))
            print(describe_fit("start vs clock", got["start_vs_clock"]))
            print(describe_fit("width vs fraction", got["width_vs_fraction"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
