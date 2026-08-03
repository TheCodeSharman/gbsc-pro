"""geometry_math against the register sets that were photographed working.

No hardware. The four SOLVED-* snapshots are states that were on screen, clean
and full width, at the end of the 2026-08-03 session; the artefacting ones are
recorded too. If the arithmetic cannot reproduce what those registers do, it is
wrong, and no amount of bench time will make it right.
"""

import glob
import json
import os

import pytest

import geometry_math as gm

HERE = os.path.dirname(os.path.abspath(__file__))
SNAPSHOTS = os.path.join(HERE, "snapshots")


def field(config, segment, register, offset, width):
    """A field out of a snapshot's flat {"seg:reg": value} map."""
    span = (offset + width + 7) // 8
    raw = 0
    for index in range(span):
        byte = config.get(f"{segment}:{register + index:02x}")
        if byte is None:
            return None
        raw |= byte << (8 * index)
    return (raw >> offset) & ((1 << width) - 1)


def geometry_of(path):
    """The horizontal geometry a snapshot describes, or None if it predates the
    segment 1 capture (a few early dumps only cover the preset ranges)."""
    with open(path) as handle:
        loaded = json.load(handle)
    config = loaded.get("config", loaded)

    line_units = field(config, 1, 0x0E, 0, 11)
    sp2 = field(config, 1, 0x1A, 0, 11)
    st2 = field(config, 1, 0x18, 0, 11)
    if None in (line_units, sp2, st2):
        return None

    capture = st2 - sp2
    if capture < 0:  # the window wraps the end of the line
        capture += line_units + 1

    return {
        "name": os.path.basename(path),
        "capture": capture,
        "hscale": field(config, 3, 0x16, 0, 10),
        "bypassed": bool(field(config, 3, 0x00, 4, 1)),
        "line_px": field(config, 3, 0x01, 0, 12) + 1,
        "memory": field(config, 3, 0x04, 0, 12) - field(config, 3, 0x05, 4, 12),
        "display": field(config, 3, 0x10, 0, 12) - field(config, 3, 0x11, 4, 12),
    }


def load(pattern):
    found = []
    for path in sorted(glob.glob(os.path.join(SNAPSHOTS, pattern))):
        geometry = geometry_of(path)
        if geometry and geometry["capture"] > 0 and geometry["hscale"]:
            found.append(geometry)
    return found


SOLVED = load("SOLVED-*.json")
TUNED = load("*2026-08-03*.json")


def ids(cases):
    return [case["name"] for case in cases]


# --- the analyse direction ---------------------------------------------------


def test_snapshots_were_found():
    """A glob that quietly matches nothing would make every test below vacuous."""
    assert SOLVED, "no SOLVED-* snapshots with segment 1 captured"


@pytest.mark.parametrize("case", SOLVED, ids=ids(SOLVED))
def test_solved_states_have_the_headroom_the_rule_demands(case):
    """These were clean on screen. If the rule says they should not have been,
    the rule is wrong -- this is the check that keeps the constants honest."""
    produced = gm.produced_px(case["capture"], case["hscale"], case["bypassed"])
    headroom = gm.headroom_px(case["memory"], produced)
    assert headroom >= gm.HEADROOM_MIN_PX, (
        f"{case['name']} was photographed clean but has only {headroom:.1f} px "
        f"of headroom, below the {gm.HEADROOM_MIN_PX} px that artefacted"
    )


@pytest.mark.parametrize("case", SOLVED, ids=ids(SOLVED))
def test_produced_width_fits_the_line(case):
    produced = gm.produced_px(case["capture"], case["hscale"], case["bypassed"])
    assert 0 < produced <= case["line_px"]


def test_magnification_reports_a_dropped_read_rather_than_guessing():
    """HSCALE 0 is a dropped I2C read, not 1:1. Returning 1.0 there would give a
    plausible width that is wrong, which is worse than refusing."""
    assert gm.magnification(0) is None
    assert gm.magnification(0, bypassed=True) == 1.0


# --- the generative direction ------------------------------------------------


@pytest.mark.parametrize("case", SOLVED, ids=ids(SOLVED))
def test_solve_reproduces_the_hscale_that_was_on_screen(case):
    """Round trip: ask for exactly the width this snapshot produces and the solve
    should return the HSCALE it used. Allow one count -- HSCALE is an integer
    divisor, so neighbouring values differ by a fraction of a pixel."""
    produced = gm.produced_px(case["capture"], case["hscale"], case["bypassed"])
    result = gm.solve_horizontal(case["capture"], produced, case["line_px"])
    assert abs(result["hscale"] - case["hscale"]) <= 1, (
        f"{case['name']}: asked for {produced:.1f} px from {case['capture']} "
        f"capture units, solve chose HSCALE {result['hscale']} but the working "
        f"state used {case['hscale']}"
    )


@pytest.mark.parametrize("case", TUNED, ids=ids(TUNED))
def test_solve_never_returns_an_unsafe_register_set(case):
    """Whatever is asked for, the result honours the headroom rule or says it
    could not. This is the invariant the firmware will rely on."""
    result = gm.solve_horizontal(case["capture"], case["line_px"], case["line_px"])
    if result["safe"]:
        assert result["min_memory_window_px"] - result["produced_px"] >= gm.HEADROOM_MIN_PX
    else:
        assert result["clamped"], "unsafe result with no explanation"


@pytest.mark.parametrize("case", SOLVED, ids=ids(SOLVED))
def test_a_working_memory_window_is_never_narrowed(case):
    """These windows were on screen and clean, with far more slack than the rule
    demands. Applying the bare minimum would narrow them into territory nothing
    has been measured in, so ensure_memory_window() must only ever widen.

    One of these -- mode13-fullscreen-clean, at 16.85 px -- sits below
    HEADROOM_SAFE_PX, which is the margin added above the tightest state ever
    seen clean. It is correct for that one to be widened slightly.
    """
    produced = gm.produced_px(case["capture"], case["hscale"], case["bypassed"])
    result = gm.solve_horizontal(case["capture"], produced, case["line_px"])
    kept = gm.ensure_memory_window(
        case["memory"], result["min_memory_window_px"], case["line_px"]
    )
    assert kept >= case["memory"], "a working window was narrowed"
    assert kept - produced >= gm.HEADROOM_MIN_PX


def test_a_too_narrow_memory_window_is_widened():
    result = gm.solve_horizontal(826, 1271.92, 1445)
    widened = gm.ensure_memory_window(1000, result["min_memory_window_px"], 1445)
    assert widened == result["min_memory_window_px"]
    assert widened - result["produced_px"] >= gm.HEADROOM_MIN_PX


def test_memory_window_never_exceeds_the_line():
    assert gm.ensure_memory_window(9999, 1292, 1445) == 1445


def test_solve_raises_hscale_rather_than_overflowing_the_line():
    """Asking for a picture wider than the line is the corruption case. The solve
    must shrink it, not hand back the impossible number."""
    result = gm.solve_horizontal(900, 2000, 1445)
    assert result["produced_px"] <= 1445 - gm.HEADROOM_MIN_PX
    assert result["clamped"]


def test_solve_refuses_a_line_too_short_to_hold_headroom():
    with pytest.raises(ValueError):
        gm.solve_horizontal(900, 100, gm.HEADROOM_SAFE_PX)


# --- the starting-point rule -------------------------------------------------


def test_default_vertical_fraction_splits_on_field_rate():
    """50 Hz sources carry the same active height in a longer frame, so they have
    far more vertical blanking. One constant would be wrong for one of them."""
    _, pal = gm.default_active_fractions(50.0)
    _, ntsc = gm.default_active_fractions(60.0)
    assert pal < ntsc


def test_default_capture_window_over_captures():
    """Biased to show black edges rather than crop. A cropped edge looks like a
    tuning fault and sends you hunting for a problem that is not there."""
    line = 1277
    start, stop = gm.default_capture_window(line, 50.0)
    assert (stop - start) > line * gm.DEFAULT_H_ACTIVE_FRACTION
    assert 0 <= start < stop <= line


def test_default_capture_window_stays_inside_the_line():
    for line in (400, 1277, 2047):
        for rate in (50.0, 60.1):
            start, stop = gm.default_capture_window(line, rate)
            assert 0 <= start < stop <= line
