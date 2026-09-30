#!/usr/bin/env python3
"""Turn a framing_sweep run into the attribution table and the window fits.

    python3 tools/gbsc-pro-hwtest/framing_report.py tools/gbsc-pro-hwtest/sweeps/framing-<UTC>.jsonl [more runs]
    ... --markdown          for a docs investigation
    ... --acquisitions      every acquisition in run order, with the raster it came from
    ... --sink              the sink's own window per state, grouped by raster, and its fits

One row per state: the raster, the slope predicted and measured, the black at
each edge, the four terms at each end of the line, the expected capture
offset the mode's own file predicts, and the verdict. Then, per output mode,
the fits the campaign is run for: the window's measured start against the
mode's sync + porch, as a function of the raster total and of the clock (a
duration is flat in T, a fraction grows with it), and the window's width
against the mode's fraction of the line.
"""

import argparse
import gzip
import json
import sys

import numpy as np

import framing_decomposition as fd

TERMS = ("dEncoder", "dModelApplied", "dPlace", "dCapture")


def refit_walks(walks, slope=None):
    """The walks fitted again from the points they kept, so a run is judged by
    the fit as it stands rather than by the one it was written under. Given
    the raster's slope they are read as walks taken with black at the edge."""
    import transmitted_window
    if slope:
        transmitted_window.read_at_slope(walks, slope)
    else:
        for walk in walks.values():
            walk["strip_zero"], walk["strip_slope"] = transmitted_window.crossing(walk.get("strips") or [])[:2]
            walk["black_zero"], walk["black_slope"] = transmitted_window.crossing(walk.get("blacks") or [])[:2]
    return fd.measured_window(walks)


def refit(record):
    """Every window in the record read off its walks again."""
    if "walk" in record:
        window, instruments = refit_walks(record["walk"])
        record["window"].update(window)
        record["window"]["instruments"] = instruments
    sink = record.get("sink") or {}
    if "walk" in sink:
        cols, _rows = fd.slope_predicted(sink["window"]["carried"], sink["window"]["T"])
        window, instruments = refit_walks(sink["walk"], slope=cols)
        sink["window"].update(E0=window["E0"], E1=window["E1"],
                              instruments=dict(E0=instruments["E0"], E1=instruments["E1"]))


def load(path):
    opener = gzip.open if str(path).endswith(".gz") else open
    with opener(path, "rt") as handle:
        records = [json.loads(line) for line in handle if line.strip()]
    for record in records:
        refit(record)
        if "positions" in record:
            fd.remodel(record)
            record["residuals"], record["verdict"] = fd.rejudge(record)
    return records


def load_runs(paths):
    """Several runs read as one, in the order given, so a campaign split across
    invocations is one table and one set of fits."""
    return [record for path in paths for record in load(path)]


def shown_output(record):
    """The output asked for, and the one the registers carry where the engine
    fell back to another."""
    carried = record.get("carried", record["output"])
    return record["output"] if carried == record["output"] else f"{record['output']}>{carried}"


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


HEADER = (f"{'mode':24} {'output':11} {'T':>5} {'Hz':>6}  {'slope pred/near/far':21}  "
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
    return (f"{record['source']:24} {shown_output(record):11} {window['T']:5} {rate:6.2f}  {slopes:21}  "
            f"{black['left']:4}{black['right']:4}{black['top']:4}{black['bottom']:4}   "
            f"{terms(h['near']):>28}   {terms(h['far']):>28}  {exp:>13}  {record['verdict']}")


def sync_porch_units_of(output, clock_hz):
    """The mode's sync + back porch as OutputMode::solve() converts them, so
    the fits ask about the delay after them rather than about the whole start."""
    mode = fd.MODES[output]
    scaled = lambda px: max(0, int(round(px * clock_hz / mode.standard_hz)))
    return max(1, scaled(mode.sync_px)) + scaled(mode.back_porch_px)


def sync_porch_units(record):
    return sync_porch_units_of(record.get("carried", record["output"]), record["window"]["clock_hz"])


def default_window(record):
    """The window the judged default frame carries, with the mode it was
    modelled as."""
    window = record.get("window")
    if not window:
        return None
    return dict(window, carried=record.get("carried", record["output"]))


def sink_window(record):
    """The window the sink probe placed, or None where the probe did not run
    or did not land."""
    sink = record.get("sink") or {}
    return None if "skipped" in sink else sink.get("window")


def fit(xs, ys):
    xs, ys = np.array(xs, float), np.array(ys, float)
    if xs.size < 3 or np.ptp(xs) == 0:
        return None
    slope, intercept = np.polyfit(xs, ys, 1)
    residual = float(np.abs(ys - (slope * xs + intercept)).max())
    return dict(slope=float(slope), intercept=float(intercept), worst=residual, points=int(xs.size))


def window_fits(records, window_of=default_window):
    """Per output mode, a measured window against the model's terms: the start
    as a delay after the mode's sync and porch, against the raster total and
    against the clock, and the width against the mode's fraction of the line."""
    by_output = {}
    for record in records:
        window = window_of(record)
        if not window or window.get("E0") is None:
            continue
        by_output.setdefault(window["carried"], []).append(window)
    fits = {}
    for output, group in by_output.items():
        mode = fd.MODES[output]
        totals = [w["T"] for w in group]
        clocks = [w["clock_hz"] for w in group]
        delays = [w["E0"] - sync_porch_units_of(output, w["clock_hz"]) for w in group]
        widened = [w for w in group if w.get("E1") is not None]
        widths = [w["E1"] - w["E0"] for w in widened]
        fractions = [w["T"] * mode.carried_px / mode.total_px for w in widened]
        fits[output] = dict(points=len(group),
                            start_vs_total=fit(totals, delays),
                            start_vs_clock=fit(clocks, delays),
                            width_vs_fraction=fit(fractions, widths),
                            delays=delays, widths_off=[w - f for w, f in zip(widths, fractions)])
    return fits


def sink_fits(records):
    return window_fits(records, window_of=sink_window)


# Black the frame may carry at its left edge with the picture flush to the
# window: the allowance a flush verdict gets. More than this and the window
# opened on black rather than on content, which is what the probe needs.
SINK_BLACK_COLS = 2


def black_at_the_edge(sink, slope):
    """Columns of the source's own black between the window's start and the
    content when the pad returned. The frame's own count, or the near walk's
    first step less the blanking that step had put there -- the larger, since
    the frame after a toggle can be from before the sink re-acquired."""
    counted = sink["black_left"]
    walk = (sink.get("walk") or {}).get("h_near") or {}
    steps, e0 = walk.get("blacks") or [], sink["window"].get("E0")
    if steps and e0 is not None:
        value, at = steps[0]
        counted = max(counted, at - max(0.0, (value - e0) * slope))
    return counted


def sink_rows(records):
    """One row per state whose sink probe landed: the window the pad's return
    placed, as a delay after the mode's sync and porch, beside what the frame
    carried at its edge -- black, or the picture, which the sink pulls the
    window to and which is then not its own position."""
    rows = []
    for record in records:
        window = sink_window(record)
        if not window or window.get("E0") is None:
            continue
        sink = record["sink"]
        mode = fd.MODES[window["carried"]]
        sync_porch = sync_porch_units_of(window["carried"], window["clock_hz"])
        fraction = window["T"] * mode.carried_px / mode.total_px
        e1 = window.get("E1")
        black = black_at_the_edge(sink, fd.slope_predicted(window["carried"], window["T"])[0])
        rows.append(dict(source=record["source"], output=window["carried"], T=window["T"],
                         rate=window["clock_hz"] / (window["T"] * fd.frame_lines(mode)),
                         clock_hz=window["clock_hz"], sync_porch=sync_porch,
                         E0=window["E0"], delay=window["E0"] - sync_porch, E1=e1,
                         E0_sink_px=window["E0"] * mode.total_px / window["T"],
                         delay_sink_px=(window["E0"] - sync_porch) * mode.total_px / window["T"],
                         width_off=None if e1 is None else (e1 - window["E0"]) - fraction,
                         black_left=black,
                         own_position=black > SINK_BLACK_COLS,
                         pan_units=sink["pan"]["moved_units"], instruments=window["instruments"]))
    return rows


def raster_groups(rows):
    """The sink rows that share a raster -- output, total and field rate to a
    tenth -- with the spread of the window's start across them."""
    groups = {}
    for row in rows:
        key = (row["output"], row["T"], round(row["rate"], 1))
        groups.setdefault(key, []).append(row)
    out = []
    for (output, total, rate), members in sorted(groups.items()):
        starts = [m["E0"] for m in members]
        px = [m["E0_sink_px"] for m in members]
        out.append(dict(output=output, T=total, rate=rate, n=len(members),
                        mean=sum(starts) / len(starts), spread=max(starts) - min(starts),
                        sink_px=sum(px) / len(px),
                        own=sum(1 for m in members if m["own_position"]),
                        sources=[m["source"] for m in members]))
    return out


SINK_HEADER = (f"{'source':24} {'output':6} {'T':>5} {'Hz':>6} {'MHz':>6} {'s+p':>4} {'E0':>7} "
               f"{'delay':>6} {'sink px':>8} {'dly px':>7} {'E1':>8} {'width-frac':>10} "
               f"{'black':>5} {'pan':>4}  instruments")


def format_sink_row(row):
    instruments = row["instruments"]
    return (f"{row['source']:24} {row['output']:6} {row['T']:5} {row['rate']:6.2f} "
            f"{row['clock_hz'] / 1e6:6.1f} {row['sync_porch']:4} {number(row['E0'], 7, 1):>7} "
            f"{number(row['delay'], 6, 1):>6} {row['E0_sink_px']:8.1f} {number(row['delay_sink_px'], 7, 1):>7} "
            f"{number(row['E1'], 8, 1):>8} "
            f"{number(row['width_off'], 10, 1):>10} {row['black_left']:5.0f} {row['pan_units']:+4d}  "
            f"{instruments.get('E0')}/{instruments.get('E1')}"
            + ("" if row["own_position"] else "  CONTENT AT THE EDGE"))


def acquisitions(records):
    """One row per acquisition in run order, with the raster the previous
    measured state ran: a placement that follows the transition into a raster
    rather than the state shows only this way. A skipped state breaks the
    chain, since what raster the unit was left on is not recorded."""
    rows, previous = [], None
    for record in records:
        if "skipped" in record:
            previous = None
            continue
        window = record["window"]
        e0, e1 = window.get("E0"), window.get("E1")
        mode = fd.MODES[record.get("carried", record["output"])]
        fraction = window["T"] * mode.carried_px / mode.total_px
        rows.append(dict(source=record["source"], output=shown_output(record), T=window["T"],
                         from_T=previous, E0m=window["E0m"], E0=e0,
                         delay=None if e0 is None else e0 - sync_porch_units(record),
                         E1m=window["E1m"], E1=e1,
                         width_off=None if e0 is None or e1 is None else (e1 - e0) - fraction,
                         black=record["black_cols"],
                         instruments=(window.get("instruments") or {})))
        previous = window["T"]
    return rows


ACQUISITION_HEADER = (f"{'source':24} {'output':11} {'T':>5} {'from T':>6} {'E0m':>5} {'E0':>7} "
                      f"{'delay':>6} {'E1m':>5} {'E1':>8} {'width-frac':>10} {'black L R T B':>14}  instruments")


def format_acquisition(row):
    black = row["black"]
    instruments = row["instruments"]
    return (f"{row['source']:24} {row['output']:11} {row['T']:5} "
            f"{'-' if row['from_T'] is None else row['from_T']:>6} {row['E0m']:5} "
            f"{number(row['E0'], 7, 1):>7} {number(row['delay'], 6, 1):>6} {row['E1m']:5} "
            f"{number(row['E1'], 8, 1):>8} {number(row['width_off'], 10, 1):>10} "
            f"{black['left']:4}{black['right']:4}{black['top']:4}{black['bottom']:4}  "
            f"{instruments.get('E0')}/{instruments.get('E1')}")


def describe_fit(name, got):
    if got is None:
        return f"    {name:18} too few distinct points"
    return (f"    {name:18} slope {got['slope']:+.4f}  intercept {got['intercept']:+.2f}  "
            f"worst {got['worst']:.2f}  over {got['points']}")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+")
    parser.add_argument("--markdown", action="store_true")
    parser.add_argument("--acquisitions", action="store_true")
    parser.add_argument("--sink", action="store_true")
    args = parser.parse_args()
    records = load_runs(args.paths)

    if args.sink:
        rows = sink_rows(records)
        print(SINK_HEADER)
        for row in rows:
            print(format_sink_row(row))
        print("\nby raster: output, T, Hz -> states, own position of them, E0 mean, spread")
        for group in raster_groups(rows):
            print(f"  {group['output']:6} {group['T']:5} {group['rate']:6.1f} -> {group['n']:2} states, "
                  f"{group['own']:2} own, E0 {group['mean']:7.1f} spread {group['spread']:4.1f} "
                  f"sink px {group['sink_px']:6.1f}   " + ", ".join(group["sources"]))
        print_fits(sink_fits(records))
        return 0

    if args.acquisitions:
        print(ACQUISITION_HEADER)
        for row in acquisitions(records):
            print(format_acquisition(row))
        return 0

    if args.markdown:
        print("| mode | output | T | Hz | black L/R/T/B | L: dEnc dModel dPlace dCap | "
              "R: dEnc dModel dPlace dCap | expected L/R | verdict |")
        print("|---|---|---|---|---|---|---|---|---|")
        for record in records:
            if "skipped" in record:
                print(f"| {record['source']} | {shown_output(record)} | | | | | | | SKIP {record['skipped']} |")
                continue
            black = record["black_cols"]
            h = record["residuals"]["h"]
            rate = record["geometry"]["lineRateHz"] / (record["registers"]["STATUS_SYNC_PROC_VTOTAL"] + 1)
            expected = expected_units(record)
            exp = f"{expected['left']:+.1f}/{expected['right']:+.1f}" if expected else "-"
            print(f"| {record['source']} | {shown_output(record)} | {record['window']['T']} | {rate:.2f} | "
                  f"{black['left']}/{black['right']}/{black['top']}/{black['bottom']} | "
                  f"{terms(h['near'])} | {terms(h['far'])} | {exp} | {record['verdict']} |")
    else:
        print(HEADER)
        for record in records:
            print(format_row(record))

    print_fits(window_fits(records))
    return 0


def print_fits(fits):
    if not fits:
        return
    print("\nthe encoder's window, measured, against the model's terms")
    for output, got in fits.items():
        print(f"  {output}: {got['points']} state(s); delay after sync+porch "
              + ", ".join(f"{d:+.1f}" for d in got["delays"])
              + "; width against the fraction "
              + ", ".join(f"{w:+.1f}" for w in got["widths_off"]))
        print(describe_fit("start vs T", got["start_vs_total"]))
        print(describe_fit("start vs clock", got["start_vs_clock"]))
        print(describe_fit("width vs fraction", got["width_vs_fraction"]))


if __name__ == "__main__":
    sys.exit(main())
