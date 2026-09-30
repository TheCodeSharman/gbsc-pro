#!/usr/bin/env python3
"""Sweep source modes and output resolutions, and attribute the black at every
edge of the emitted frame to the stage that owns it.

    python3 tools/gbsc-pro-hwtest/framing_sweep.py --host 192.168.88.108 --mdf <file> --tier A
    ... --modes "X640 Y480 C256 F60,X800 Y600 C256 F60" --outputs 1080p,720p
    ... --probes none|pan|full     (a tier names its own)

One JSONL record per (source mode, output) in --out, a PNG of each judged clip
beside it, and framing_report.py turns a run into the table. Per state:

    MODE; acquire and settle; PATTERN CARD, ANIM OFF; reset the framing
    (/sc?B -- a stored entry is forgotten, by design); freeze; MODE again, the
    re-lock, since the shown position is latched at link acquisition; wait for
    the dongle; registers in one pass, the clip, registers again -- a record
    whose two reads differ is refused.

    pan   the capture panned PAN_UNITS: black that moves with it is captured
          source blanking, black that stays is the output's.
    full  a zoomed framing so content reaches every edge, then each aperture
          register walked INTO the picture with automation frozen; the
          differenced strip and the black count are both recorded per step
          and each is fitted for where the encoder's window really is.

--mdf is the monitor definition the source is running, checked against MODES;
each mode's EXPECTED placement -- the SourceTiming row its key lands on,
against the mode's own picture area -- comes from it, so dCapture is judged
against the design rather than against zero.

framing_decomposition.py is the arithmetic; docs/investigations/
full-screen-framing-on-the-vesa-modes.md is the goal.
"""

import argparse
import datetime
import json
import os
import re
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import framing_decomposition as fd
import full_margins
import gbs_unit
import hdmi_capture
import mdf_modes
import picture_jitter
import published_rasters
import setfield
import transmitted_window

HERE = os.path.dirname(os.path.abspath(__file__))
MODESERV = "192.168.88.10"
OUTPUTS = tuple(gbs_unit.OUTPUT_COMMANDS)
PROBE_LEVELS = ("none", "pan", "full")

TIERS = {
    "A": dict(modes=["X640 Y480 C256 F60", "X800 Y600 C256 F60"],
              outputs=["1080p", "720p", "960p", "1024p"], probes="full"),
    "B": dict(modes=["X640 Y480 C256 F60", "X640 Y480 C256 F73", "X640 Y480 C256 F75",
                     "X800 Y600 C256 F56", "X800 Y600 C256 F60",
                     "X320 Y256 C256 F50", "X640 Y512 C256 F50", "X640 Y200 C256 F60"],
              outputs=["1080p", "720p"], probes="full"),
    "C": dict(modes=None, outputs=["1080p"], probes="none"),
}

FIELDS = full_margins.FIELDS + ["VDS_HB_SP", "VDS_HB_ST", "VDS_VB_SP", "VDS_VB_ST",
                                "VDS_HS_ST", "VDS_HS_SP", "VDS_VS_ST", "VDS_VS_SP",
                                "PLLAD_KS", "IF_LINE_ST", "IF_LINE_SP",
                                "PB_FETCH_NUM", "PB_CAP_OFFSET"]

CLIP_FRAMES = 8
DONGLE_SETTLE_S = 10.0
PAN_UNITS = 8
ZOOM_EXTENT, ZOOM_ORIGIN = 0.30, 0.35
WALK_STEP, WALK_POINTS, WALK_WARMUP, WALK_FRAMES = 20, 12, 10, 3
# card_edges.FLUSH at a near edge, plus PARITY at the far one.
PROFILE_DEPTH = 96


# --- the mode file ---------------------------------------------------------------

def mode_key(entry):
    key = published_rasters.key_of(entry)
    return f"X{entry['x_res']} Y{entry['y_res']} C256 F{round(key['rate'])}"


def verify_mdf(listed, entries):
    """The modes MODES lists that the file does not state -- a file the machine
    is not running, or not this one."""
    offered = set()
    for entry in entries:
        try:
            offered.add(mode_key(entry))
        except (KeyError, ValueError):
            continue
    return [mode for mode in listed if mode not in offered]


def mdf_entry_for(entries, mode):
    """The entry MODE picks for this request: X and Y exactly, F nearest."""
    want = dict(re.findall(r"([XYF])(\d+)", mode))
    best = None
    for entry in entries:
        if entry.get("x_res") != want["X"] or entry.get("y_res") != want["Y"]:
            continue
        try:
            off = abs(published_rasters.key_of(entry)["rate"] - float(want["F"]))
        except (KeyError, ValueError):
            continue
        if best is None or off < best[0]:
            best = (off, entry)
    return best[1] if best else None


def expected_for(entry, rows):
    """Where the design puts this mode's capture, and what it costs the mode."""
    if entry is None:
        return None
    key = published_rasters.key_of(entry)
    row = published_rasters.match(rows, key)
    return dict(key=key, answered_by=(f"{row['authority']} {row['name']}" if row else "envelope"),
                offsets_px=published_rasters.expected_offsets(entry, row))


# --- the analysis -----------------------------------------------------------------

def field_rate_of(regs, geometry):
    return geometry["lineRateHz"] / (regs["STATUS_SYNC_PROC_VTOTAL"] + 1)


def analyse_default(output, regs, geometry, clip, window_measured=None):
    """Every position and residual one default-framing clip supports.

    `window_measured` carries E0/E1/V0/V1 from the walks where they ran; an
    edge without a measurement is judged against the model's.
    """
    pos = fd.positions(regs, geometry)
    total = pos["h"]["T"]
    window = fd.predicted_window(output, total, field_rate_of(regs, geometry))
    window.update({k: None for k in ("E0", "E1", "V0", "V1")})
    window.update(window_measured or {})
    cols, rows = fd.slope_predicted(output, total)

    grey = hdmi_capture.luma(clip[0])
    profiles = fd.edge_profiles(grey, PROFILE_DEPTH)
    black = {edge: fd.black_extent(profile) for edge, profile in profiles.items()}

    e0 = window["E0"] if window["E0"] is not None else window["E0m"]
    v0 = window["V0"] if window["V0"] is not None else window["V0m"]
    h_near, h_far, h_off = fd.card_columns(clip, 1, (pos["h"]["P1"] - pos["h"]["P0"]) * cols)
    v_near, v_far, v_off = fd.card_columns(clip, 0, (pos["v"]["P1"] - pos["v"]["P0"]) * rows)
    pos["h"].update(C0=None if h_near is None else fd.columns_to_units(h_near, e0, cols),
                    C1=None if h_far is None else fd.columns_to_units(h_far, e0, cols))
    pos["v"].update(C0=None if v_near is None else fd.columns_to_units(v_near, v0, rows),
                    C1=None if v_far is None else fd.columns_to_units(v_far, v0, rows))

    slope = dict(h_pred=cols, v_pred=rows)
    residuals, verdict = fd.judge(pos, window, slope, black)
    return dict(window=window, slope=slope, positions=pos,
                card=dict(h=(h_near, h_far, h_off), v=(v_near, v_far, v_off)),
                black_cols=black, profiles={k: [float(x) for x in v] for k, v in profiles.items()},
                residuals=residuals, verdict=verdict)


# --- the bench --------------------------------------------------------------------

def column_profile(dev, frames=WALK_FRAMES, warmup=WALK_WARMUP):
    return hdmi_capture.luma(hdmi_capture.frames(frames, dev, warmup=warmup)[-1])


def judged_clip(dev):
    time.sleep(DONGLE_SETTLE_S)
    return hdmi_capture.frames(CLIP_FRAMES, dev)


def relock(where, host, mode):
    """The valid re-lock: the source leaves and returns with our registers as
    they stand, and the engine settles again."""
    return gbs_unit.mode_round_trip(where, host, mode)


def pan_probe(host, where, dev, mode, reference, magnification, slope):
    """Pan the capture PAN_UNITS and ask which columns moved with it."""
    gbs_unit.freeze(host, False)
    before = gbs_unit.get_json(host, "/geometry")[1]
    after = gbs_unit.framing_by(host, "oh", PAN_UNITS)
    if not after or not before:
        return None
    moved_units = after["oh"] - before["oh"]
    gbs_unit.freeze(host, True)
    if not relock(where, host, mode):
        return None
    current = hdmi_capture.luma(judged_clip(dev)[0]).mean(axis=0)
    maxlag = int(abs(moved_units) * magnification * slope) + 12
    regions = dict(picture=slice(300, 1620), left_strip=slice(0, 48), right_strip=slice(-48, None))
    shifts = {}
    for name, region in regions.items():
        shift, bracketed = picture_jitter.displacement(reference[region], current[region], maxlag)
        shifts[name] = dict(cols=shift, bracketed=bracketed)
    return dict(asked_units=PAN_UNITS, moved_units=moved_units, shifts=shifts,
                slope_cols_per_unit=(abs(shifts["picture"]["cols"]) / (abs(moved_units) * magnification)
                                     if moved_units else None))


def zoom_in(host, geometry):
    """A framing with content at every edge: the middle of the line and frame.
    Reports what was asked and where the pads landed it, since a solve clamps
    what it is given."""
    gbs_unit.freeze(host, False)
    asked = {}
    for extent, origin, units in (("eh", "oh", geometry["ch"]), ("ev", "ov", geometry["cv"])):
        asked[extent] = int(ZOOM_EXTENT * units)
        asked[origin] = int(ZOOM_ORIGIN * units)
        gbs_unit.framing_to(host, extent, asked[extent])
        gbs_unit.framing_to(host, origin, asked[origin])
    gbs_unit.freeze(host, True)
    return dict(asked=asked, landed=gbs_unit.get_json(host, "/geometry")[1])


def walk_edge(host, dev, spec_map, field, first, near, vertical, expected_per_unit):
    """Walk one aperture register INTO the picture and record, per step, where
    the differenced strip says the blanking reaches and how many positions at
    the edge read black. Two instruments on the same frames."""
    setfield.apply(host, field, spec_map[field], first, False)
    time.sleep(0.5)
    axis = 1 if vertical else 0
    previous = column_profile(dev).mean(axis=axis)
    taken = first
    strips, blacks = [], []
    try:
        for i in range(1, WALK_POINTS):
            value = first + i * WALK_STEP if near else first - i * WALK_STEP
            setfield.apply(host, field, spec_map[field], value, False)
            time.sleep(0.5)
            current = column_profile(dev).mean(axis=axis)
            at = transmitted_window.blanking_edge(current, previous, near,
                                                  expected_per_unit * abs(value - taken))
            blacks.append((value, fd.black_extent(current if near else current[::-1])))
            if at is not None:
                strips.append((value, at))
                previous, taken = current, value
    finally:
        setfield.apply(host, field, spec_map[field], first, False)
    strip_zero, strip_slope, used, worst = transmitted_window.crossing(strips)
    black_zero, black_slope, _, _ = transmitted_window.crossing(blacks) if len(blacks) >= 3 else (None, None, 0, 0.0)
    return dict(field=field, first=first, strips=strips, blacks=blacks,
                strip_zero=strip_zero, strip_slope=strip_slope, used=used, worst=worst,
                black_zero=black_zero, black_slope=black_slope,
                signature=fd.walk_signature(blacks[:4]))


def walks(host, dev, regs, slope):
    spec_map = setfield.load_map()
    cols, rows = slope
    return dict(
        h_near=walk_edge(host, dev, spec_map, "VDS_DIS_HB_SP", regs["VDS_DIS_HB_SP"], True, False, cols),
        h_far=walk_edge(host, dev, spec_map, "VDS_DIS_HB_ST", regs["VDS_DIS_HB_ST"], False, False, cols),
        v_near=walk_edge(host, dev, spec_map, "VDS_DIS_VB_SP", regs["VDS_DIS_VB_SP"], True, True, rows),
        v_far=walk_edge(host, dev, spec_map, "VDS_DIS_VB_ST", regs["VDS_DIS_VB_ST"], False, True, rows))


def measure_state(ctx, mode, output):
    host, where, dev = ctx["host"], ctx["modeserv"], ctx["dev"]
    record = dict(at=datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
                  host=host, mdf=ctx["mdf"], source=mode, output=output,
                  letter=gbs_unit.OUTPUT_COMMANDS[output], probes=ctx["probes"],
                  expected=expected_for(mdf_entry_for(ctx["entries"], mode), ctx["rows"]))

    def refused(why):
        record["skipped"] = why
        return record

    if not gbs_unit.modeserv_ok(gbs_unit.mode_serv(where, f"MODE {mode}")):
        return refused("source refused the mode")
    if not gbs_unit.acquired_and_settled(host):
        return refused("never acquired, or never stopped re-solving")
    gbs_unit.show_card(where)
    if gbs_unit.reset_framing(host) is None:
        return refused("the framing never reset")
    if not gbs_unit.freeze(host, True):
        return refused("automation would not freeze")
    try:
        if not relock(where, host, mode):
            return refused("the re-lock never settled")
        regs = gbs_unit.read_fields(host, FIELDS)
        geometry = gbs_unit.get_json(host, "/geometry")[1]
        clip = judged_clip(dev)
        regs_after = gbs_unit.read_fields(host, FIELDS)
        moved = moved_fields(regs, regs_after)
        if moved:
            return refused(f"registers moved during the clip: {', '.join(moved)}")
        record.update(registers=regs, geometry=geometry,
                      tier_answered=(published_rasters.tier_answered(ctx["rows"], geometry) or {}).get("name"))
        reference = hdmi_capture.luma(clip[0]).mean(axis=0)
        analysis = analyse_default(output, regs, geometry, clip)
        record["clips"] = dict(default=save_png(ctx, mode, output, "default", clip[0]))

        if ctx["probes"] in ("pan", "full"):
            record["pan"] = pan_probe(host, where, dev, mode, reference,
                                      analysis["positions"]["h"]["m"], analysis["slope"]["h_pred"])
            gbs_unit.freeze(host, False)
            gbs_unit.reset_framing(host)
            gbs_unit.freeze(host, True)
        if ctx["probes"] == "full":
            record["zoom"] = zoom_in(host, geometry)
            if relock(where, host, mode):
                zoomed = judged_clip(dev)
                record["clips"]["zoom"] = save_png(ctx, mode, output, "zoom", zoomed[0])
                record["walk"] = walks(host, dev, regs, (analysis["slope"]["h_pred"],
                                                         analysis["slope"]["v_pred"]))
                window, instruments = fd.measured_window(record["walk"])
                analysis = analyse_default(output, regs, geometry, clip, window)
                analysis["window"]["instruments"] = instruments
                analysis["slope"].update(h_near=record["walk"]["h_near"]["strip_slope"],
                                         h_far=record["walk"]["h_far"]["strip_slope"],
                                         v_near=record["walk"]["v_near"]["strip_slope"],
                                         v_far=record["walk"]["v_far"]["strip_slope"])
        record.update(analysis)
    finally:
        gbs_unit.freeze(host, False)
        gbs_unit.reset_framing(host)
    return record


def moved_fields(before, after):
    """The solved registers that read differently either side of the clip.
    The sync processor's status counters are left out: they read a unit either
    way on a healthy unit, and they measure the source rather than state the
    clip was taken in."""
    return [name for name in before
            if not name.startswith("STATUS_") and before[name] != after.get(name)]


def save_png(ctx, mode, output, which, frame):
    slug = re.sub(r"[^A-Za-z0-9]+", "-", mode).strip("-")
    stem = f"{slug}-{output}-{which}"
    taken = ctx.setdefault("frames_taken", {})
    taken[stem] = taken.get(stem, 0) + 1
    name = stem if taken[stem] == 1 else f"{stem}-{taken[stem]}"
    path = os.path.join(ctx["png_dir"], f"{name}.png")
    hdmi_capture.write_png(path, frame)
    return os.path.relpath(path, HERE)


def jsonable(value):
    if isinstance(value, (np.floating, np.integer)):
        return value.item()
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, slice):
        return str(value)
    raise TypeError(type(value).__name__)


def summary_line(record):
    if "skipped" in record:
        return f"  {record['source']:24} {record['output']:6} SKIP  {record['skipped']}"
    h, v = record["residuals"]["h"], record["residuals"]["v"]
    black = record["black_cols"]
    return (f"  {record['source']:24} {record['output']:6} T {record['window']['T']:5}"
            f"  black L{black['left']:3} R{black['right']:3} T{black['top']:3} B{black['bottom']:3}"
            f"  answered {record.get('tier_answered')}"
            f"  {record['verdict']}"
            f"   near {h['near']['verdict']}/{v['near']['verdict']} far {h['far']['verdict']}/{v['far']['verdict']}")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv", default=MODESERV)
    parser.add_argument("--mdf", required=True, help="the monitor definition the source is running")
    parser.add_argument("--tier", choices=sorted(TIERS))
    parser.add_argument("--modes", default=None, help="comma separated; default is the tier's")
    parser.add_argument("--outputs", default=None, help="comma separated; default is the tier's")
    parser.add_argument("--probes", choices=PROBE_LEVELS, default=None)
    parser.add_argument("--out", default=os.path.join(HERE, "sweeps"))
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--force-mdf", action="store_true",
                        help="run although MODES lists modes the file lacks")
    args = parser.parse_args()

    tier = TIERS.get(args.tier, dict(modes=None, outputs=["1080p"], probes="none"))
    outputs = [o.strip() for o in args.outputs.split(",")] if args.outputs else tier["outputs"]
    unknown = [o for o in outputs if o not in OUTPUTS]
    if unknown:
        sys.exit(f"unknown output(s): {', '.join(unknown)}")
    probes = args.probes or tier["probes"]

    listed = gbs_unit.list_modes(args.modeserv)
    if not listed:
        sys.exit("MODES answered nothing -- is ModeServ running?")
    entries = mdf_modes.parse(args.mdf)
    missing = verify_mdf(listed, entries)
    if missing and not args.force_mdf:
        sys.exit(f"the source lists {len(missing)} mode(s) {args.mdf} does not state -- it is "
                 f"not the file the machine is running: {', '.join(missing[:5])}")
    modes = ([m.strip() for m in args.modes.split(",") if m.strip()] if args.modes
             else (tier["modes"] or listed))

    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    os.makedirs(args.out, exist_ok=True)
    png_dir = os.path.join(HERE, "snapshots", "framing", stamp)
    os.makedirs(png_dir, exist_ok=True)
    path = os.path.join(args.out, f"framing-{stamp}.jsonl")
    ctx = dict(host=args.host, modeserv=args.modeserv, dev=hdmi_capture.device(), mdf=args.mdf,
               entries=entries, rows=published_rasters.rows(), probes=probes, png_dir=png_dir)

    print(f"{len(modes)} mode(s) x {len(outputs)} output(s), probes {probes}, "
          f"records -> {path}, frames -> {png_dir}\n")
    gbs_unit.autosave(args.host, False)
    try:
        with open(path, "a") as out:
            for output in outputs:
                if not gbs_unit.choose_output(args.host, output):
                    print(f"  {output}: the raster never held after /uc -- skipped")
                    continue
                for _ in range(args.repeat):
                    for mode in modes:
                        record = measure_state(ctx, mode, output)
                        out.write(json.dumps(record, default=jsonable) + "\n")
                        out.flush()
                        print(summary_line(record), flush=True)
    finally:
        gbs_unit.autosave(args.host, True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
