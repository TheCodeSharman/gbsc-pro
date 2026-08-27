#!/usr/bin/env python3
"""A manual control panel for the TV5725's geometry registers.

    python3 tools/gbsc-pro-hwtest/regpanel.py --host 192.168.88.108
    then open http://localhost:8752

Why this exists: tuning the scaler is an eye-in-the-loop job. The picture is the
only measurement of whether a geometry register is right, so a value computed and
written blind has nothing to check itself against.

This serves a page and proxies register access to the unit, so it needs no
firmware change and nothing to flash. The unit's own /getreg and /setreg do the
work; the browser talks to this process to avoid cross-origin restrictions.

Fields are addressed as (segment, register, bit offset, width) exactly as the
firmware's Tv5725.h declares them, and read/written through one generic routine.
That matters: several TV5725 fields start partway through a byte and run into the
next one — VDS_VSYNC_RST begins at bit 4 of S3_02 — so the masking belongs in one
place rather than at every call site.

Nothing written here persists: these are TV5725 registers, and a mode change or
reset reloads the preset over them.
"""

import argparse
import json
import os
import pathlib
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

import geometry_math
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gbs_unit

HOST = None

# (label, segment, register, bit offset, width, note). Order is display order.
# These are the ones worth having to hand; build_groups() appends a section for
# every remaining register so nothing is reachable by search alone.
CURATED = [
    ("Frame buffer — playback", [
        ("PB_CAP_OFFSET", 4, 0x37, 0, 10, "line stride, in 64-bit memory words — shared by capture and playback"),
        ("PB_FETCH_NUM", 4, 0x39, 0, 10, "words read per line"),
        ("PB_ENABLE", 4, 0x2B, 7, 1, "playback on"),
        ("CAPTURE_ENABLE", 4, 0x21, 0, 1, "capture on"),
        ("CAP_ADR_ADD_2", 4, 0x21, 7, 1, "0 = one 32-bit word per pixel"),
        ("PB_CAP_BUF_STA_ADDR_A", 4, 0x31, 0, 21, "capture buffer base, 32-bit word address"),
        ("CAP_SAFE_GUARD_A", 4, 0x24, 0, 21, "top of the capture region"),
    ]),
    ("Input — capture window", [
        ("IF_HB_SP2", 1, 0x1A, 0, 11, "left edge of captured video"),
        ("IF_HB_ST2", 1, 0x18, 0, 11, "right edge of captured video"),
        ("IF_VB_SP", 1, 0x1E, 0, 11, "top edge"),
        ("IF_VB_ST", 1, 0x1C, 0, 11, "bottom edge"),
        ("IF_HSYNC_RST", 1, 0x0E, 0, 11, "input line length — capture must stay inside this"),
        ("IF_LINE_ST", 1, 0x20, 0, 12, "line buffer start"),
        ("IF_LINE_SP", 1, 0x22, 0, 12, "line buffer stop"),
    ]),
    ("Output — raster", [
        ("VDS_HSYNC_RST", 3, 0x01, 0, 12, "output line length − 1"),
        ("VDS_VSYNC_RST", 3, 0x02, 4, 11, "output frame length − 1"),
        ("VDS_HS_ST", 3, 0x0A, 0, 12, "hsync pulse start"),
        ("VDS_HS_SP", 3, 0x0B, 4, 12, "hsync pulse stop"),
        ("VDS_VS_ST", 3, 0x0D, 0, 11, "vsync pulse start"),
        ("VDS_VS_SP", 3, 0x0E, 4, 11, "vsync pulse stop"),
    ]),
    ("Output — display and blanking windows", [
        ("VDS_DIS_HB_SP", 3, 0x11, 4, 12, "display window left"),
        ("VDS_DIS_HB_ST", 3, 0x10, 0, 12, "display window right"),
        ("VDS_DIS_VB_SP", 3, 0x14, 4, 11, "display window top"),
        ("VDS_DIS_VB_ST", 3, 0x13, 0, 11, "display window bottom"),
        ("VDS_HB_SP", 3, 0x05, 4, 12, "blanking window left"),
        ("VDS_HB_ST", 3, 0x04, 0, 12, "blanking window right"),
        ("VDS_VB_SP", 3, 0x08, 4, 11, "blanking window top"),
        ("VDS_VB_ST", 3, 0x07, 0, 11, "blanking window bottom"),
    ]),
    ("Scalers", [
        ("VDS_HSCALE", 3, 0x16, 0, 10, "lower = wider picture. degrades progressively when pushed"),
        ("VDS_VSCALE", 3, 0x17, 4, 10, "lower = taller picture. 1023 = bypass"),
        ("VDS_HSCALE_BYPS", 3, 0x00, 4, 1, "1 = horizontal scaler off"),
        ("VDS_VSCALE_BYPS", 3, 0x00, 5, 1, "1 = vertical scaler off"),
    ]),
    ("Sync processor", [
        ("SP_RT_HS_ST", 5, 0x49, 0, 12, "retimed hsync start"),
        ("SP_RT_HS_SP", 5, 0x4B, 0, 12, "retimed hsync stop — firmware sets this to 0.93 x PLLAD_MD"),
        ("SP_CS_CLP_ST", 5, 0x41, 0, 12, "clamp start"),
        ("SP_CS_CLP_SP", 5, 0x43, 0, 12, "clamp stop"),
        ("SP_H_CST_ST", 5, 0x4D, 0, 12, "coast start"),
        ("SP_H_CST_SP", 5, 0x4F, 0, 12, "coast stop"),
    ]),
    ("Sample clock and phase", [
        ("PLLAD_MD", 5, 0x12, 0, 12, "samples per input line. IF_HSYNC_RST etc. derive from it"),
        ("PLLAD_KS", 5, 0x16, 4, 2, "VCO band — a large PLLAD_MD change may need this moved"),
        ("PLLAD_FS", 5, 0x11, 5, 1, "VCO gain select"),
        ("PA_ADC_S", 5, 0x18, 1, 5, "ADC sampling phase, 32 positions"),
        ("PA_SP_S", 5, 0x19, 1, 5, "sync processor phase, 32 positions"),
    ]),
]


STATUS = [
    ("VTOTAL", 0, 0x1B, 0, 11),
    ("HTOTAL", 0, 0x17, 0, 12),
    ("HPERIOD_IF", 0, 0x06, 0, 9),
    ("VPERIOD_IF", 0, 0x07, 1, 11),
    ("HLOW_LEN", 0, 0x19, 0, 12),
    ("STATUS_16", 0, 0x16, 0, 8),
    ("PLLAD_LOCK", 0, 0x09, 7, 1),
    ("preset ID", 1, 0x2B, 0, 7),
]

# What the source measurement registers are worth is unsettled — see
# docs/tv5725-chip.md. The datasheet says HPERIOD_IF is the source's H total in
# pixels divided by four, which would be the one quantity every geometry
# calculation needs and which nothing in the firmware uses for geometry. Readings
# so far do not fit that reading consistently, so this reports the raw value and
# both interpretations side by side rather than picking one.
MEASURED = [
    ("HPERIOD_IF", 0, 0x06, 0, 9, "source H total — datasheet says pixels/4, 9 bits so caps at 511"),
    ("VPERIOD_IF", 0, 0x07, 1, 11, "source V total in lines, per the datasheet"),
    ("HLOW_LEN", 0, 0x19, 0, 12, "measured hsync low time, in sample clocks"),
    ("VTOTAL", 0, 0x1B, 0, 11, "sync processor V total — agrees with mode files to a line"),
    ("HTOTAL", 0, 0x17, 0, 12, "sample clocks per line — reads back PLLAD_MD, measures nothing"),
]


def load_register_doc():
    """The suite's own register map, addressing and descriptions both anchored
    to RD-5725-1.1 via datasheet_fields.json. Optional — the panel works without
    it, just without descriptions or the search list."""
    path = pathlib.Path(__file__).resolve().parent / "tv5725_registers.json"
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    # GBS_* are gbs-control's own option bits parked in chip scratch space, not
    # TV5725 registers.
    return {name: entry for name, entry in doc.items()
            if not name.startswith("GBS_")}


SUBSYSTEMS = [
    (("PB_", "CAP_", "CAPTURE_"), "Frame buffer — capture and playback"),
    (("WFF_",), "Frame buffer — write FIFO"),
    (("RFF_",), "Frame buffer — read FIFO (deinterlacer)"),
    (("MEM_", "SDRAM_"), "Memory bus and SDRAM"),
    (("IF_",), "Input formatter"),
    (("VDS_",), "Video display (VDS)"),
    (("SP_",), "Sync processor"),
    (("ADC_",), "ADC"),
    (("PLLAD_", "PLL_", "PA_", "CKT_", "CLKOUT_"), "Clocks and PLLs"),
    (("MADPT_", "MAPDT_", "DIAG_"), "Deinterlacer (MADPT)"),
    (("MD_",), "Mode detect"),
    (("STATUS_", "HPERIOD_", "VPERIOD_"), "Status (read-only)"),
    (("OSD_",), "OSD"),
    (("PIP_",), "PIP"),
    (("DAC_", "DIGOUT_", "OUT_"), "Output and DACs"),
    (("PAD_",), "Pads"),
    (("INT_", "SFTRST_"), "Interrupts and reset"),
    (("GPIO_",), "GPIO"),
    (("HD_",), "HD path"),
]
OTHER = "Test, decode and unclassified"


def _section_for(name):
    for prefixes, label in SUBSYSTEMS:
        if name.startswith(prefixes):
            return label
    return OTHER


def build_groups(curated, doc):
    """The curated sections, then one section per subsystem holding every
    register the curated ones left out. A register reachable only by typing its
    name is a register nobody finds."""
    curated_fields = {name for _, fields in curated for name, *_ in fields}
    curated_labels = {label for label, _ in curated}
    buckets = {label: [] for _, label in SUBSYSTEMS}
    buckets[OTHER] = []

    for name in sorted(doc):
        if name in curated_fields:
            continue
        entry = doc[name]
        buckets[_section_for(name)].append(
            (name, entry["seg"], entry["reg"], entry["off"], entry["width"], ""))

    return list(curated) + [
        (f"{label} — the rest" if label in curated_labels else label, fields)
        for label, fields in buckets.items() if fields]


REGISTER_DOC = load_register_doc()
ALL_FIELDS = {n: (e["seg"], e["reg"], e["off"], e["width"])
              for n, e in REGISTER_DOC.items()}
GROUPS = build_groups(CURATED, REGISTER_DOC)


def find_field(name):
    """A field by name, from the curated groups or the full header."""
    for _, entries in GROUPS:
        for fname, seg, reg, off, width, _doc in entries:
            if fname == name:
                return seg, reg, off, width
    return ALL_FIELDS.get(name)


def _get(path, timeout=5):
    try:
        with urllib.request.urlopen(f"http://{HOST}{path}", timeout=timeout) as r:
            return r.read().decode("utf-8", "replace")
    except (urllib.error.URLError, urllib.error.HTTPError, OSError):
        return None


def read_reg(segment, register):
    body = _get(f"/getreg?s={segment:x}&r={register:02x}")
    if not body:
        return None
    try:
        return int(json.loads(body)["value"], 16)
    except (ValueError, KeyError):
        return None


def write_reg(segment, register, value):
    return _get(f"/setreg?s={segment:x}&r={register:02x}&v={value:02x}") is not None


def _span(offset, width):
    """How many consecutive registers a field occupies."""
    return (offset + width + 7) // 8


def read_field(segment, register, offset, width):
    """A field of any width starting at any bit, across as many registers as it
    needs. The TV5725 spreads fields low-byte-first and some start mid-byte.

    One register per request. For more than a field or two, read_fields() does
    the same decoding off a single fetched span — see there for why that matters.
    """
    data = bytearray()
    for i in range(_span(offset, width)):
        byte = read_reg(segment, register + i)
        if byte is None:
            return None
        data.append(byte)
    return _decode(data, register, register, offset, width)


def fetch_span(segment, first, last):
    """One segment's registers, first..last inclusive, in a single request."""
    body = _get(f"/getregs?s={segment:x}&from={first:02x}&to={last:02x}")
    if not body:
        return None
    try:
        return bytes.fromhex(json.loads(body)["values"])
    except (ValueError, KeyError):
        return None


def _decode(data, base, register, offset, width):
    """A field out of a fetched span, low byte first, starting at any bit."""
    raw = 0
    for i in range(_span(offset, width)):
        raw |= data[register - base + i] << (8 * i)
    return (raw >> offset) & ((1 << width) - 1)


def read_fields(fetch, fields):
    """Every field, in one round trip per segment.

    The unit answers with `Connection: close`, so a request costs a TCP
    connection that then sits in TIME-WAIT for a minute. Reading a register at a
    time made /api/status cost 27 connections every 1500 ms and starved a device
    whose websocket server caps at five clients. Fetching each segment's touched
    span costs one — and returns a consistent set, where per-register reads gave
    fields sampled at different instants.

    `fields` entries may carry documentation after the first five elements.
    """
    heads = [f[:5] for f in fields]

    wanted = {}
    for _, segment, register, offset, width in heads:
        last = register + _span(offset, width) - 1
        have = wanted.get(segment)
        wanted[segment] = ((register, last) if have is None
                           else (min(have[0], register), max(have[1], last)))

    spans = {}
    for segment, (first, last) in sorted(wanted.items()):
        data = fetch(segment, first, last)
        if data is None:
            return None
        spans[segment] = (first, data)

    return {name: _decode(spans[segment][1], spans[segment][0],
                          register, offset, width)
            for name, segment, register, offset, width in heads}


def write_field(segment, register, offset, width, value):
    """Read-modify-write, so the bits sharing those registers are preserved."""
    span = _span(offset, width)
    raw = 0
    for i in range(span):
        byte = read_reg(segment, register + i)
        if byte is None:
            return False
        raw |= byte << (8 * i)
    mask = ((1 << width) - 1) << offset
    raw = (raw & ~mask) | ((value << offset) & mask)
    for i in range(span):
        if not write_reg(segment, register + i, (raw >> (8 * i)) & 0xFF):
            return False
    return True


# What check_invariants needs, as a table so a poll can batch it with STATUS in
# one round trip per segment instead of one per register.
INVARIANTS = [
    ("PLLAD_MD", 5, 0x12, 0, 12),
    ("IF_HSYNC_RST", 1, 0x0E, 0, 11),
    ("IF_LINE_ST", 1, 0x20, 0, 12),
    ("IF_LINE_SP", 1, 0x22, 0, 12),
    ("SP_RT_HS_SP", 5, 0x4B, 0, 12),
    ("IF_HB_SP2", 1, 0x1A, 0, 11),
    ("IF_HB_ST2", 1, 0x18, 0, 11),
]


def check_invariants(values):
    """Relationships the input side must hold, each one learned by breaking it.

    Violating any of these corrupts the picture in a way that looks like a
    hardware fault rather than an arithmetic error, which is why they are checked
    continuously rather than left to be rediscovered.
    """
    if not values:
        return []
    md = values.get("PLLAD_MD")
    rst = values.get("IF_HSYNC_RST")
    lst = values.get("IF_LINE_ST")
    lsp = values.get("IF_LINE_SP")
    rt = values.get("SP_RT_HS_SP")
    sp2 = values.get("IF_HB_SP2")
    st2 = values.get("IF_HB_ST2")
    if None in (md, rst, lst, lsp, rt, sp2, st2):
        return []

    out = []

    def add(ok, label, detail):
        out.append({"ok": bool(ok), "label": label, "detail": detail})

    add(lsp - lst == rst + 1, "line span",
        f"IF_LINE_SP-IF_LINE_ST = {lsp - lst}, wants IF_HSYNC_RST+1 = {rst + 1}")
    add(rst == md // 2, "line length",
        f"IF_HSYNC_RST = {rst}, wants PLLAD_MD/2 = {md // 2}")
    add(rt < md, "retime gate",
        f"SP_RT_HS_SP = {rt}, must stay under PLLAD_MD = {md} or the picture rolls")
    # STRICTLY inside: IF_HB_ST2 == IF_HSYNC_RST rolls the picture.
    add(0 <= sp2 < st2 < rst, "capture window",
        f"{sp2}..{st2} must sit inside 0..{rst - 1}, SP2 below ST2 and ST2 "
        f"below IF_HSYNC_RST = {rst}")
    return out


def latch_pllad():
    """The PLL keeps running on its old divider until this edge. Writing
    PLLAD_MD without it changes the register and nothing else."""
    write_field(5, 0x11, 7, 1, 0)
    time.sleep(0.01)
    write_field(5, 0x11, 7, 1, 1)


# Writing these requires the PLL latch afterwards or the write does nothing.
NEEDS_LATCH = {"PLLAD_MD", "PLLAD_KS", "PLLAD_FS"}

# Everything measured in units of the input line, so all of it scales with
# PLLAD_MD. Nothing in the firmware keeps them in step when it is written
# directly, which is what /api/pllad exists to do.
SCALES_WITH_PLLAD = [
    ("IF_HSYNC_RST", 1, 0x0E, 0, 11),
    ("IF_LINE_ST", 1, 0x20, 0, 12),
    ("IF_LINE_SP", 1, 0x22, 0, 12),
    ("IF_HB_SP2", 1, 0x1A, 0, 11),
    ("IF_HB_ST2", 1, 0x18, 0, 11),
    ("SP_RT_HS_SP", 5, 0x4B, 0, 12),
]


def set_pllad_scaled(target):
    """Move PLLAD_MD and everything measured against it, together.

    Changing the sample clock changes what one capture unit means. Moving it
    alone leaves the capture window addressing a different fraction of the line
    than it did before, which looks like corruption rather than a mis-set window.
    """
    old = read_field(5, 0x12, 0, 12)
    if not old:
        return {"ok": False, "why": "could not read PLLAD_MD"}
    ratio = target / old
    moved = {}
    for name, seg, reg, off, width in SCALES_WITH_PLLAD:
        value = read_field(seg, reg, off, width)
        if value is None:
            continue
        scaled = max(0, min(round(value * ratio), (1 << width) - 1))
        write_field(seg, reg, off, width, scaled)
        moved[name] = scaled
    write_field(5, 0x12, 0, 12, target)
    latch_pllad()
    time.sleep(0.4)
    return {
        "ok": True,
        "from": old,
        "to": read_field(5, 0x12, 0, 12),
        "ratio": round(ratio, 4),
        "moved": moved,
        "locked": bool(read_field(0, 0x09, 7, 1)),
        "htotal": read_field(0, 0x17, 0, 12),
    }


# --- pan and scale ------------------------------------------------------------
#
# The ENGINE owns these. This panel asks it for a framing and reads the result
# back; it does not solve the geometry and it does not write the answer. A second
# solver here was a second owner of the same registers.

GEOMETRY_FIELDS = [
    ("IF_HB_SP2", 1, 0x1A, 0, 11), ("IF_HB_ST2", 1, 0x18, 0, 11),
    ("IF_VB_SP", 1, 0x1E, 0, 11), ("IF_VB_ST", 1, 0x1C, 0, 11),
    ("IF_HSYNC_RST", 1, 0x0E, 0, 11),
    ("VDS_HSYNC_RST", 3, 0x01, 0, 12), ("VDS_VSYNC_RST", 3, 0x02, 4, 11),
    ("VDS_HSCALE", 3, 0x16, 0, 10), ("VDS_VSCALE", 3, 0x17, 4, 10),
    ("VDS_HSCALE_BYPS", 3, 0x00, 4, 1), ("VDS_VSCALE_BYPS", 3, 0x00, 5, 1),
    ("VDS_HB_SP", 3, 0x05, 4, 12), ("VDS_HB_ST", 3, 0x04, 0, 12),
    ("VDS_VB_SP", 3, 0x08, 4, 11), ("VDS_VB_ST", 3, 0x07, 0, 11),
    ("VDS_DIS_HB_SP", 3, 0x11, 4, 12), ("VDS_DIS_HB_ST", 3, 0x10, 0, 12),
    ("VDS_DIS_VB_SP", 3, 0x14, 4, 11), ("VDS_DIS_VB_ST", 3, 0x13, 0, 11),
    ("VTOTAL", 0, 0x1B, 0, 11),
    # The four the scan mode is applied as one, because what a vertical capture
    # unit IS depends on them.
    ("IF_PRGRSV_CNTRL", 1, 0x00, 6, 1), ("IF_LD_RAM_BYPS", 1, 0x0C, 0, 1),
    ("IF_LD_SEL_PROV", 1, 0x0B, 7, 1), ("IF_HS_DEC_FACTOR", 1, 0x0B, 4, 2),
]

GEOMETRY_SPECS = {name: (seg, reg, off, width)
                  for name, seg, reg, off, width in GEOMETRY_FIELDS}


def capture_wraps_at(state):
    """Where each capture window rolls over rather than clamping.

    Horizontally that is the input line. Vertically it depends on the scan mode,
    because the doubler runs the IF's line counter at twice the source rate --
    see geometry_math.if_vertical_wrap. Crossing either makes the picture jump,
    which has been misread as losing the capture. The vertical is None when the
    scan mode registers disagree, because a guessed wrap is worse than no bound.
    """
    doubled = geometry_math.line_doubled(
        state["IF_PRGRSV_CNTRL"], state["IF_LD_RAM_BYPS"],
        state["IF_LD_SEL_PROV"], state["IF_HS_DEC_FACTOR"])
    return (state["IF_HSYNC_RST"] + 1,
            geometry_math.if_vertical_wrap(state["VTOTAL"], doubled))


def geometry_now():
    """Current geometry, and where the formula says the windows belong.

    Reports only -- reads never write. The display window is brought to the
    formula when the user presses a pad, not on the refresh poll: a poll that
    wrote would put a register change on the bus every 1500 ms with nobody
    asking, and on a bench where the boundary is the measurement, a write
    nobody made is a measurement nobody can trust.
    """
    state = read_fields(fetch_span, GEOMETRY_FIELDS)
    if state is None:
        return None
    wrap_h, wrap_v = capture_wraps_at(state)
    return {
        "state": state,
        "framing": framing(),
        "wrap": {"horizontal": wrap_h, "vertical": wrap_v},
    }






# Only the far edge of each memory window. Widening it can only add headroom --
# it cannot move the picture, cannot crop it, and cannot expose scratch.

# The aperture the picture is seen through, all four edges. The near edges are
# pinned to the measured corner and the far edges follow `produced`, so the
# whole window is derived from the capture and the scale rather than inherited
# from whatever was there.
#
# Both ways of getting that wrong have the same cause, and were on the bench at
# once on 2026-08-05: horizontally a 1262 px aperture on a 1176 px picture, showing
# 86 px of unwritten memory as garbage down the right; vertically a 676-line
# aperture on a ~786-line picture, cropping the bottom and making the scale
# control look dead, because VSCALE then only changes how much of the picture is
# thrown away, never how much is visible.




def framing():
    """The engine's own framing: where the capture window starts and how far it
    runs, per axis, in input units. None when the route is absent, which at
    GBS_DEBUG=0 it is."""
    body = _get("/geometry")
    if not body:
        return None
    try:
        return json.loads(body)
    except ValueError:
        return None


def set_framing(**wanted):
    """Walk each named field to its value through the pads, and report where the
    framing landed. There is no route that sets it outright: a panel must reach
    what the person at the OSD can reach."""
    landed = None
    for field, value in wanted.items():
        landed = gbs_unit.framing_to(HOST, field, value)
        if landed is None:
            return None
    return landed


def pan(dx, dy):
    """Move the picture, by asking the ENGINE rather than writing registers.

    The engine calculates every register from held state, so a register written
    behind its back is state it does not know it has. This panel used to solve
    the geometry itself and write the answer, which made it a second owner of
    the same registers and a second implementation to keep in step.

    `oh` and `ov` are where the capture window starts, in input units -- the
    units this panel's step box already counts in, so a delta passes straight
    through.
    """
    held = framing()
    if held is None:
        return {"ok": False, "why": gbs_unit.GEOMETRY_GATED
                if gbs_unit.geometry_gated(HOST) else "could not read /geometry"}
    if not dx and not dy:
        return {"ok": False, "why": "no movement asked for"}
    wanted = {"oh": held["oh"] + dx, "ov": held["ov"] + dy}
    if set_framing(**wanted) is None:
        return {"ok": False, "why": "the engine refused the framing"}
    time.sleep(0.2)
    return {"ok": True, "wrote": wanted}


def rescale(dh, dv):
    """Zoom, by asking the engine. It crops; the picture keeps filling the raster.

    These pads send a NEGATIVE delta to crop in, and `eh`/`ev` are how far the
    capture window runs -- so cropping in is the extent going DOWN and the delta
    passes through with its own sign.
    """
    held = framing()
    if held is None:
        return {"ok": False, "why": gbs_unit.GEOMETRY_GATED
                if gbs_unit.geometry_gated(HOST) else "could not read /geometry"}
    if not dh and not dv:
        return {"ok": False, "why": "no scale change asked for"}
    wanted = {"eh": held["eh"] + dh, "ev": held["ev"] + dv}
    if set_framing(**wanted) is None:
        return {"ok": False, "why": "the engine refused the framing"}
    time.sleep(0.2)
    return {"ok": True, "wrote": wanted,
            "zoomed": [n for n, d in (("horizontal", dh), ("vertical", dv)) if d]}


PAGE = r"""<!doctype html><html><head><meta charset="utf-8"><title>TV5725 register panel</title>
<style>
 body{font:13px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace;margin:0;background:#14161a;color:#dfe3e8}
 header{position:sticky;top:0;background:#1b1f25;border-bottom:1px solid #2a3038;padding:10px 14px;z-index:5}
 h1{font-size:14px;margin:0 0 6px;font-weight:600;letter-spacing:.02em}
 #status{display:flex;flex-wrap:wrap;gap:14px;font-size:12px}
 #status b{color:#9fb4c9;font-weight:500}
 .warn{color:#ff8f6b!important}
 .ok{color:#7fd18e}
 main{padding:14px;max-width:1100px}
 section{margin-bottom:18px}
 h2{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:#7e8b99;margin:0 0 6px;font-weight:600}
 table{border-collapse:collapse;width:100%}
 td{padding:3px 6px;border-bottom:1px solid #21262d;vertical-align:middle}
 td.n{color:#cfe0f0;white-space:nowrap}
 td.a{color:#5d6b7a;font-size:11px;white-space:nowrap}
 td.d{color:#6d7c8b;font-size:11px}
 input{width:80px;background:#0f1216;color:#f0f4f8;border:1px solid #333c47;border-radius:3px;padding:3px 5px;font:inherit;text-align:right}
 input:focus{outline:1px solid #4d7fb3}
 button{background:#252c34;color:#cfd8e3;border:1px solid #333c47;border-radius:3px;padding:2px 7px;font:inherit;cursor:pointer}
 button:hover{background:#2e3740}
 button.p{background:#2b4a6b;border-color:#39618a}
 .chg{background:#3a2f14!important;border-color:#7a6320!important}
 #bar{display:flex;gap:8px;margin-bottom:12px;flex-wrap:wrap}
 #log{color:#7e8b99;font-size:11px;margin-left:auto;align-self:center}
</style></head><body>
<header>
 <h1>TV5725 register panel &mdash; <span id="host"></span></h1>
 <div id="status">connecting&hellip;</div>
</header>
<main>
 <div id="bar">
  <button class="p" onclick="refresh()">Refresh all</button>
  <button onclick="snapshot()">Snapshot</button>
  <button onclick="revert()">Revert to snapshot</button>
  <span style="margin-left:14px">PLLAD_MD + rescale everything:
   <input id="pll" style="width:70px" value="">
   <button class="p" onclick="pllad()">go</button></span>
  <button onclick="measure()">Measure source (12x)</button>
  <button class="p" onclick="geo()">Pan &amp; scale</button>
  <span style="margin-left:14px">browse segment:
   <button onclick="seg(0)">0 status</button><button onclick="seg(1)">1 input fmt</button><button onclick="seg(2)">2 deint</button><button onclick="seg(3)">3 video proc</button><button onclick="seg(4)">4 memory</button><button onclick="seg(5)">5 adc/sync/pll</button><button onclick="document.getElementById('found').innerHTML=''">clear</button></span>
  <span style="margin-left:14px">search:
   <input id="q" style="width:150px" placeholder="name or description"
          oninput="dosearch()"></span>
  <span id="log"></span>
 </div>
 <div id="geo"></div>
 <div id="measured"></div>
 <div id="found"></div>
 <div id="groups"></div>
</main>
<script>
let SNAP={}, GUARD=null;
const log=m=>document.getElementById('log').textContent=m;

async function api(p,o){const r=await fetch(p,o);return r.json()}

async function refresh(){
  const d=await api('/api/all');
  const g=document.getElementById('groups'); g.innerHTML='';
  for(const grp of d.groups){
    const s=document.createElement('section');
    s.innerHTML='<h2>'+grp.name+'</h2>';
    const t=document.createElement('table');
    for(const f of grp.fields){
      const tr=document.createElement('tr');
      tr.innerHTML=
        '<td class="n">'+f.name+'</td>'+
        '<td class="a">s'+f.seg+' 0x'+f.reg.toString(16).padStart(2,'0')+
          (f.off?' b'+f.off:'')+':'+f.w+'</td>'+
        '<td><input id="v_'+f.name+'" value="'+f.val+'" data-orig="'+f.val+'"></td>'+
        '<td>'+
          '<button onclick="nudge(\''+f.name+'\',-10)">-10</button> '+
          '<button onclick="nudge(\''+f.name+'\',-1)">-1</button> '+
          '<button onclick="nudge(\''+f.name+'\',1)">+1</button> '+
          '<button onclick="nudge(\''+f.name+'\',10)">+10</button> '+
          '<button class="p" onclick="apply(\''+f.name+'\')">set</button>'+
        '</td>'+
        '<td class="d">'+f.doc+(f.ds?' <span style="opacity:.65">&mdash; '+f.ds+'</span>':'')+'</td>';
      t.appendChild(tr);
    }
    s.appendChild(t); g.appendChild(s);
  }
  log('read '+new Date().toLocaleTimeString());
}

async function apply(name){
  const el=document.getElementById('v_'+name);
  const r=await api('/api/set?name='+encodeURIComponent(name)+'&value='+parseInt(el.value,10));
  if(r.ok){el.dataset.orig=r.value;el.value=r.value;el.classList.remove('chg');log(name+' = '+r.value)}
  else log('FAILED '+name);
}

function nudge(name,d){
  const el=document.getElementById('v_'+name);
  el.value=Math.max(0,parseInt(el.value,10)+d);
  el.classList.add('chg');
  apply(name);
}

const SEGNAME=['status (read-only measurements)','input formatter','de-interlacer',
               'video processor / display','memory & capture','ADC, sync processor, PLLs'];
async function seg(n){
  const box=document.getElementById('found');
  box.innerHTML='<section><h2>segment '+n+'</h2><p class="d">reading&hellip;</p></section>';
  log('reading segment '+n+'...');
  const d=await api('/api/segment?n='+n);
  let h='<section><h2>segment '+n+' &mdash; '+SEGNAME[n]+' &mdash; '+d.fields.length+
        ' fields from '+d.registers_read+' registers</h2><table>';
  let lastreg=-1;
  for(const f of d.fields){
    if(f.reg!==lastreg){ lastreg=f.reg;
      h+='<tr><td colspan="5" class="a" style="padding-top:8px;color:#7e8b99">'+
         '&mdash;&mdash; S'+n+'_'+f.reg.toString(16).padStart(2,'0').toUpperCase()+'</td></tr>'; }
    h+='<tr><td class="n">'+f.name+'</td>'+
       '<td class="a">b'+f.off+':'+f.w+'</td>'+
       '<td><input id="s_'+f.name+'" style="width:70px" value="'+(f.val===null?'?':f.val)+'"></td>'+
       '<td><button onclick="peek(\''+f.name+'\')">read</button> '+
           '<button class="p" onclick="poke(\''+f.name+'\')">set</button></td>'+
       '<td class="d">'+(f.desc||'')+'</td></tr>';
  }
  box.innerHTML=h+'</table></section>';
  box.scrollIntoView({behavior:'smooth'});
  log('segment '+n+': '+d.fields.length+' fields, '+d.registers_read+' registers read');
}

let SEARCHT=null;
function dosearch(){clearTimeout(SEARCHT);SEARCHT=setTimeout(runsearch,250)}
async function runsearch(){
  const q=document.getElementById('q').value;
  const box=document.getElementById('found');
  if(q.length<2){box.innerHTML='';return}
  const d=await api('/api/search?q='+encodeURIComponent(q));
  if(!d.hits.length){box.innerHTML='<section><h2>search</h2><p class="d">no match</p></section>';return}
  let h='<section><h2>search &mdash; '+d.hits.length+' match'+(d.hits.length>1?'es':'')+'</h2><table>';
  for(const f of d.hits){
    h+='<tr><td class="n">'+f.name+'</td>'+
       '<td class="a">s'+f.seg+' 0x'+f.reg.toString(16).padStart(2,'0')+(f.off?' b'+f.off:'')+':'+f.w+'</td>'+
       '<td><input id="s_'+f.name+'" style="width:70px" value="?"></td>'+
       '<td><button onclick="peek(\''+f.name+'\')">read</button> '+
           '<button class="p" onclick="poke(\''+f.name+'\')">set</button></td>'+
       '<td class="d">'+(f.desc||'')+'</td></tr>';
  }
  box.innerHTML=h+'</table></section>';
}
async function peek(n){
  const r=await api('/api/read?name='+encodeURIComponent(n));
  document.getElementById('s_'+n).value = r.ok ? r.value : 'err';
}
async function poke(n){
  const v=parseInt(document.getElementById('s_'+n).value,10);
  if(isNaN(v)){log('enter a value first');return}
  const r=await api('/api/set?name='+encodeURIComponent(n)+'&value='+v);
  log(r.ok ? (n+' = '+r.value) : ('FAILED '+n));
  await refresh();
}

// --- pan and scale -----------------------------------------------------------
// Two 4-way pads. Move shifts the capture window, which changes WHICH part of
// the source is shown without changing how big it is. Scale changes how big it
// is, which moves the picture, so every scale change re-solves the output
// windows. Lower VDS_?SCALE means a bigger picture, so '+' sends a negative step.
// The controls are named for what the PICTURE does, not for the register. Both
// are inverted against their register: panning the capture window right shows
// source content further right, which slides the picture LEFT; and lower
// VDS_?SCALE makes a BIGGER picture. Getting either backwards makes the tool
// feel broken, so the sign lives here, once, next to the label.
// The step survives outside the DOM because every geo() refresh rebuilds the
// whole panel with innerHTML, which destroys the input and took the typed value
// with it -- so a creep of 8 silently became a creep of 1 on the second press,
// and the register moved by an amount nobody asked for. Persisted too, because
// the step is a property of what is being measured, not of the page.
let gstep = parseInt(localStorage.getItem('gstep') || '1', 10) || 1;

function setStep(value){
  const v=parseInt(value,10);
  gstep=(isNaN(v)||v<1)?1:v;
  localStorage.setItem('gstep',gstep);
}

function step(){
  const el=document.getElementById('gstep');
  if(el) setStep(el.value);
  return gstep;
}

function pad(label,keys){
  return '<div style="display:inline-block;margin-right:26px;vertical-align:top">'+
    '<div class="d" style="margin-bottom:4px">'+label+'</div>'+
    '<div style="text-align:center">'+
    '<button onclick="'+keys[0]+'" style="width:44px">&uarr;</button><br>'+
    '<button onclick="'+keys[2]+'" style="width:44px">&larr;</button>'+
    '<button onclick="'+keys[3]+'" style="width:44px">&rarr;</button><br>'+
    '<button onclick="'+keys[1]+'" style="width:44px">&darr;</button>'+
    '</div></div>';
}


async function geo(){
  const box=document.getElementById('geo');
  const d=await api('/api/geometry');
  if(!d.ok){box.innerHTML='<section><h2>pan &amp; scale</h2><p class="warn">'+
    (d.why||'no answer')+'</p></section>';return}
  const s=d.state, f=d.framing;
  let h='<section><h2>Pan &amp; scale</h2>'+
    pad('move the picture',
        ['panBy(0,step())','panBy(0,-step())',
         'panBy(step(),0)','panBy(-step(),0)'])+
    pad('scale &mdash; &uarr; taller, &rarr; wider',
        ['scaleBy(0,-step())','scaleBy(0,step())',
         'scaleBy(step(),0)','scaleBy(-step(),0)'])+
    '<div style="display:inline-block;margin-right:26px;vertical-align:top">'+
    '<div class="d" style="margin-bottom:4px">step</div>'+
    '<input id="gstep" value="'+gstep+'" oninput="setStep(this.value)" '+
    'style="width:44px;text-align:center">'+
    '<div class="d" style="margin-top:4px">units per press</div></div>'+
    '<div style="display:inline-block;vertical-align:top">'+
    '<div class="d" style="margin-bottom:4px">engine framing</div>'+
    (f? '<div class="ok" style="margin-top:6px">capture h '+f.oh+'+'+f.eh+'/'+f.ch+
        ' &nbsp;v '+f.ov+'+'+f.ev+'/'+f.cv+'</div>'
      : '<div class="warn" style="margin-top:6px">/geometry did not answer</div>')+
    '</div>'+
    '<p class="d">capture '+(s.IF_HB_ST2-s.IF_HB_SP2)+' IF units ['+
      s.IF_HB_SP2+'..'+s.IF_HB_ST2+'] of '+d.wrap.horizontal+
      ' &nbsp;&middot;&nbsp; '+(s.IF_VB_ST-s.IF_VB_SP)+' half-lines ['+
      s.IF_VB_SP+'..'+s.IF_VB_ST+'] of '+d.wrap.vertical+
      ' &nbsp;&middot;&nbsp; HSCALE '+s.VDS_HSCALE+' VSCALE '+s.VDS_VSCALE+
      ' &nbsp;&middot;&nbsp; raster '+(s.VDS_HSYNC_RST+1)+'x'+(s.VDS_VSYNC_RST+1)+
      '</p>'+
    '<p class="d">memory '+s.VDS_HB_SP+'..'+s.VDS_HB_ST+' x '+
      s.VDS_VB_SP+'..'+s.VDS_VB_ST+' &nbsp;&middot;&nbsp; display '+
      s.VDS_DIS_HB_SP+'..'+s.VDS_DIS_HB_ST+' x '+
      s.VDS_DIS_VB_SP+'..'+s.VDS_DIS_VB_ST+'</p>';
  box.innerHTML=h+'</section>';
}

async function panBy(dx,dy){
  const r=await api('/api/pan?dx='+dx+'&dy='+dy);
  log(r.ok?('panned '+JSON.stringify(r.wrote)):('pan: '+(r.why||'failed')));
  geo();
}

async function scaleBy(dh,dv){
  const r=await api('/api/scale?dh='+dh+'&dv='+dv);
  log(r.ok?'rescaled':('scale: '+(r.why||'failed')));
  geo();
}

async function measure(){
  log('sampling source measurement registers...');
  const d=await api('/api/measure?n=12');
  let h='<section><h2>Source measurement &mdash; '+d.samples+' samples each</h2><table>'+
        '<tr><td class="a">field</td><td class="a">mean</td><td class="a">min..max</td>'+
        '<td class="a">spread</td><td class="a">verdict</td><td class="a">note</td></tr>';
  for(const f of d.fields){
    h+='<tr><td class="n">'+f.name+'</td>'+
       '<td>'+f.mean+(f.x4?' <span class="d">(x4 = '+f.x4+')</span>':'')+'</td>'+
       '<td>'+f.min+'..'+f.max+'</td>'+
       '<td>'+f.spread+'</td>'+
       '<td class="'+(f.stable?'ok':'warn')+'">'+(f.stable?'stable':'MOVING')+'</td>'+
       '<td class="d">'+f.doc+'</td></tr>';
  }
  document.getElementById('measured').innerHTML=h+'</table></section>';
  log('measured '+new Date().toLocaleTimeString());
}

async function pllad(){
  const v=parseInt(document.getElementById('pll').value,10);
  if(!v){log('enter a PLLAD_MD value');return}
  log('moving PLLAD_MD and everything derived from it...');
  const r=await api('/api/pllad?value='+v);
  await refresh();
  log(r.ok ? ('PLLAD_MD '+r.from+' -> '+r.to+'  (x'+r.ratio+')  HTOTAL '+r.htotal+
              '  PLL '+(r.locked?'locked':'UNLOCKED - back it out'))
           : 'failed');
}

async function snapshot(){SNAP=(await api('/api/all')).flat;log('snapshot taken')}
async function revert(){
  if(!Object.keys(SNAP).length){log('no snapshot');return}
  await api('/api/setmany',{method:'POST',body:JSON.stringify(SNAP)});
  await refresh(); log('reverted');
}

async function poll(){
  try{
    const s=await api('/api/status');
    if(GUARD===null) GUARD=s.VTOTAL;
    const st=s.STATUS_16;
    const moved=s.VTOTAL!==GUARD;
    document.getElementById('status').innerHTML=
      '<span'+(moved?' class="warn"':'')+'><b>VTOTAL</b> '+s.VTOTAL+(moved?' ⚠ moved from '+GUARD:'')+'</span>'+
      '<span><b>HTOTAL</b> '+s.HTOTAL+'</span>'+
      '<span><b>HPERIOD_IF</b> '+s.HPERIOD_IF+' (x4 = '+(s.HPERIOD_IF*4)+' px)</span>'+
      '<span><b>VPERIOD_IF</b> '+s.VPERIOD_IF+'</span>'+
      '<span><b>H</b> '+((st&2)?'act':'---')+((st&1)?'+':'-')+
        ' <b>V</b> '+((st&8)?'act':'---')+((st&4)?'+':'-')+'</span>'+
      '<span class="'+(s.PLLAD_LOCK?'ok':'warn')+'"><b>PLL</b> '+(s.PLLAD_LOCK?'locked':'UNLOCKED')+'</span>'+
      '<span><b>preset</b> 0x'+s['preset ID'].toString(16)+'</span>'+
      '<button onclick="GUARD=null" style="padding:0 6px">reset guard</button>'+
      (s.checks||[]).filter(c=>!c.ok).map(c=>
        '<span class="warn" title="'+c.detail+'">⚠ '+c.label+': '+c.detail+'</span>').join('');
  }catch(e){document.getElementById('status').innerHTML='<span class="warn">unit not responding</span>'}
  setTimeout(poll,1500);
}
document.getElementById('host').textContent=location.host;
refresh(); poll();
</script></body></html>"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def _send(self, body, ctype="application/json"):
        raw = body.encode() if isinstance(body, str) else body
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        path, _, query = self.path.partition("?")
        args = dict(p.split("=", 1) for p in query.split("&") if "=" in p)

        if path == "/":
            return self._send(PAGE, "text/html; charset=utf-8")

        if path == "/api/status":
            values = read_fields(fetch_span, STATUS + INVARIANTS) or {}
            out = {name: values.get(name) for name, *_ in STATUS}
            out["checks"] = check_invariants(values)
            return self._send(json.dumps(out))

        if path == "/api/all":
            every = [f for _, fields in GROUPS for f in fields]
            flat = read_fields(fetch_span, every) or {}
            groups = []
            for gname, fields in GROUPS:
                entries = []
                for name, seg, reg, off, width, doc in fields:
                    val = flat.get(name)
                    ref = REGISTER_DOC.get(name, {})
                    entries.append({"name": name, "seg": seg, "reg": reg,
                                    "off": off, "w": width, "val": val, "doc": doc,
                                    "ds": ref.get("desc", "")})
                groups.append({"name": gname, "fields": entries})
            return self._send(json.dumps({"groups": groups, "flat": flat}))

        if path == "/api/set":
            name = urllib.parse.unquote(args.get("name", ""))
            try:
                value = int(args.get("value", ""))
            except ValueError:
                return self._send(json.dumps({"ok": False}))
            spec = find_field(name)
            if spec:
                    seg, reg, off, width = spec
                    if True:
                        value = max(0, min(value, (1 << width) - 1))
                        ok = write_field(seg, reg, off, width, value)
                        if name in NEEDS_LATCH:
                            latch_pllad()
                            time.sleep(0.2)
                        back = read_field(seg, reg, off, width)
                        return self._send(json.dumps(
                            {"ok": ok, "value": back,
                             "locked": bool(read_field(0, 0x09, 7, 1))}))
            return self._send(json.dumps({"ok": False}))

        if path == "/api/segment":
            # Every field in one segment, with values, off a single fetched span.
            # Segment 3 has 188 fields across ~60 registers; de-duplicating the
            # registers made that a few dozen round trips, and fetching the span
            # makes it one.
            try:
                seg = int(args.get("n", "0"))
            except ValueError:
                seg = 0
            fields = sorted(((n, e) for n, e in REGISTER_DOC.items()
                             if e["seg"] == seg and e["reg"] <= 0xFF),
                            key=lambda kv: (kv[1]["reg"], kv[1]["off"]))
            values = read_fields(
                fetch_span,
                [(n, seg, e["reg"], e["off"], e["width"]) for n, e in fields]) or {}
            out = [{"name": name, "seg": seg, "reg": e["reg"],
                    "off": e["off"], "w": e["width"], "val": values.get(name),
                    "desc": e.get("desc", "")[:220]}
                   for name, e in fields]
            return self._send(json.dumps(
                {"seg": seg, "registers_read": len(out), "fields": out}))

        if path == "/api/search":
            q = urllib.parse.unquote(args.get("q", "")).upper()
            hits = []
            if len(q) >= 2:
                for name, e in sorted(REGISTER_DOC.items()):
                    if q in name.upper() or q in e.get("desc", "").upper():
                        hits.append({"name": name, "seg": e["seg"], "reg": e["reg"],
                                     "off": e["off"], "w": e["width"],
                                     "desc": e.get("desc", "")[:200]})
                    if len(hits) >= 60:
                        break
            return self._send(json.dumps({"hits": hits}))

        if path == "/api/read":
            name = urllib.parse.unquote(args.get("name", ""))
            spec = find_field(name)
            if not spec:
                return self._send(json.dumps({"ok": False}))
            seg, reg, off, width = spec
            return self._send(json.dumps(
                {"ok": True, "value": read_field(seg, reg, off, width)}))

        if path == "/api/measure":
            # The firmware never trusts a single read of these: updateCoastPosition
            # samples eight times and abandons the measurement if any two differ by
            # more than 3. Same idea here, but reporting the spread rather than
            # hiding it, since how noisy they are is itself the open question.
            try:
                n = max(3, min(int(args.get("n", "12")), 40))
            except ValueError:
                n = 12
            out = []
            for name, seg, reg, off, width, doc in MEASURED:
                samples = []
                for _ in range(n):
                    v = read_field(seg, reg, off, width)
                    if v is not None:
                        samples.append(v)
                if not samples:
                    continue
                lo, hi = min(samples), max(samples)
                mean = sum(samples) / len(samples)
                out.append({
                    "name": name, "n": len(samples),
                    "min": lo, "max": hi, "spread": hi - lo,
                    "mean": round(mean, 1),
                    "stable": (hi - lo) <= 3,          # the firmware's own criterion
                    "x4": round(mean * 4) if name == "HPERIOD_IF" else None,
                    "doc": doc,
                })
            return self._send(json.dumps({"samples": n, "fields": out}))

        if path == "/api/geometry":
            report = geometry_now()
            if report is None:
                return self._send(json.dumps({"ok": False, "why": "no answer"}))
            return self._send(json.dumps(dict(report, ok=True)))

        if path == "/api/pan":
            def step(key):
                try:
                    return int(args.get(key, "0"))
                except ValueError:
                    return 0
            return self._send(json.dumps(pan(step("dx"), step("dy"))))

        if path == "/api/scale":
            def step(key):
                try:
                    return int(args.get(key, "0"))
                except ValueError:
                    return 0
            return self._send(json.dumps(rescale(step("dh"), step("dv"))))

        if path == "/api/pllad":
            try:
                target = int(args.get("value", ""))
            except ValueError:
                return self._send(json.dumps({"ok": False}))
            return self._send(json.dumps(set_pllad_scaled(target)))

        self.send_response(404)
        self.end_headers()

    def do_POST(self):
        if self.path != "/api/setmany":
            self.send_response(404)
            self.end_headers()
            return
        length = int(self.headers.get("Content-Length", 0))
        wanted = json.loads(self.rfile.read(length) or "{}")
        for _, fields in GROUPS:
            for name, seg, reg, off, width, _doc in fields:
                if name in wanted and wanted[name] is not None:
                    write_field(seg, reg, off, width, int(wanted[name]))
        self._send(json.dumps({"ok": True}))


def main():
    global HOST
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True, help="the unit, e.g. 192.168.88.108")
    ap.add_argument("--port", type=int, default=8752)
    args = ap.parse_args()
    HOST = args.host
    print(f"registers {len(REGISTER_DOC)} known"
          f" ({sum(1 for e in REGISTER_DOC.values() if e.get('desc'))} documented)")

    if read_reg(0, 0x16) is None:
        raise SystemExit(f"{HOST}: no answer from /getreg — is the unit up?")

    print(f"unit    {HOST}")
    print(f"panel   http://localhost:{args.port}")
    print("nothing written here persists; a mode change or reset reloads the preset")
    # Threaded: /api/measure makes ~120 round trips to the unit, and a
    # single-threaded server would block the status poll for its whole
    # duration, which looks exactly like the page having crashed.
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
