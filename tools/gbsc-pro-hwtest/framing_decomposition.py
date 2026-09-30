"""Attribute the black at each edge of the emitted frame to the stage that owns it.

Five positions per edge, all in the output raster's own units (display-clock
cycles, `T = VDS_HSYNC_RST + 1`):

    E   the encoder's window, MEASURED off the emitted frame
    Em  the encoder's window as OutputMode::solve() models it
    A   our aperture, VDS_DIS_HB_SP..ST
    P   the written picture, from the write model and the scale
    C   the card's green border, read off the frame and converted

and the black at the left edge is an identity whose terms each have one owner:

    C0 - E0 = (C0 - P0)   capture default / SourceTiming row
            + (P0 - A0)   write-start constants, scale rounding, parity
            + (A0 - E0m)  aperture not where the engine's own model put it
            + (E0m - E0)  the model of the encoder is wrong

The output side is a port of OutputMode::solve() and OutputMode::horizontal-
TotalFor(), pinned by test_framing_decomposition.py to the values
test/test_output_mode.cpp asserts. It is the PREDICTED column of a record, not
a specification: when a constant moves in the C++ it moves here in the same
commit. src/tv5725/OutputMode.cpp, src/tv5725/OutputWindow.cpp.
"""

from collections import namedtuple

# OutputMode.h
WORKING_CEILING_HZ = 129_600_000
ENGINE_CEILING_HZ = 108_000_000
ENCODER_CEILING_HZ = 165_000_000
MAX_HORIZONTAL_TOTAL = 2450
FRONT_PORCH_MIN_PX = 16
TRANSMITTED_WINDOW_DELAY_PX = 20
HSYNC_START_PX = 13
HORIZONTAL_TOTAL_MAX = 4096

# DisplayClock.cpp, ascending
SEEDS_HZ = (40_500_000, 54_000_000, 64_800_000, 81_000_000,
            108_000_000, 129_600_000, 162_000_000)

# OutputWindow.cpp: the write start, VDS_?B_SP + constant + perMagnification x m
WRITE_START_H = (55.0, 25.0)
WRITE_START_V = (0.2, 0.8)
CAPTURE_MARGIN_H = 1
CAPTURE_MARGIN_V = 2
SCALE_UNITY = 1024

# The dongle's frame, whatever the HDMI mode: it rescales to this.
CAPTURE_WIDTH, CAPTURE_HEIGHT = 1920, 1080

Mode = namedtuple("Mode", "active_lines sync_px back_porch_px active_px carried_px "
                          "total_px standard_hz vsync v_back_porch v_front_porch")

# OutputMode.cpp's table, one row per mode, in its argument order.
MODES = {
    "1080p": Mode(1080, 44, 148, 1920, 1920, 2200, 148_500_000, 5, 36, 4),
    "1024p": Mode(1024, 112, 248, 1280, 1280, 1688, 108_000_000, 3, 38, 1),
    "960p":  Mode(960, 112, 312, 1280, 1280, 1800, 108_000_000, 3, 36, 1),
    "720p":  Mode(720, 40, 220, 1280, 1280, 1650, 74_250_000, 5, 20, 5),
    "576p":  Mode(600, 64, 68, 720, 679, 864, 27_000_000, 5, 19, 1),
    "480p":  Mode(480, 62, 60, 720, 690, 858, 27_000_000, 6, 30, 9),
}

# The HDMI active height the encoder emits for each mode, which the dongle
# stretches to CAPTURE_HEIGHT. Ours for the four the campaign runs; the two SD
# modes are unverified.
HDMI_ACTIVE_LINES = {"1080p": 1080, "1024p": 1024, "960p": 960, "720p": 720,
                     "576p": 576, "480p": 480}


def frame_lines(mode):
    return mode.active_lines + mode.v_front_porch + mode.vsync + mode.v_back_porch


def carried_output(registers):
    """The mode whose frame the registers carry, or None. The engine falls back
    from the mode asked to one the encoder can transmit at the source's field
    rate, and the registers are the only record of which."""
    if registers.get("VDS_VSYNC_RST") is None:
        return None
    lines = registers["VDS_VSYNC_RST"] + 1
    for name, mode in MODES.items():
        if frame_lines(mode) == lines:
            return name
    return None


def field_rate_of(registers, geometry):
    return geometry["lineRateHz"] / (registers["STATUS_SYNC_PROC_VTOTAL"] + 1)


def horizontal_total_for(hz, lines, field_rate):
    if hz == 0 or lines == 0 or field_rate <= 0.0:
        return 0
    per_line = (hz / field_rate) / lines
    if per_line < 1.0:
        return 0
    total = int(per_line)
    total += total % 2
    return 0 if total > HORIZONTAL_TOTAL_MAX else total


def clock_for(output, field_rate, ceiling_hz=ENGINE_CEILING_HZ):
    lines = frame_lines(MODES[output])
    best = 0
    for hz in SEEDS_HZ:
        if hz > ceiling_hz:
            continue
        total = horizontal_total_for(hz, lines, field_rate)
        if total == 0 or total > MAX_HORIZONTAL_TOTAL:
            continue
        if hz >= best:
            best = hz
    return best


def predicted_window(output, total, field_rate):
    """Where OutputMode::solve() puts the encoder's window inside a line of
    `total` units running at `field_rate` -- the raster the engine solved,
    VDS_HSYNC_RST + 1, rather than one re-solved here."""
    mode = MODES[output]
    lines = frame_lines(mode)
    clock_hz = total * lines * field_rate

    def scaled(standard_px):
        return max(0, int(round(standard_px * clock_hz / mode.standard_hz)))

    width = max(1, scaled(mode.sync_px))
    porch = scaled(mode.back_porch_px)
    span = (total * mode.carried_px) // mode.total_px
    last_usable = total - FRONT_PORCH_MIN_PX
    start = width + porch + TRANSMITTED_WINDOW_DELAY_PX
    if start + span > last_usable:
        start = last_usable - span
    if start < width:
        start = width
    stop = max(min(start + span, last_usable), start)
    return dict(T=total, clock_hz=clock_hz,
                hsync=(HSYNC_START_PX, HSYNC_START_PX + width),
                E0m=start, E1m=stop,
                V0m=mode.vsync + mode.v_back_porch,
                V1m=lines - mode.v_front_porch)


def solve_raster(output, field_rate, ceiling_hz=ENGINE_CEILING_HZ):
    """The whole raster the engine would solve for this output at this rate, or
    None where the encoder cannot carry it or no seed reaches it."""
    mode = MODES[output]
    lines = frame_lines(mode)
    if int(mode.total_px * lines * field_rate + 0.5) > ENCODER_CEILING_HZ:
        return None
    hz = clock_for(output, field_rate, ceiling_hz)
    if hz == 0:
        return None
    total = horizontal_total_for(hz, lines, field_rate)
    if total == 0:
        return None
    raster = predicted_window(output, total, field_rate)
    raster["seed_hz"] = hz
    return raster


def slope_predicted(output, total):
    """(dongle columns per raster unit, dongle rows per raster line).

    The encoder resamples our line into the mode's total and emits its carried
    fraction, which the dongle stretches to its own frame -- so column 0 is the
    window's start and the whole window is CAPTURE_WIDTH columns wide.
    """
    mode = MODES[output]
    window_units = total * mode.carried_px / mode.total_px
    return (CAPTURE_WIDTH / window_units,
            CAPTURE_HEIGHT / HDMI_ACTIVE_LINES[output])


def columns_to_units(column, e0, slope):
    """A dongle column as a raster unit, through the MEASURED window start --
    never through the aperture, which is what the 1080p tools assumed and is
    the confound docs/investigations/the-transmitted-window-is-latched-from-our-
    blanking.md names."""
    return e0 + column / slope


def units_to_columns(unit, e0, slope):
    return (unit - e0) * slope


def _axis_positions(total_reg, scale_reg, memory_stop, aperture, capture_stop,
                    capture_start, margin, extent_asked, write_start):
    magnification = SCALE_UNITY / scale_reg
    constant, per_magnification = write_start
    write = memory_stop + constant + per_magnification * magnification
    span = capture_start - capture_stop
    return dict(T=total_reg + 1, m=magnification, A0=aperture[0], A1=aperture[1],
                W0=write, P0=write + margin * magnification,
                P1=write + (span - margin) * magnification,
                capture_span=span, asked=extent_asked, clamped=span - 2 * margin < extent_asked)


def positions(regs, geometry):
    """The register-only positions per axis: the raster total, the
    magnification, the aperture A, the write start W0 and the picture P.

    P is the register pair less the axis's capture margin at each end, placed
    by the write model -- registers only, because /geometry's `oh`/`ov` are the
    framing's ASK against the counter's own origin, which is not the register's.
    The ask is carried beside it and `clamped` says when the chip got less than
    was asked (docs/investigations/the-raster-bound-is-stored-as-a-proportion.md).
    """
    return dict(
        h=_axis_positions(regs["VDS_HSYNC_RST"], regs["VDS_HSCALE"], regs["VDS_HB_SP"],
                          (regs["VDS_DIS_HB_SP"], regs["VDS_DIS_HB_ST"]),
                          regs["IF_HB_SP2"], regs["IF_HB_ST2"],
                          CAPTURE_MARGIN_H, geometry["eh"], WRITE_START_H),
        v=_axis_positions(regs["VDS_VSYNC_RST"], regs["VDS_VSCALE"], regs["VDS_VB_SP"],
                          (regs["VDS_DIS_VB_SP"], regs["VDS_DIS_VB_ST"]),
                          regs["IF_VB_SP"], regs["IF_VB_ST"],
                          CAPTURE_MARGIN_V, geometry["ev"], WRITE_START_V))


TERMS = ("dCapture", "dPlace", "dModelApplied", "dEncoder")

# Black an edge may carry and still be flush, in dongle columns: (near, far).
# The far edge carries the memory-window parity workaround's one column.
ALLOWANCE_COLS = (1, 2)


def _difference(a, b):
    return None if a is None or b is None else a - b


def _edge(terms, black_units, slope, allowance, clipped):
    known = {k: v for k, v in terms.items() if v is not None}
    if black_units is None:
        verdict = "unmeasured"
    elif abs(black_units) * slope <= allowance:
        verdict = "clipped" if clipped else "flush"
    else:
        verdict = max(known, key=lambda k: abs(known[k]))
        if clipped:
            verdict += "+clip"
    return dict(terms, black_units=black_units,
                black_cols=None if black_units is None else black_units * slope,
                clipped=clipped, verdict=verdict)


def _black_units(identity, counted_cols, slope):
    """The black between the card's edge and the window's: the identity where
    the border is on the frame, the frame's own count where it is not -- a
    border off the frame is clipped, and the black beside it is still
    somebody's."""
    if identity is not None:
        return identity
    return None if counted_cols is None else counted_cols / slope


def decompose(pos, window, content, slope, allowance):
    """The four terms at each end of one axis, and a verdict per edge.

    `window` carries the measured E0/E1 and the modelled E0m/E1m; a missing
    measurement leaves dEncoder None and the edge is judged against the model.
    `content` carries the card's edges C0/C1, None where a border is off the
    frame, and may carry black0/black1, the columns counted black at each edge
    of the frame, which is what the black is when the border cannot be seen.
    `allowance` is (near, far) in dongle columns -- the far edge carries the
    parity workaround's one column. Positive terms are black, negative clip.
    """
    e0 = window["E0"] if window.get("E0") is not None else window["E0m"]
    e1 = window["E1"] if window.get("E1") is not None else window["E1m"]
    near = dict(dCapture=_difference(content.get("C0"), pos["P0"]),
                dPlace=pos["P0"] - pos["A0"],
                dModelApplied=pos["A0"] - window["E0m"],
                dEncoder=_difference(window["E0m"], window.get("E0")))
    far = dict(dCapture=_difference(pos["P1"], content.get("C1")),
               dPlace=pos["A1"] - pos["P1"],
               dModelApplied=window["E1m"] - pos["A1"],
               dEncoder=_difference(window.get("E1"), window["E1m"]))
    c0, c1 = content.get("C0"), content.get("C1")
    near_black = _black_units(_difference(c0, e0), content.get("black0"), slope)
    far_black = _black_units(_difference(e1, c1), content.get("black1"), slope)
    return dict(near=_edge(near, near_black, slope, allowance[0], c0 is None),
                far=_edge(far, far_black, slope, allowance[1], c1 is None))


def judge(pos, window, slope, black):
    """The four terms at every edge and the verdict, from what a state measured:
    the positions, the window (measured and modelled), the slope per axis and
    the black counted at each edge of the frame."""
    residuals = dict(
        h=decompose(pos["h"], dict(E0=window["E0"], E1=window["E1"],
                                   E0m=window["E0m"], E1m=window["E1m"]),
                    dict(C0=pos["h"]["C0"], C1=pos["h"]["C1"],
                         black0=black["left"], black1=black["right"]),
                    slope["h_pred"], ALLOWANCE_COLS),
        v=decompose(pos["v"], dict(E0=window["V0"], E1=window["V1"],
                                   E0m=window["V0m"], E1m=window["V1m"]),
                    dict(C0=pos["v"]["C0"], C1=pos["v"]["C1"],
                         black0=black["top"], black1=black["bottom"]),
                    slope["v_pred"], (ALLOWANCE_COLS[0], ALLOWANCE_COLS[0])))
    return residuals, verdict_of(residuals)


def remodel(record):
    """A record's model, slope and card positions re-derived for the mode its
    registers carry, where that is not the mode it was modelled as."""
    carried = carried_output(record["registers"])
    if carried is None or carried == record.get("carried", record["output"]):
        record.setdefault("carried", record["output"])
        return
    regs, geometry, window = record["registers"], record["geometry"], record["window"]
    total = regs["VDS_HSYNC_RST"] + 1
    window.update(predicted_window(carried, total, field_rate_of(regs, geometry)))
    cols, rows = slope_predicted(carried, total)
    record["slope"].update(h_pred=cols, v_pred=rows)
    pos, card = record["positions"], record["card"]
    e0 = window["E0"] if window.get("E0") is not None else window["E0m"]
    v0 = window["V0"] if window.get("V0") is not None else window["V0m"]
    pos["h"].update(C0=None if card["h"][0] is None else columns_to_units(card["h"][0], e0, cols),
                    C1=None if card["h"][1] is None else columns_to_units(card["h"][1], e0, cols))
    pos["v"].update(C0=None if card["v"][0] is None else columns_to_units(card["v"][0], v0, rows),
                    C1=None if card["v"][1] is None else columns_to_units(card["v"][1], v0, rows))
    record["carried"] = carried


def rejudge(record):
    """A recorded state judged again by the decomposition as it stands, off
    the raw fields the record keeps, so a run is not stuck with the verdicts
    it was written under."""
    return judge(record["positions"], record["window"], record["slope"], record["black_cols"])


def verdict_of(residuals):
    edges = (("L", residuals["h"]["near"]), ("R", residuals["h"]["far"]),
             ("T", residuals["v"]["near"]), ("B", residuals["v"]["far"]))
    if all(edge["verdict"] == "flush" for _, edge in edges):
        return "ok"
    return " ".join(f"{name}:{edge['verdict']}" for name, edge in edges
                    if edge["verdict"] != "flush")


# --- reading the frame ------------------------------------------------------------

# Limited-range black is 16; hdmi_capture.BLACK, restated so this module needs
# no ffmpeg to import.
BLACK = 24


def edge_profiles(grey, depth=96):
    """The mean luma of the outermost `depth` columns or rows at each edge, each
    running INWARD from its edge, so index 0 is the frame's edge."""
    columns = grey.mean(axis=0)
    rows = grey.mean(axis=1)
    return dict(left=columns[:depth], right=columns[::-1][:depth],
                top=rows[:depth], bottom=rows[::-1][:depth])


def black_extent(profile, threshold=BLACK):
    """How many positions from the edge are blanking-black, stopping at the
    first that is not. A fetch ramp or unwritten memory is not black."""
    count = 0
    for level in profile:
        if level > threshold:
            break
        count += 1
    return count


# A step's black count has to move by more than the capture's own noise to be
# called a move; the card's edge dithers a column.
WALK_NOISE_COLS = 2


def walk_signature(steps):
    """What the black at the edge did as our aperture was walked INTO the
    picture: `grows` means the aperture is inside the encoder's window and is
    blanking it, `shrinks` that it opened before the picture, `constant` that
    the black is the source's own blanking, captured."""
    counts = [black for _, black in steps]
    if len(counts) < 2:
        return "constant"
    first, last = counts[0], counts[-1]
    if last - first > WALK_NOISE_COLS:
        return "grows"
    if first - last > WALK_NOISE_COLS:
        return "shrinks"
    return "constant"


def card_columns(clip, axis, expected_span):
    """The card's green border at both ends of `axis`, as sub-column centroids
    off the clip, or None for an edge that is not on the frame."""
    import full_margins
    edges, _runs, residual = full_margins.green_edges(clip, axis, expected_span)
    if edges is None:
        return None, None, None
    if edges[1] is None and edges[0] >= clip.shape[1 + axis] / 2.0:
        return None, edges[0], None
    return edges[0], edges[1], residual


def slope_from_pan(shift_cols, pan_units, magnification):
    """Dongle columns per raster unit, from a pan of `pan_units` capture units
    that moved the picture `shift_cols` columns."""
    return shift_cols / (pan_units * magnification)


WALK_EDGES = (("E0", "h_near"), ("E1", "h_far"), ("V0", "v_near"), ("V1", "v_far"))


def measured_window(walked):
    """E0/E1/V0/V1 off the aperture walks: the differenced strips where they
    found the blanking, the black count where they did not -- and which one
    answered, per edge."""
    window, instruments = {}, {}
    for edge, walk in WALK_EDGES:
        got = walked.get(walk) or {}
        if got.get("strip_zero") is not None:
            window[edge], instruments[edge] = got["strip_zero"], "strip"
        elif got.get("black_zero") is not None:
            window[edge], instruments[edge] = got["black_zero"], "black"
        else:
            window[edge], instruments[edge] = None, None
    return window, instruments
