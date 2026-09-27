#!/usr/bin/env python3
"""Measure the source's four blanking regions off the emitted frame, in source
pixels and lines, against what the mode file states.

    python3 tools/gbsc-pro-hwtest/full_margins.py --host <ip> --modes "X320 Y256 C256 F50"

At full framing the capture window spans the whole capturable line and frame, so
the emitted picture carries the source's own blanking at every edge and each
margin is a measurement of the source's raster rather than of our placement.

The card's green frame is drawn on the outermost pixels of the framebuffer, so
the two green edges are `active - 1` source units apart whatever the source is
doing. That separation is the ruler: it gives capture units per source pixel,
and the line counter divided by it is how many pixels the source really puts on
a line. The transmitted window's latch shifts both edges together and so cancels
out of every span; it reaches only the absolute figures, by a few columns.

An IF unit is TWO ADC samples on a line-doubled source and one otherwise, so
every figure the sync processor reports -- it counts in ADC samples -- is
converted before it meets a window register.
"""
import argparse
import re
import sys
import time

import numpy as np

sys.path.insert(0, "/home/msharman/Projects/gbsc-pro/tools/gbsc-pro-hwtest")
import card_edges
import gbs_unit
import hdmi_capture
import mdf_modes

MODESERV = "192.168.88.10"
MDF = "/home/msharman/Projects/RiscPc/tools/video-source/RetroScaler-Acorn.mdf"
# The encoder's own line total for the 1125-line output raster it locks to: our
# line is resampled to this, which is what makes a dongle column a known
# fraction of a scaler pixel. The vertical is 1125 at both ends and so is 1:1.
ENCODER_TOTAL = 2200

FIELDS = ["PLLAD_MD", "IF_HSYNC_RST", "IF_HB_SP2", "IF_HB_ST2",
          "IF_VB_SP", "IF_VB_ST", "VDS_HSCALE", "VDS_VSCALE",
          "VDS_HSYNC_RST", "VDS_VSYNC_RST", "VDS_DIS_HB_SP", "VDS_DIS_HB_ST",
          "VDS_DIS_VB_SP", "VDS_DIS_VB_ST",
          "STATUS_SYNC_PROC_HTOTAL", "STATUS_SYNC_PROC_VTOTAL",
          "STATUS_SYNC_PROC_HLOW_LEN",
          "SP_RT_HS_ST", "SP_RT_HS_SP", "IF_HBIN_SP", "IF_HBIN_ST"]


def stated(mode):
    """The mode file entry MODE picks for this request, by X, Y and F."""
    want = dict(re.findall(r"([XYF])(\d+)", mode))
    best = None
    for entry in mdf_modes.parse(MDF):
        if entry["x_res"] != want["X"] or entry["y_res"] != want["Y"]:
            continue
        h = [int(v) for v in entry["h_timings"].split(",")]
        v = [int(v) for v in entry["v_timings"].split(",")]
        clock = int(entry["pixel_rate"]) * 1000.0
        off = abs(clock / sum(h) / sum(v) - float(want["F"]))
        if best is None or off < best[0]:
            best = (off, h, v, clock)
    return best[1:] if best else None


def open_wide(host):
    """The engine's own full framing: the whole capturable line and frame.

    The route QUEUES the change, and the default framing already opens at the
    window's floor, so the geometry alone cannot say whether it took. The route
    reporting the state with nothing still queued is what can.
    """
    if gbs_unit.get(host, "/framing/full?on=1")[0] != 200:
        return None
    took = gbs_unit.wait_for(
        lambda: (lambda at: at if at and at["full"] and not at["queued"] else None)(
            gbs_unit.get_json(host, "/framing/full")[1]), timeout=20.0)
    return took and gbs_unit.framing_settled(host)


def green_edges(clip, axis, expected):
    """The card frame's two edges along `axis`, as CENTROIDS in captured pixels.

    A centroid rather than a run's midpoint: the frame is one source pixel wide
    and the interpolator smears it over several output ones, so where the run
    starts and stops follows the threshold while its weight does not.

    The pair is the one whose separation comes closest to `expected`, never the
    outermost two. Green appears at an edge for reasons that are not the card --
    a row of unwritten memory past the end of the written picture is one, and it
    reads as the frame with the picture stretched a third beyond its own raster.
    The span is known from the scale and the card's own size; only the POSITION
    is being measured, so using it to identify the edges begs no question. The
    residual is printed, so a bad pick is visible rather than silent.
    """
    greenest = np.zeros(clip.shape[1 + axis], np.float32)
    for rgb in clip:
        r, g, b = (rgb[:, :, i].astype(np.float32) for i in range(3))
        greenest = np.maximum(greenest,
                              (g - np.maximum(r, b)).mean(axis=1 - axis))
    runs = card_edges.green_runs(clip, axis)

    def centroid(run):
        at = np.arange(run[0], run[1] + 1, dtype=np.float32)
        weight = np.maximum(greenest[run[0]:run[1] + 1] - card_edges.GREEN_HUE, 0)
        return float((at * weight).sum() / weight.sum()) if weight.sum() else None

    at = [(run, centroid(run)) for run in runs]
    pairs = [(abs(far - near), near, far)
             for _, near in at for _, far in at
             if near is not None and far is not None and far > near]
    if not pairs:
        return None, runs, None
    best = min(pairs, key=lambda p: abs(p[0] - expected))
    return (best[1], best[2]), runs, best[0] - expected


def report(mode, field, geo, clip, h, v, clock):
    units = field["IF_HSYNC_RST"] + 1
    # An IF unit is two ADC samples on a doubled line and one otherwise, and the
    # vertical window counts doubled lines where the sync processor counts the
    # source's. Both of the chip's own figures cross that boundary.
    doubling = max(1, int(round(field["PLLAD_MD"] / float(field["IF_HSYNC_RST"]))))
    out_total = field["VDS_HSYNC_RST"] + 1
    active_px, active_ln = int(h[3]), int(v[3])
    file_total, file_vtotal = sum(h), sum(v)
    frame_lines = field["STATUS_SYNC_PROC_VTOTAL"] + 1

    # Dongle pixels per capture unit, and per captured line: the scale, then the
    # encoder resampling our line to its own total. The vertical is 1:1.
    per_unit = 1024.0 / field["VDS_HSCALE"] * ENCODER_TOTAL / out_total
    per_line = 1024.0 / field["VDS_VSCALE"]
    # What the file's own line total implies, to identify the edges with. The
    # measured ruler replaces it below.
    seed = units / float(file_total)
    across, runs, h_off = green_edges(clip, 1, (active_px - 1) * seed * per_unit)
    down, vruns, v_off = green_edges(clip, 0, (active_ln - 1) * doubling * per_line)

    print(f"{mode}")
    print(f"  window  h {field['IF_HB_SP2']}..{field['IF_HB_ST2']} of {units}"
          f"   v {field['IF_VB_SP']}..{field['IF_VB_ST']} of {frame_lines * doubling}"
          f"   hscale {field['VDS_HSCALE']} vscale {field['VDS_VSCALE']}"
          f"   out {out_total}x{field['VDS_VSYNC_RST'] + 1}"
          f"   md {field['PLLAD_MD']} (x{doubling})")
    if across is None or down is None:
        print(f"  NO FRAME on both edges: across {runs} down {vruns}")
        return

    left = field["IF_HB_SP2"] + across[0] / per_unit
    right = field["IF_HB_SP2"] + across[1] / per_unit
    top = (field["IF_VB_SP"] + down[0] / per_line) / doubling
    bottom = (field["IF_VB_SP"] + down[1] / per_line) / doubling

    units_px = (right - left) / (active_px - 1.0)
    total_px = units / units_px
    print(f"  green   across {across[0]:7.1f}..{across[1]:7.1f} px"
          f"  = units {left:7.1f}..{right:7.1f}"
          f"   down {down[0]:6.1f}..{down[1]:6.1f} px = lines {top:6.1f}..{bottom:6.1f}"
          f"   span off {h_off:+.1f}/{v_off:+.1f} px")
    print(f"  line    {total_px:7.1f} px (file {file_total}, {total_px - file_total:+.1f})"
          f"   clock {total_px * geo['lineRateHz'] / 1e6:7.3f} MHz"
          f" (file {clock / 1e6:.3f})   {units_px:.5f} units/px")
    print(f"  frame   {frame_lines} lines (file {file_vtotal},"
          f" {frame_lines - file_vtotal:+d})"
          f"   sync {field['STATUS_SYNC_PROC_HLOW_LEN'] / doubling / units_px:5.1f} px"
          f" (file {h[0]})   rt_hs {field['SP_RT_HS_ST']}..{field['SP_RT_HS_SP']}"
          f"   hbin {field['IF_HBIN_SP']}..{field['IF_HBIN_ST']}")

    # The four blanking regions, each against what the file states. The card is
    # drawn on the framebuffer and the border sits outside it, so the file's
    # leading run is sync + back porch + border.
    #
    # The two counters take their origins from different edges of their pulse.
    # The line counter's is the hsync LEADING edge, which is the edge a video
    # standard counts from, so the whole pulse is in the lead. The frame
    # counter's is the vsync TRAILING edge, so the pulse is not -- it falls at
    # the far end, after the front porch.
    measured = [left / units_px, (right - left) / units_px + 1.0,
                total_px - right / units_px - 1.0,
                top, bottom - top + 1.0, frame_lines - bottom - 1.0]
    filed = [h[0] + h[1] + h[2], active_px, h[4] + h[5],
             v[1] + v[2], active_ln, v[4] + v[5] + v[0]]
    names = ["h lead", "h active", "h trail", "v lead", "v active", "v trail"]
    print("  margins " + "  ".join(
        f"{name} {got:.1f}/{want} ({got - want:+.1f})"
        for name, got, want in zip(names, measured, filed)))
    print(f"  frame   left {across[0]:6.1f} px  right"
          f" {clip.shape[2] - 1 - across[1]:6.1f} px"
          f"   top {down[0]:5.1f} px  bottom {clip.shape[1] - 1 - down[1]:5.1f} px"
          f"   of {clip.shape[2]}x{clip.shape[1]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--modeserv", default=MODESERV)
    parser.add_argument("--modes", required=True)
    parser.add_argument("--repeat", type=int, default=1)
    args = parser.parse_args()

    dev = hdmi_capture.device()
    for mode in [m.strip() for m in args.modes.split(",")]:
        found = stated(mode)
        if found is None:
            print(f"{mode}: no entry in the mode file")
            continue
        h, v, clock = found
        reply = gbs_unit.mode_serv(args.modeserv, f"MODE {mode}")
        if not reply or not reply.startswith("OK"):
            print(f"{mode}: source refused it: {reply!r}")
            continue
        if not card_edges.settled(args.host):
            print(f"{mode}: never acquired, or never stopped re-solving")
            continue
        gbs_unit.mode_serv(args.modeserv, "PATTERN CARD")
        if open_wide(args.host) is None:
            print(f"{mode}: the framing would not open")
            continue
        for _ in range(args.repeat):
            time.sleep(2.0)
            geo = gbs_unit.get_json(args.host, "/geometry")[1]
            field = gbs_unit.read_fields(args.host, FIELDS)
            clip = hdmi_capture.frames(card_edges.CLIP_FRAMES, dev)
            report(mode, field, geo, clip, h, v, clock)
        print(flush=True)


if __name__ == "__main__":
    main()
