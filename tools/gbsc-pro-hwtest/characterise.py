#!/usr/bin/env python3
"""Record one observation of a picture fault beside the whole register state.

    # Sweep HSCALE on the OSD and photograph each boundary.
    python3 characterise.py --host <ip> --label "onset-1" --verdict glitch \\
        --note "every second line, left third" --photo IMG_4021.jpeg

    python3 characterise.py --table          # everything recorded so far

**An OSD sweep is not a single-variable sweep, and that is the whole reason
this exists.** A pad press recomputes every window, pan included, so between two
observations VDS_DIS_HB_ST/SP, VDS_HB_SP and the capture have all moved along
with VDS_HSCALE. "The glitch starts at HSCALE 795" is therefore not a finding:
it is a finding about whichever of those tracks the fault, and the only way to
tell afterwards is to have recorded all of them at the time.

The display window is recorded at every point for the same reason. A reading
taken through an edge nobody could see can at least be identified as one later,
which is what the retracted headroom rule needed and did not have.

**THE VERDICT COMES FROM THE SCREEN.** Nothing here scores a picture: these
faults are things you look at, and a checker that guessed would be inventing the
data the exercise exists to collect.
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_INDEX = os.path.join(HERE, "snapshots", "hscale-characterisation.jsonl")

# Everything that moves when the OSD changes the scale, plus the rasters they
# are positions in. Recorded together because which one tracks the fault is the
# question, not an assumption.
#
# **NAMES ONLY.** The segment, register, offset and width come from
# tv5725_registers.json, the same map inrange.py resolves against. Hand-typed
# instead, nearly half of them come out wrong and fill the table with plausible
# numbers read from the wrong registers.
FIELDS = [
    # The variable being swept, and its vertical twin -- they share s3 0x17, so
    # only a field-level read separates them.
    "VDS_HSCALE", "VDS_VSCALE",
    # The capture. Moves with every press.
    "IF_HB_SP2", "IF_HB_ST2", "IF_VB_SP", "IF_VB_ST",
    # The memory window.
    "VDS_HB_SP", "VDS_HB_ST", "VDS_VB_SP", "VDS_VB_ST",
    # The display aperture. The retracted headroom rule died here: a picture
    # wider than this window is measured through an edge nobody can see.
    "VDS_DIS_HB_SP", "VDS_DIS_HB_ST", "VDS_DIS_VB_SP", "VDS_DIS_VB_ST",
    # The rasters, so every position above can be judged against its own.
    "IF_HSYNC_RST", "VDS_HSYNC_RST", "VDS_VSYNC_RST", "PLLAD_MD",
    # The OUTPUT hsync pulse: a third quantity that flips the fault. The same
    # pulse clears left-hand corruption that survives everything else, moved from
    # 10 to 62..77.
    # docs/investigations/hscale-tearing-characterisation.md
    "VDS_HS_ST", "VDS_HS_SP",
    # The line double and memory FIFO parity controls. Present because the fault
    # under investigation alternates line to line, and these are the only
    # registers that decide which line is which.
    "IF_LD_RAM_BYPS", "IF_LD_SEL_PROV", "IF_PRGRSV_CNTRL",
    "WFF_LINE_FLIP", "RFF_LINE_FLIP", "RFF_ADR_ADD_2", "MADPT_PD_RAM_BYPS",
    # Per-line phase. The pattern is STATIC -- whatever drifts resets every frame
    # and accumulates every line -- so a register setting a per-line pixel offset
    # is a direct candidate. IF_INI_ST is the only one RD-5725-1.1 describes that
    # way, and IF_LD_ST is the line double's write reset position.
    "IF_INI_ST", "IF_LD_ST", "MADPT_SEL_PHASE_INI",
    # The memory bus arbitration, which is the fault. PB_FETCH_NUM 256 -> 204
    # clears it: it is the playback burst length, so it sets the read request
    # rate, and the interference is the beat between those and the capture write
    # requests. **RECORD THEM.** While a fix is being refined they get tuned
    # BETWEEN presses, and rows then differ in something the row does not
    # carry.
    "PB_FETCH_NUM", "PB_CAP_OFFSET", "PB_REQ_SEL",
    "CAP_FF_HALF_REQ", "CAP_REQ_OVER",
    # Source truth. STATUS_IF_INP_INT is the chip's own interlace indicator, and
    # nothing in the firmware reads it -- interlace handling is decided from
    # gbs-control's own classification instead.
    "STATUS_IF_INP_INT", "STATUS_SYNC_PROC_VTOTAL", "STATUS_SYNC_PROC_HLOW_LEN",
]

# One garbled INTERVAL is the unit of observation, not one bad scale: a sweep
# runs until the tearing completely stops, and near-misses along the way do not
# end it. `almost` records them inside the interval, because they may be the band
# structure the headroom note saw and losing them means sweeping again.
# docs/investigations/hscale-tearing-characterisation.md
VERDICTS = ("clean", "onset", "almost", "peak", "end", "glitch")


def observe(read, label, verdict, note="", photo=""):
    """One row: the verdict from the screen, and every register that co-varies.

    `read` takes a field name and returns its value, or None if it could not be
    read. A dropped read is recorded as None rather than skipped -- a hole in a
    row is a fact about the row.
    """
    return {
        "label": label,
        "verdict": verdict,
        "note": note,
        "photo": photo,
        "registers": {name: read(name) for name in FIELDS},
    }


def picture_wider_than_display(row):
    """Whether the picture overruns the aperture it is judged through.

    True means the observation was made through an edge that could not be seen,
    which is what makes a reading worthless rather than merely noisy.
    """
    r = row.get("registers", {})
    capture = r.get("IF_HB_ST2"), r.get("IF_HB_SP2")
    scale = r.get("VDS_HSCALE")
    window = r.get("VDS_DIS_HB_ST"), r.get("VDS_DIS_HB_SP")
    if None in capture or None in window or not scale:
        return None
    produced = (capture[0] - capture[1]) * 1024.0 / scale
    return produced > (window[0] - window[1])


def load(path):
    if not os.path.exists(path):
        return []
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def append(path, row):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a") as f:
        f.write(json.dumps(row) + "\n")


def table(rows, columns=None):
    if not rows:
        return "  nothing recorded yet"
    columns = columns or ["VDS_HSCALE", "IF_HB_SP2", "IF_HB_ST2",
                          "VDS_DIS_HB_SP", "VDS_DIS_HB_ST", "VDS_HB_SP"]
    head = f"  {'label':<14} {'verdict':<8} " + " ".join(f"{c:>14}" for c in columns) + "  hidden  note"
    out = [head, "  " + "-" * (len(head) - 2)]
    for row in rows:
        r = row.get("registers", {})
        cells = " ".join(f"{str(r.get(c, '-')):>14}" for c in columns)
        hidden = picture_wider_than_display(row)
        flag = "?" if hidden is None else ("YES" if hidden else "no")
        out.append(f"  {row['label']:<14} {row['verdict']:<8} {cells}  {flag:>6}  {row.get('note', '')}")
    return "\n".join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host")
    parser.add_argument("--label")
    parser.add_argument("--verdict", choices=VERDICTS)
    parser.add_argument("--note", default="")
    parser.add_argument("--photo", default="")
    parser.add_argument("--index", default=DEFAULT_INDEX)
    parser.add_argument("--table", action="store_true")
    args = parser.parse_args(argv)

    if args.table:
        print()
        print(table(load(args.index)))
        print()
        return 0

    if not (args.host and args.label and args.verdict):
        parser.error("--host, --label and --verdict are required to record")

    sys.path.insert(0, HERE)
    import bench_probe
    from gbs_unit import read_reg

    with open(os.path.join(HERE, "tv5725_registers.json")) as f:
        regs = json.load(f)
    probe = bench_probe.Probe(lambda s, r: read_reg(args.host, s, r),
                              lambda s, r, v: None)

    def read(name):
        entry = regs.get(name)
        if entry is None:
            return None
        return probe.read_field((name, entry["seg"], entry["reg"],
                                 entry["off"], entry["width"]))

    row = observe(read, args.label, args.verdict, args.note, args.photo)
    append(args.index, row)

    hidden = picture_wider_than_display(row)
    print()
    print(table([row]))
    if hidden:
        print("\n  WARNING: the picture is wider than the display window, so part"
              "\n  of it is off the aperture and this reading is through an edge"
              "\n  you cannot see. That is what retracted the headroom rule.")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
