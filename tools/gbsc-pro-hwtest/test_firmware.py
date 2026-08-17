"""Regression tests for this fork's firmware changes, run against a live unit.

    pytest --host=gbscontrol.local
    pytest --host=192.168.1.20 --preset-save

Covers the /getreg and /setreg endpoints, printVideoTimings() reaching the web
console, the preset save path, and the external clock generator's display-clock
stash. The timings tests need a GBS_DEBUG=1 build; they fail rather than
skip if the console is silent, because a silent console is the regression they
exist to catch.

The display-clock tests read the firmware source rather than the unit, so they
run with no hardware attached.
"""

import re
import time
from pathlib import Path

import pytest

from gbs_unit import (
    field_from,
    get,
    get_json,
    parse_timings,
    read_reg,
    read_field,
    read_segment,
    fs_dir,
    fs_read,
    wait_for,
    write_reg,
)

# A register that exists in every mode and is safe to write: VDS_HSYNC_RST's low
# byte. In HD bypass the VDS block is not driving the output at all, and in a
# scaling preset the guard fixture puts it straight back.
SAFE_SEGMENT, SAFE_REGISTER = 3, 0x01

PRESET_VALUE_COUNT = 432  # see presetRegisterRanges[] in gbs-control.ino

# Sync processor state, all reachable through /getreg.
STATUS_16 = (0, 0x16)  # what every "is there sync?" test in the firmware reads
HS_ACTIVE, VS_ACTIVE = 1 << 1, 1 << 3
# The lock signal is HSACT ALONE, which is what the firmware itself waits on:
# getStatus16SpHsStable() checks `status16 & 0x02` and nothing else.
#
# **VSACT MUST NOT BE PART OF IT.** It reports the sync path, not the lock:
# measured 2026-08-13 on the csync path (SP_SOG_MODE 1) it read low in 3761 of
# 3761 samples over 90 s with HSACT high throughout and VTOTAL a steady 308,
# and measured 2026-08-15 on the separate-sync path it read high in 150 of 150
# with VTOTAL 311. Both of its values occur with a perfect picture, so including
# it makes the hold loop below time out in its first assert and never run its own
# body. See CLAUDE.md.
LOCKED = HS_ACTIVE

SYNC_PROC_00 = (5, 0x20)
EXT_SYNC_SEL = 1 << 3  # take V sync off the HS line instead of the VS input

PLLAD_LOCK = 1 << 7  # seg 0 0x09
# HPERIOD_IF .. VTOTAL: everything the sync check correlates against, in one burst
SYNC_SAMPLE_RANGE = (0x06, 0x1C)

LOCK_TIMEOUT = 45.0  # the firmware's own RGBHV retry loop is slow
HOLD_SECONDS = 15.0  # long enough to catch a lock the sync watcher takes back

# Console label -> the registers behind each printed column, as
# (segment, low byte, bit offset, width), copied from the UReg declarations in
# Tv5725.h. Both the scaling and the HD bypass blocks are here so the cross-check
# runs in either mode.
#
# The offsets matter: VDS_VSYNC_RST and VDS_VSCALE start at bit 4, sharing their
# low byte with the field below. Reading them mask-only returns a wrong number
# that still looks like a plausible timing value — 1818 for a VDS_VSYNC_RST of
# 625 — so the cross-check reported a firmware disagreement that was its own.
TIMING_REGISTERS = {
    "HT / scale": [(3, 0x01, 0, 12), (3, 0x16, 0, 10)],  # VDS_HSYNC_RST, VDS_HSCALE
    "VT / scale": [(3, 0x02, 4, 11), (3, 0x17, 4, 10)],  # VDS_VSYNC_RST, VDS_VSCALE
    "HD_HSYNC_RST": [(1, 0x37, 0, 11)],
    "HD_INI_ST": [(1, 0x39, 0, 11)],
}

# printVideoTimings() prints from an async handler while the firmware is also
# hunting for sync, so a single collect() window intermittently catches nothing —
# or catches the block half-written, which is worse, because a partial capture
# looks like a successful read of a firmware that printed less than it should.
TIMINGS_ATTEMPTS = 4
TIMINGS_MIN_ROWS = 5
TIMINGS_COLLECT_SECONDS = 2.5


# --- /getreg and /setreg ----------------------------------------------------


def test_getreg_reads_a_register(host):
    status, payload = get_json(host, "/getreg?s=3&r=0x01")

    assert status == 200, f"/getreg returned {status}"
    assert payload["segment"] == 3
    assert payload["register"] == "0x01"
    assert re.fullmatch(r"0x[0-9a-f]{2}", payload["value"]), payload


def test_getreg_reads_registers_as_hex_not_decimal(host):
    """r=11 must mean register 0x11. Parsed as decimal it would be 0x0b — a
    different register, silently."""
    status_bare, bare = get_json(host, "/getreg?s=3&r=11")
    status_prefixed, prefixed = get_json(host, "/getreg?s=3&r=0x11")

    assert (status_bare, status_prefixed) == (200, 200)
    assert bare["register"] == "0x11"
    assert prefixed["register"] == "0x11"


@pytest.mark.parametrize(
    "path",
    [
        "/getreg",
        "/getreg?s=3",
        "/getreg?r=0x01",
        "/getreg?s=6&r=0x01",  # segment above 5
        "/getreg?s=3&r=100",  # register above 0xff
        "/getreg?s=3&r=zz",
        "/getreg?s=3&r=",
        "/getreg?s=-1&r=0x01",
        "/setreg?s=3&r=0x01",  # missing value
        "/setreg?s=3&r=0x01&v=100",
        "/setreg?s=3&r=0x01&v=zz",
    ],
)
def test_malformed_register_requests_are_rejected(host, path):
    """Anything missing or out of range must be a 400, not a poke at whatever
    register the parse happened to land on."""
    status, _ = get(host, path)

    assert status == 400, f"{path} returned {status}, expected 400"


def test_setreg_reports_the_previous_value(host, register_guard):
    """Writing a register the value it already holds proves the round trip
    without changing anything."""
    before = register_guard(SAFE_SEGMENT, SAFE_REGISTER)

    payload = write_reg(host, SAFE_SEGMENT, SAFE_REGISTER, before)

    assert payload is not None, "/setreg did not return 200"
    assert int(payload["was"], 16) == before
    assert int(payload["value"], 16) == before


def test_setreg_changes_a_register(host, register_guard):
    """The read-back in the response must reflect the write. register_guard
    restores the original even if this fails."""
    before = register_guard(SAFE_SEGMENT, SAFE_REGISTER)
    probe = (before ^ 0x01) & 0xFF

    payload = write_reg(host, SAFE_SEGMENT, SAFE_REGISTER, probe)

    assert payload is not None, "/setreg did not return 200"
    assert int(payload["was"], 16) == before
    assert int(payload["value"], 16) == probe
    assert read_reg(host, SAFE_SEGMENT, SAFE_REGISTER) == probe


# --- /getregs ---------------------------------------------------------------


def test_getregs_returns_an_inclusive_range(host):
    status, payload = get_json(host, "/getregs?s=3&from=00&to=05")

    assert status == 200, f"/getregs returned {status}"
    assert (payload["segment"], payload["from"], payload["to"]) == (3, 0, 5)
    assert re.fullmatch(r"[0-9a-f]{12}", payload["values"]), payload  # six bytes, not five


def test_getregs_agrees_with_getreg(host):
    """The whole point of the endpoint is fewer round trips for the same bytes.

    Reads segment 3, output configuration, as the steadiest thing available --
    but "steadiest" is not "still". runAutoBestHTotal() writes VDS timings while
    the unit is running, so a register that moves between the burst read and the
    single read is the firmware doing its job, not the endpoint disagreeing.
    Scoring that as a failure made this flap intermittently (2026-08-05).

    So a candidate mismatch is confirmed by re-reading: it only counts if the
    register is stable across two single reads and *still* differs from the
    burst. A register caught mid-move is discarded rather than reported.
    """
    registers = read_segment(host, 3, 0x00, 0x0F)
    assert registers is not None, "/getregs did not answer with a usable range"

    mismatched = {}
    for reg, burst in registers.items():
        if burst == read_reg(host, 3, reg):
            continue
        # Candidate. Two more reads: if they disagree with each other the
        # register is moving, which says nothing about /getregs.
        first, second = read_reg(host, 3, reg), read_reg(host, 3, reg)
        if first == second and first != burst:
            mismatched[f"s3:{reg:#04x}"] = (burst, first)

    assert not mismatched, (
        f"/getregs disagrees with /getreg on registers that are holding still "
        f"(burst, single): {mismatched}"
    )


@pytest.mark.parametrize(
    "path",
    [
        "/getregs",  # no segment
        "/getregs?s=6",  # segment above 5
        "/getregs?s=3&from=zz",
        "/getregs?s=3&to=100",  # register above 0xff
        "/getregs?s=3&from=10&to=05",  # to below from
    ],
)
def test_malformed_getregs_requests_are_rejected(host, path):
    status, _ = get(host, path)

    assert status == 400, f"{path} returned {status}, expected 400"


# --- printVideoTimings() ----------------------------------------------------


def _collect_timings(host, console, attempts=TIMINGS_ATTEMPTS, minimum=TIMINGS_MIN_ROWS):
    """Ask for the timings block until the console answers with a whole one.

    Returns the best attempt as (rows, lines). Retried, and retried on a *partial*
    block rather than only on silence, because the print races the firmware's own
    sync hunting: one window in a full run comes back empty, and another comes
    back with two rows of a block that has more. Accepting the partial one would
    report the firmware printing less than it does.

    A console that stays silent, or never completes the block, across every
    attempt still fails — that is the regression these tests exist to catch, and
    it is left to the caller to assert on.
    """
    best_rows, best_lines = {}, []
    for _ in range(attempts):
        console.drain()
        get(host, "/sc?,")
        lines = console.collect(seconds=TIMINGS_COLLECT_SECONDS)
        rows = parse_timings(lines)
        if len(rows) > len(best_rows):
            best_rows, best_lines = rows, lines
        if len(rows) >= minimum:
            break
        if not console.alive:
            break  # retrying a dead socket only wastes the timeout
    return best_rows, best_lines


def _console_diagnosis(console):
    """Why a collection came back short. A dropped WebSocket and a silent
    firmware look identical from the collect() side, and only one of them is a
    firmware regression."""
    if console.alive:
        return "the console is connected, so the firmware really did print this little"
    return (
        f"the WebSocket dropped ({console.failure}), so this says nothing about the "
        "firmware. Each client costs heap: close the web UI and regpanel.py, then "
        "re-run"
    )


@pytest.fixture
def timings(host, console):
    """The block /sc?, prints to the web console.

    Function-scoped because `console` is: a session-scoped fixture cannot hold a
    per-test one. Nothing is lost -- only test_sc_comma_prints_timings uses this,
    so the session scope was never saving a second capture.
    """
    rows, lines = _collect_timings(host, console)
    assert len(rows) >= TIMINGS_MIN_ROWS, (
        f"/sc?, printed {len(rows)} timing rows, expected at least {TIMINGS_MIN_ROWS}, "
        f"in {TIMINGS_ATTEMPTS} attempts — {_console_diagnosis(console)}. "
        f"Is this a GBS_DEBUG=1 build? "
        f"Best capture: {list(rows)}, raw output: {lines!r}"
    )
    return rows


def test_sc_comma_prints_timings(timings):
    assert len(timings) >= TIMINGS_MIN_ROWS, f"only got {list(timings)}"


def _read_all(host):
    """Every register the console block covers, read over HTTP."""
    return {
        (label, index): read_field(host, segment, register, offset, width)
        for label, columns in TIMING_REGISTERS.items()
        for index, (segment, register, offset, width) in enumerate(columns)
    }


def test_timings_agree_with_getreg(host, console):
    """What the console prints and what /getreg reads must be the same
    registers — an independent check of each feature against the other.

    The reads bracket the print, because the firmware rewrites these registers
    while it is hunting for sync. A register that moved across the window proves
    nothing either way, so it is reported rather than asserted on; only ones that
    held still are compared.
    """
    before = _read_all(host)
    rows, lines = _collect_timings(host, console)
    after = _read_all(host)

    assert rows, (
        f"/sc?, printed nothing parseable in {TIMINGS_ATTEMPTS} attempts — "
        f"{_console_diagnosis(console)}. Raw output: {lines!r}"
    )

    compared, moved = {}, {}
    for label, columns in TIMING_REGISTERS.items():
        if label not in rows:
            continue
        for index, (segment, register, _, _width) in enumerate(columns):
            key = f"{label} s{segment}:{register:#04x}"
            printed = rows[label][index]
            if before[(label, index)] != after[(label, index)]:
                moved[key] = (before[(label, index)], printed, after[(label, index)])
            else:
                compared[key] = (printed, before[(label, index)])

    if not compared and moved:
        pytest.skip(f"every register moved while reading; unit has no stable sync: {moved}")
    assert compared, f"no known timing labels in {list(rows)}"

    disagreed = {k: v for k, v in compared.items() if v[0] != v[1]}
    assert not disagreed, f"console vs /getreg (printed, read): {disagreed}; moved: {moved}"


# --- sync type classification -----------------------------------------------


def test_a_source_with_its_own_vsync_is_not_configured_for_csync(
    host, source, register_guard
):
    """A source driving its own VS line is not composite sync, and the firmware
    must not configure the sync processor as though it were: SP_EXT_SYNC_SEL makes
    it take V off the HS line, blinding it to the V sync that is there.

    Sampled across a window rather than read once, because a firmware that has
    lost the lock hunts through configurations. Whether this input has its own V
    sync is then established with the probe the firmware itself uses, so a
    genuinely composite source skips instead of failing.
    docs/investigations/riscpc-no-sync.md
    """
    chosen = []
    deadline = time.monotonic() + HOLD_SECONDS
    while time.monotonic() < deadline:
        chosen.append(read_reg(host, *SYNC_PROC_00) or 0)

    baseline = register_guard(*SYNC_PROC_00)
    own_vsync = wait_for(
        # Rewritten every pass: the sync watcher puts its own value back.
        lambda: write_reg(host, *SYNC_PROC_00, baseline & ~EXT_SYNC_SEL)
        and (read_reg(host, *STATUS_16) or 0) & VS_ACTIVE,
        timeout=3.0,
    )
    if not own_vsync:
        pytest.skip("this input has no V sync of its own, so composite is the right call")

    csync = sum(1 for value in chosen if value & EXT_SYNC_SEL)
    assert csync == 0, (
        f"the source drives its own V sync, but the firmware set SP_EXT_SYNC_SEL in "
        f"{csync} of {len(chosen)} samples over {HOLD_SECONDS:.0f}s — blinding the "
        f"sync processor to the V sync that is there"
    )


def _sync_sample(host):
    """STATUS_16 with the registers around it, from one request where the
    firmware has /getregs. Sampling them together is the whole point: it is what
    separates a real sync loss — the PLL unlocking at the same instant — from a
    lone status bit that flickered between two round trips."""
    first, last = SYNC_SAMPLE_RANGE
    registers = read_segment(host, 0, first, last)
    if registers is None:  # firmware without /getregs; correlation is weaker
        registers = {r: read_reg(host, 0, r) for r in range(first, last + 1)}
    return {
        "status16": registers.get(0x16) or 0,
        "pll_lock": bool((registers.get(0x09) or 0) & PLLAD_LOCK),
        "htotal": field_from(registers, 0x17, 0, 12),
        "vtotal": field_from(registers, 0x1B, 0, 11),
    }


def test_the_sync_processor_holds_a_lock(host, source):
    """H sync active and staying active is what every downstream stage waits
    for. The no-sync fault shows up here twice over: as a lock that never
    arrives, and as one that arrives and collapses seconds later when the sync
    watcher reconfigures the sync processor underneath it.

    VSACT is deliberately not part of the lock -- see the LOCKED constant. It
    reads 0 on this source with a perfect picture, so requiring it made this
    test time out in its first assert for twelve days without ever reaching the
    hold loop that is the point of it.

    A dropout is confirmed before it fails the test: either it survives an
    immediate re-read, or the ADC PLL was unlocked in the same burst. Both forms
    of the no-sync fault last far longer than one sample and still fail here — a
    lock that collapses stays collapsed. What this filters out is the isolated
    single-read blip, measured at roughly 2 in 10,000 samples on a unit that ran
    9h38m without a visible disturbance, and never reproduced across 4285
    consecutive reads. Those are reported, not asserted on, because it is not
    established whether they are a real momentary loss or an artefact of reading
    a live segment-0 register mid-update.
    """
    locked = wait_for(
        lambda: (read_reg(host, *STATUS_16) or 0) & LOCKED == LOCKED, timeout=LOCK_TIMEOUT
    )
    assert locked, f"no H and V lock within {LOCK_TIMEOUT:.0f}s of asking"

    samples, anomalies = 0, []
    deadline = time.monotonic() + HOLD_SECONDS
    while time.monotonic() < deadline:
        sample = _sync_sample(host)
        samples += 1
        if sample["status16"] & LOCKED == LOCKED:
            continue
        follow = [_sync_sample(host) for _ in range(3)]
        anomalies.append(
            {
                "status16": sample["status16"],
                "pll_lock": sample["pll_lock"],
                "htotal": sample["htotal"],
                "vtotal": sample["vtotal"],
                "follow": [f["status16"] for f in follow],
                "confirmed": not sample["pll_lock"]
                or any(f["status16"] & LOCKED != LOCKED for f in follow),
            }
        )

    confirmed = [a for a in anomalies if a["confirmed"]]
    assert not confirmed, (
        f"the lock did not hold: {len(confirmed)} confirmed dropout(s) in {samples} "
        f"samples over {HOLD_SECONDS:.0f}s. Each either persisted into a re-read or "
        f"coincided with the ADC PLL unlocking: {confirmed}"
    )

    if anomalies:
        print(
            f"\n{len(anomalies)} unconfirmed single-read blip(s) in {samples} samples "
            f"({len(anomalies) / samples:.3%}); each recovered by the next read with the "
            f"PLL still locked: {anomalies}"
        )


# --- detection settles on an input instead of hunting between them -----------

ADC_INPUT_SEL = (5, 0x02, 6, 2)  # which ADC input the sync processor is watching

# The livelock alternated on a ~1 s beat, so ten seconds is eight or nine
# flips — far more than enough to catch it, and short enough to leave in a
# routine --source run.
MUX_HOLD_SECONDS = 10.0


def test_detection_settles_on_one_input_instead_of_hunting(host, source):
    """Detection has to converge. When it cannot, it does not fail loudly — it
    flips ADC_INPUT_SEL to the other input and tries again, forever, tearing
    down the picture it just built on every pass.

    The livelock, measured 2026-08-13 with the RiscPC at 320x256@50:

        s5_02=0x4b  ADC_INPUT_SEL 1  STATUS_16 0x02   the source, sync present
        s5_02=0x0d  ADC_INPUT_SEL 0  STATUS_16 0x00   nothing connected
        ... alternating on a ~1 s beat, indefinitely

    Cause: detectAndSwitchToActiveInput() ran the V-sync-absent search only for
    SeleInputSource == S_RGBs, while the V-sync-present search accepted S_VGA
    too. A unit saved as S_VGA on a source reading VSACT 0 -- which is the
    NORMAL reading on this bench, with a perfect picture -- matched neither, so
    detection fell out of the bottom of the function every time. Every boot
    ended in low power with the DAC down.

    This is the behavioural test for that: not "did detection succeed", which a
    single sample cannot distinguish from a livelock caught mid-flip, but "did
    it stop moving". SyncSearch covers which branch is chosen on the host; this
    covers the thing the user actually sees.
    """
    settled = wait_for(
        lambda: (read_reg(host, *STATUS_16) or 0) & HS_ACTIVE == HS_ACTIVE,
        timeout=LOCK_TIMEOUT,
    )
    assert settled, f"no H-sync within {LOCK_TIMEOUT:.0f}s, so there is nothing to settle on"

    seen = []
    deadline = time.monotonic() + MUX_HOLD_SECONDS
    while time.monotonic() < deadline:
        seen.append(read_field(host, *ADC_INPUT_SEL))
        time.sleep(0.4)

    distinct = sorted({v for v in seen if v is not None})
    assert len(distinct) == 1, (
        f"the input mux is hunting: ADC_INPUT_SEL took {distinct} across "
        f"{len(seen)} samples in {MUX_HOLD_SECONDS:.0f}s. Detection is not "
        f"converging, so every picture it builds is torn down on the next pass. "
        f"Samples: {seen}"
    )


# --- external clock generator: the display-clock stash -----------------------
#
# While the external clock generator drives the display, PLL648_CONTROL_01 is
# parked at a 0x75 sentinel and the real divider is stashed in RAM.
#
# **IT CANNOT BE STASHED IN GBS_PRESET_DISPLAY_CLOCK (s1_2D).** That sits inside
# the range every preset array overwrites, and all but the two downscale presets
# write 0 there. A preset load then zeroes the stash, the restore is skipped
# because it is guarded on the stash being non-zero, the sentinel reaches
# externalClockGenResetClock()'s lookup, matches no known divider, and
# rto->freqExtClockGen keeps a frequency left over from an earlier preset. That
# goes to the Si5351 as the display clock, so the TV gets timing it cannot lock
# to and goes blank -- with every scaler register still reading correct.


PRESET_DISPLAY_CLOCK = (1, 0x2D)  # GBS_PRESET_DISPLAY_CLOCK in Tv5725.h

# The eight values externalClockGenResetClock() maps to a frequency, plus the
# sentinel it parks at. A divider showing up in the stash register means someone
# has started using it to hold state again.
DISPLAY_CLOCK_DIVIDERS = {0x25, 0x35, 0x45, 0x55, 0x65, 0x85, 0x95, 0xA5}
DISPLAY_CLOCK_SENTINEL = 0x75

# The only values the preset arrays themselves put at s1_2D: 0 in the twelve
# scaling presets, 0x25 in pal_downscale/ntsc_downscale.
PRESET_STASH_VALUES = {0x00, 0x25}

# DS-5725-3.2 Table 15, Video Output Port AC timing: CLKOUT Frequency 1/Tmc max
# 108 MHz at 20pF. The display output clock, not Table 14's 80 MHz CLKIN (the
# digital video INPUT port, unused on this board) and not the memory interface's
# 162 MHz FBCLK. See src/tv5725/DisplayClock.h.
CLKOUT_CEILING_HZ = 108_000_000

# A raster is allowed to sit slightly over the rated maximum, because one
# measurably does: ntsc_1920x1080's 1602 x 1126 @ 60 Hz demands 108.23 MHz and
# the encoder locks to it. This is the observed margin, not a licence.
CLKOUT_TOLERANCE = 1.005

# How much of the budget a raster must actually spend. pal_1920x1080 shipped
# 1445 x 1126 @ 50 Hz -- 81.35 MHz, 75.3% -- which is what this catches.
CLKOUT_MIN_SPEND = 0.90

# Source VTOTAL above this is a 50 Hz standard. The firmware's own mode detect
# splits the same way: PAL-like sources run 312 lines (the bench RiscPC 311),
# NTSC-like 262.
SOURCE_VTOTAL_50HZ_MIN = 290

SYNC_PROC_VTOTAL = (0, 0x1B, 0, 11)  # STATUS_SYNC_PROC_VTOTAL
VDS_HSYNC_RST = (3, 0x01, 0, 12)
VDS_VSYNC_RST = (3, 0x02, 4, 11)


def test_the_output_raster_is_the_one_the_engine_computed(host, source):
    """VDS_HSYNC_RST holds what OutputRaster solved, not something steered to.

    The engine computes htotal = floor(clock / fieldRate / frameLines) and
    writes it once per mode change. Nothing else may move it: with an external
    clock generator FrameSync locks the frame time by steering the Si5351 --
    visible in the console as "Setting clock frequency" every 1.76 s -- so
    applyBestHTotal() also rewriting the raster is a SECOND correction for the
    same error, and the two do not converge to the same place.

    Measured across three boots of the same firmware: the engine computes 1918
    every time (the boot log's s0_41=0x85 -> 108000000 Hz is unambiguous) and the
    raster settles at 1915, 1436 and 1740. That last gives away 9% of the
    horizontal resolution to a correction the clock has already made.

    Tolerance is PROPORTIONAL, and 0.5% is chosen to separate the two cases
    rather than to be generous. The engine divides by the MEASURED field rate,
    which is 50.08 Hz on this bench and not the nominal 50 this test can compute
    from -- worth 3 px, or 0.16%. A steered raster is out by 8-25%. There is no
    overlap, and an absolute +-1 would fail on the correct value.
    """
    source_vtotal = read_field(host, *SYNC_PROC_VTOTAL)
    assert source_vtotal, "no source VTOTAL: nothing is locked"
    field_rate = 50 if source_vtotal > SOURCE_VTOTAL_50HZ_MIN else 60

    htotal_reg = read_field(host, *VDS_HSYNC_RST)
    vtotal_reg = read_field(host, *VDS_VSYNC_RST)
    assert htotal_reg and vtotal_reg, "could not read the output raster"

    frame_lines = vtotal_reg + 1
    if frame_lines < 1000:
        pytest.skip(f"frame height {frame_lines} is not a 1080p-class mode")

    # Tv5725::OutputRaster::htotalFor, floored, at EngineCeilingHz.
    wanted = int(CLKOUT_CEILING_HZ / field_rate / frame_lines)

    drift = abs((htotal_reg + 1) - wanted) / float(wanted)
    assert drift <= 0.005, (
        f"raster is {htotal_reg + 1} x {frame_lines} at ~{field_rate} Hz, "
        f"{drift:.1%} from the {wanted} the engine computes at "
        f"{CLKOUT_CEILING_HZ / 1e6:.0f} MHz. "
        f"Something moved it after solveRaster() -- applyBestHTotal() is the "
        f"one thing that does, and with an external clock generator it must "
        f"not: the Si5351 is already being steered to lock the frame time.")


def test_the_output_raster_spends_its_pixel_clock_budget(host, source):
    """A 1080p output raster must use most of the 108 MHz the part is rated for.

    Engine::solveRaster() derives the raster from the frame height and the
    measured field rate, so this reads the engine's output. Measured on the
    bench either side of that:

        preset table   1436 x 1126 @ 50 Hz   80.85 MHz   74.9%
        computed       1915 x 1126 @ 50 Hz  107.81 MHz   99.8%

    **Do not repair a failure here by patching a preset table.** That turns it
    green and destroys its meaning. If it goes red, the engine stopped computing
    the raster.

    Deliberately NOT keyed to a resolution or a field rate: every output raster
    has to answer the same question, so asking what fraction of the budget is
    spent is what catches the next one.
    """
    source_vtotal = read_field(host, *SYNC_PROC_VTOTAL)
    assert source_vtotal, "no source VTOTAL: nothing is locked, so there is no field rate"

    field_rate = 50 if source_vtotal > SOURCE_VTOTAL_50HZ_MIN else 60

    htotal_reg = read_field(host, *VDS_HSYNC_RST)
    vtotal_reg = read_field(host, *VDS_VSYNC_RST)
    assert htotal_reg and vtotal_reg, "could not read the output raster registers"

    if vtotal_reg + 1 < 1000:
        pytest.skip(
            f"output raster is {htotal_reg + 1} x {vtotal_reg + 1}, not a 1080p-class "
            "mode; this test is about how much of the clock a large raster spends"
        )

    # Both registers hold total-1, confirmed against the register docs.
    demanded = (htotal_reg + 1) * (vtotal_reg + 1) * field_rate
    spend = demanded / CLKOUT_CEILING_HZ
    context = (
        f"raster {htotal_reg + 1} x {vtotal_reg + 1} at {field_rate} Hz demands "
        f"{demanded / 1e6:.3f} MHz, {spend:.1%} of the {CLKOUT_CEILING_HZ / 1e6:.0f} MHz "
        f"CLKOUT ceiling (source VTOTAL {source_vtotal})"
    )

    assert spend <= CLKOUT_TOLERANCE, (
        f"the output raster asks for more than the part is rated for: {context}. "
        "DS-5725-3.2 Table 15 rates CLKOUT at 108 MHz; a raster past it is out of spec "
        "even if a particular encoder happens to lock."
    )
    assert spend >= CLKOUT_MIN_SPEND, (
        f"the output raster is leaving pixel clock unused: {context}. At this field rate "
        f"the ceiling affords {int(CLKOUT_CEILING_HZ / field_rate / (vtotal_reg + 1))} "
        f"pixels per line against the {htotal_reg + 1} programmed. That is horizontal "
        "resolution the board could produce and is not -- check s0_41 and s3_01/s3_02 in "
        "the preset table for this field rate."
    )


# The display-clock lookup has no silent fallthrough, and that is STRUCTURAL
# rather than guarded here: externalClockGenResetClock() assigns
# rto->freqExtClockGen unconditionally on its first line from
# DisplayClock::hzFor(), which returns on every path, so a stale frequency is not
# representable. Assigning inside the branches instead leaves the previous
# frequency in place for a byte matching none of them.
#
# The real decision -- that an unrecognised byte yields 0 rather than a plausible
# default -- is pinned in test/test_display_clock.cpp, "the external-clock
# sentinel names no clock at all". `make -C test display-clock`.

def test_display_clock_is_not_stashed_in_a_preset_register(host):
    """Live check of the same invariant: nothing has written a divider to s1_2D.

    Read-only and mode-independent — it asserts only that the value is one a
    preset array could have put there. The old firmware left a real divider here
    after every preset apply, so this fails against it.
    """
    segment, register = PRESET_DISPLAY_CLOCK
    stashed = read_reg(host, segment, register)
    assert stashed is not None, f"could not read s{segment}_{register:02X}"

    parked = read_reg(host, 0, 0x41)  # PLL648_CONTROL_01
    context = (
        f"s{segment}_{register:02X}={stashed:#04x}, PLL648_CONTROL_01={parked:#04x} "
        f"(sentinel is {DISPLAY_CLOCK_SENTINEL:#04x})"
    )

    assert stashed not in (DISPLAY_CLOCK_DIVIDERS - PRESET_STASH_VALUES), (
        f"a display-clock divider is sitting in the preset-written stash register: {context}. "
        "That is the pre-fix behaviour; a preset load will zero it and the output clock "
        "will fall back to a stale frequency."
    )
    assert stashed in PRESET_STASH_VALUES, (
        f"unexpected value in the stash register: {context}. The preset arrays only ever "
        f"write {sorted(PRESET_STASH_VALUES)} there, so something else has written to it."
    )


# --- filesystem -------------------------------------------------------------


def test_the_filesystem_lists_each_file_once(host):
    """A file listed twice is a corrupt directory, not a quirk of the listing.

    SPIFFS updates its directory metadata in place and is not power-loss safe,
    and this unit gets hard-power-cycled mid-write routinely: two entries for
    /slots.bin, one serving HTTP 200 with 0 bytes and then hanging for the full
    client timeout, stalls the web UI on its splash because main() waits on
    fetchSlotNamesAndInit() before it paints anything.

    There is no fsck for SPIFFS, so a duplicate entry cannot be repaired in
    place, which is the argument the migration to LittleFS rests on. This guards
    the class, not that one file.
    """
    files = fs_dir(host)
    assert files is not None, "/fs/dir did not answer"

    duplicated = sorted({f for f in files if files.count(f) > 1})
    assert duplicated == [], (
        f"the filesystem lists {duplicated} more than once, out of {files}. A "
        "duplicate directory entry is the classic power-loss corruption "
        "signature, and an unreadable one stalls the web UI on its splash."
    )


# --- preset save ------------------------------------------------------------


def test_no_leftover_temp_presets(host):
    """savePresetToFS() writes '<preset>~' and renames it into place. One
    left behind means a save failed part way and did not clean up."""
    files = fs_dir(host)

    assert files is not None, "/fs/dir did not answer"
    assert [f for f in files if f.endswith("~")] == []


@pytest.mark.preset_save
def test_preset_save_completes_or_refuses_cleanly(host, console):
    """/uc?4 must either leave a complete 432-value preset, or refuse and change
    nothing at all.

    What it must never do is the old behaviour: write nothing (or a partial
    file) and still leave the unit's preset preference pointing at it, so the
    next boot loads a preset that was never written. Which branch runs depends
    on the unit's current video mode — mode 15, for one, has no preset file —
    so both are accepted, and the test asserts the contract either way.
    """
    before = fs_dir(host)
    assert before is not None, "/fs/dir did not answer before the save"
    prefs_before = fs_read(host, "/preferencesv2.txt")
    assert prefs_before is not None, "could not read the preferences file"

    console.drain()
    status, _ = get(host, "/uc?4")
    assert status == 200
    output = console.collect(5.0)

    after = fs_dir(host)
    assert after is not None, "/fs/dir did not answer after the save"
    assert [f for f in after if f.endswith("~")] == [], f"temp file left behind: {after}"

    presets = [f for f in after if f.startswith("/preset_")]
    if not presets:
        # Refused. The preference must not have moved, and it must have said why.
        assert fs_read(host, "/preferencesv2.txt") == prefs_before, (
            "the save was refused but the preferences changed anyway — the unit "
            "may now boot into a preset that does not exist"
        )
        refusals = [line for line in output if "preset save" in line]
        assert refusals, (
            "the save wrote nothing and gave no reason; expected a 'preset save: ...' "
            f"line on the console. Got: {output!r}"
        )
        print(f"\nrefusal path verified: {refusals[0]!r}")
        return

    written = sorted(presets, key=lambda f: f in before)[0]
    content = fs_read(host, written)
    assert content is not None, f"could not download {written}"

    values = [v for v in content.split("}")[0].split(",") if v.strip()]
    assert len(values) == PRESET_VALUE_COUNT, (
        f"{written} holds {len(values)} values, expected {PRESET_VALUE_COUNT}"
    )

    numbers = [int(v) for v in values]
    assert all(0 <= v <= 255 for v in numbers), f"{written} holds non-byte values"
    assert len(set(numbers)) > 1, f"{written} is a dead readback (every value identical)"
    print(f"\nwrite path verified: {written}, {len(values)} values")



# --- surviving a lost lock --------------------------------------------------


@pytest.mark.no_sync
def test_unit_answers_http_while_sync_is_absent(host):
    """With the source disconnected, the unit must stay reachable.

    FrameSync times a vsync period by spinning until an edge ISR fires. With no
    signal there is no edge, so this is the path that runs flat out for as long
    as it is allowed to. Bounded by an iteration count with the watchdog
    disabled, it holds the CPU long enough that serial, ping and HTTP all stop
    answering while the picture keeps running -- the TV5725 is a separate chip --
    and the caller re-enters immediately, so a bounded stall behaves like a
    permanent wedge. A PLLAD_MD write big enough to break sync gets you there.

    The wait is bounded in TIME, so the property is stated as reachability rather
    than as anything about the loop: every request answers, and none takes
    anything like that long.
    """
    status16 = read_reg(host, 0, 0x16)
    assert status16 is not None, "could not read STATUS_16"
    hs_active = (status16 >> 1) & 1
    vs_active = (status16 >> 3) & 1
    assert not (hs_active and vs_active), (
        f"STATUS_16 is {status16:#04x}: the source is still locked. Unplug it, "
        "or this test proves nothing about surviving its absence."
    )

    # Two samples per FrameSync pass at FS_SAMPLE_TIMEOUT_MS each, so a request
    # arriving mid-measurement can legitimately wait around half a second. The
    # regression this catches is not slowness, it is silence.
    limit_seconds = 2.0
    attempts = 40

    slowest = 0.0
    failures = []
    for attempt in range(attempts):
        started = time.monotonic()
        status, body = get(host, "/wifi/status", timeout=limit_seconds + 1.0)
        elapsed = time.monotonic() - started
        slowest = max(slowest, elapsed)
        if status != 200:
            failures.append(f"attempt {attempt}: status {status} after {elapsed:.2f}s ({body[:60]})")
        time.sleep(0.1)

    print(f"\nno sync: {attempts} requests, slowest {slowest:.2f}s")
    assert not failures, "the unit stopped answering with no sync:\n  " + "\n  ".join(failures)
    assert slowest < limit_seconds, (
        f"slowest response was {slowest:.2f}s, over the {limit_seconds}s bound -- "
        "the wait is not bounded the way it should be"
    )


# framesync's failure reporting. vsyncPeriodAndPhase() announces itself on every
# call; before this it announced nothing else, so a unit that could not sync
# printed the header forever and named no cause.
FS_HEADER = re.compile(r"vsyncPeriodAndPhase\(\), TEST_BUS_SEL=")
FS_REASON = re.compile(
    r"vsyncPeriodAndPhase\(\): no (?:INPUT|OUTPUT) vsync"
    r"|runFrequency\(\): (?:attempt \d+|gave up)"
    r"|fpsOutput="
)
FS_COLLECT_SECONDS = 12.0

# printInfo()'s status line, and the `v:` field within it. `v:` is VPERIOD_IF and
# `vt:` is the sync processor's -- two different blocks, and only one of them is
# trustworthy on RGBHV.
INFO_LINE = re.compile(r"^h:\s*\d+\s+v:")
INFO_VPERIOD_NUMBER = re.compile(r"^h:\s*\d+\s+v:\s*\d")


def test_framesync_names_its_outcome(console):
    """A framesync attempt must say how it ended, not just that it started.

    Either it succeeded, and printed fpsOutput=, or it failed and printed which
    check failed. The header alone is the regression: two sessions read a stream
    of bare headers and inferred the wrong failing sample from it.

    Skips when framesync is not running at all -- with no source, or a source
    the firmware has given up on, the loop never executes and there is nothing
    here to assert. It fails on a console that is connected but printing headers
    with no outcome, which is the actual fault.
    """
    console.drain()
    lines = console.collect(seconds=FS_COLLECT_SECONDS)

    headers = [line for line in lines if FS_HEADER.search(line)]
    if not headers:
        pytest.skip(
            f"framesync did not run in {FS_COLLECT_SECONDS:.0f}s "
            f"({_console_diagnosis(console)}). Needs a locked source and a "
            f"GBS_DEBUG=1 build"
        )

    reasons = [line for line in lines if FS_REASON.search(line)]
    assert reasons, (
        f"framesync started {len(headers)} times in {FS_COLLECT_SECONDS:.0f}s and "
        f"never reported an outcome — {_console_diagnosis(console)}. "
        f"A bare header names no cause, which is exactly what made the output "
        f"vsync sample look like the failing one when it is the input sample. "
        f"Raw output: {lines!r}"
    )


def test_console_does_not_print_an_invalid_vperiod_as_a_number(host, console):
    """printInfo()'s `v:` field must not look like a measurement when it is not.

    The line carries both vertical totals: `v:` is VPERIOD_IF, from the input
    formatter, and `vt:` is STATUS_SYNC_PROC_VTOTAL, from the sync processor.
    They come from different blocks and legitimately disagree -- on RGBHV the IF
    never completes a vertical measurement, so `v:` is meaningless while `vt:` is
    correct. Printed as two plain numbers side by side they look equally
    authoritative. See docs/tv5725-chip.md.

    The chip says which one to believe: STATUS_IF_VT_BAD (s0 0x05 bit 3) is set
    exactly when the IF's vertical timing is not valid, so when it is set `v:`
    reads `----` rather than a number.

    Skips when VT_BAD is clear, because then there is nothing to mark -- on an
    SD source the measurement is real and should print as one.

    printInfo() does not run by default: rto->printInfos is false and loop()
    gates on it, so this turns it on with /sc?i and back off afterwards. That
    toggle is also why the line is not the everyday diagnostic it looks like --
    you only see it if you asked for it, or if the no-sync counter trips.
    """
    if not (read_reg(host, 0, 0x05) >> 3) & 1:
        pytest.skip(
            "STATUS_IF_VT_BAD is clear, so VPERIOD_IF is valid here and should "
            "print as a number; nothing to assert"
        )

    get(host, "/sc?i")  # toggle printInfo() on
    try:
        console.drain()
        lines = console.collect(seconds=6)
    finally:
        get(host, "/sc?i")  # and off again, whatever happened above

    info = [line for line in lines if INFO_LINE.search(line)]
    if not info:
        pytest.skip(
            f"printInfo() did not reach the console in 6s with /sc?i on "
            f"({_console_diagnosis(console)}). Needs a GBS_DEBUG=1 build"
        )

    numeric = [line for line in info if INFO_VPERIOD_NUMBER.search(line)]
    assert not numeric, (
        f"STATUS_IF_VT_BAD is set, so the IF has no valid vertical measurement, "
        f"but printInfo() still prints `v:` as a number. That is what makes a "
        f"meaningless VPERIOD_IF look like a fault next to a correct `vt:`. "
        f"Expected `v:----`. Lines: {numeric[:3]!r}"
    )


# --- surviving a hostile PLLAD_MD -------------------------------------------

# The divider is the ADC PLL's, so a value the source cannot be sampled at takes
# input sync with it -- and that is the path that hangs the board when the wait
# for a vsync edge is unbounded.
#
# 2573 and 2583 are where the PLL hunts and then gives up on this bench source,
# which is where the hang is reachable. 4095 is the 12-bit ceiling and hostile to
# any source.
#
# **THE FIRST TWO ARE CALIBRATED TO A 320x256@50 RiscPC.** On another source the
# sampling ceiling sits elsewhere and they need recalculating. 4095 always
# holds.
PLLAD_HOSTILE = (2573, 2583, 4095)
PLLAD_MD_FIELD = (5, 0x12, 0, 12)
PLLAD_CONTROL = (5, 0x11)
PLLAD_HAMMER_SECONDS = 8.0
PLLAD_REQUEST_TIMEOUT = 2.0


def _pllad_latch(host):
    """PLLAD_LAT low then high — the PLL takes the divider on the edge."""
    control = read_reg(host, *PLLAD_CONTROL)
    assert control is not None, "could not read PLLAD control register"
    write_reg(host, *PLLAD_CONTROL, control & ~0x80)
    time.sleep(0.01)
    write_reg(host, *PLLAD_CONTROL, control | 0x80)


def _pllad_write(host, value):
    write_reg(host, 5, 0x12, value & 0xFF)
    high = read_reg(host, 5, 0x13)
    assert high is not None, "could not read PLLAD_MD high byte"
    write_reg(host, 5, 0x13, (high & 0xF0) | ((value >> 8) & 0x0F))
    _pllad_latch(host)


def _hammer(host, seconds=PLLAD_HAMMER_SECONDS):
    """Poll while the divider is wrong. Returns (requests, failures, slowest)."""
    started = time.monotonic()
    requests = failures = 0
    slowest = 0.0
    while time.monotonic() - started < seconds:
        began = time.monotonic()
        try:
            status, _ = get(host, "/wifi/status", timeout=PLLAD_REQUEST_TIMEOUT)
            answered = status == 200
        except Exception:  # noqa: BLE001 - any transport failure is a failure
            answered = False
        slowest = max(slowest, time.monotonic() - began)
        requests += 1
        if not answered:
            failures += 1
        time.sleep(0.05)
    return requests, failures, slowest


@pytest.mark.pllad_hostile
def test_unit_survives_a_hostile_pllad(host, source):
    """A divider that breaks sync must not cost us the unit.

    The property is reachability, and only reachability. A corrupted picture is
    the expected outcome here and is not a failure -- deriving dividers that keep
    the picture is a later phase. What must not happen is the board going away
    and needing a power cycle, because that is what makes automatic PLLAD_MD
    selection unsafe to build on.

    Restores the divider on the way out, including when the assertion fails, so
    a red test leaves a unit you can keep working with.
    """
    baseline = read_field(host, *PLLAD_MD_FIELD)
    assert baseline and 256 <= baseline <= 4095, (
        f"PLLAD_MD reads {baseline}; refusing to run without a sane baseline to "
        "put back"
    )

    results = []
    try:
        for value in PLLAD_HOSTILE:
            _pllad_write(host, value)
            time.sleep(0.5)
            readback = read_field(host, *PLLAD_MD_FIELD)
            requests, failures, slowest = _hammer(host)
            results.append((value, readback, requests, failures, slowest))
            assert failures == 0, (
                f"PLLAD_MD {value}: {failures} of {requests} requests went "
                f"unanswered (slowest {slowest:.2f}s). The unit stopped talking "
                f"with a bad divider -- this is the hang that cost three "
                f"sessions. Baseline {baseline} is being restored."
            )
            assert slowest < PLLAD_REQUEST_TIMEOUT, (
                f"PLLAD_MD {value}: slowest response {slowest:.2f}s. Answering "
                "but stalling means the wait is not bounded the way it should be."
            )
    finally:
        _pllad_write(host, baseline)
        time.sleep(1.0)

    for value, readback, requests, failures, slowest in results:
        print(f"\nPLLAD_MD {value} (read {readback}): {requests} requests, "
              f"{failures} failed, slowest {slowest:.2f}s")

    restored = read_field(host, *PLLAD_MD_FIELD)
    assert restored == baseline, (
        f"PLLAD_MD did not go back: wanted {baseline}, reads {restored}"
    )


# --- the boot log ------------------------------------------------------------

PREFS_PATH = "/preferencesv2.txt"
PREFS_BYTES = 39


def test_bootlog_reports_the_preferences_read(host):
    """/bootlog must show how this boot loaded its settings.

    This endpoint exists because the boot that matters cannot be watched: the
    trace prints before WiFi is up, so only serial sees it live, and serial is
    USB, and USB backfeeds power -- so attaching it stops the boot being the
    mains-only one that fails. Reading it back over HTTP is the whole point, and
    an empty or truncated log is the regression.

    Asserts the shape of the trace rather than its values, because a good boot
    and a bad one both have to be legible here.
    """
    status, body = get(host, "/bootlog")
    assert status == 200, f"/bootlog answered {status}: {body[:120]}"
    if "boot log disabled" in body:
        # BOOTLOG_BYTES defaults to 0: the 2048-byte buffer cost 2160 bytes of
        # globals and put free heap under the console's broadcast threshold.
        # This trace is opt-in now -- reflash with BOOTLOG_BYTES=2048 to use it.
        pytest.skip("boot log compiled out (BOOTLOG_BYTES=0)")
    assert "(boot log empty)" not in body, (
        "the boot log is empty. Nothing was captured before the first console "
        "client attached, or bootLogAppend() is no longer being fed"
    )

    assert "PREFS:" in body, f"no preferences trace in the boot log:\n{body}"
    assert re.search(r"PREFS: attempt \d+ .*got=\d+ .*plausible=[01]", body), (
        f"the per-attempt read line is missing or reshaped:\n{body}"
    )
    loaded = re.search(
        r"PREFS: loaded presetPreference=(\d+) frameTimeLock=(\d+).*suspect=([01])",
        body,
    )
    assert loaded, f"no 'PREFS: loaded' summary line:\n{body}"

    preference, lock, suspect = (int(g) for g in loaded.groups())
    print(f"\nboot log: presetPreference={preference} frameTimeLock={lock} "
          f"suspect={suspect}")

    # The defaults signature. Not asserted as a failure on its own -- a unit that
    # genuinely has no settings yet reads this way -- but it must never appear
    # while the loader believes it read the file, because that combination is
    # exactly the wipe that cost two sessions.
    if preference == 5 and lock == 0:
        assert suspect == 1, (
            "the boot loaded presetPreference=5 with frameTimeLock=0 -- the "
            "defaults signature -- while reporting suspect=0, meaning it read "
            "the file successfully and still ended up on defaults. Something "
            "wrote them. This is the 290b0a7 regression"
        )


def test_preferences_survive_a_round_trip(host):
    """Toggling one setting must not disturb the rest of the file.

    The failure this guards is not a wrong value, it is collateral: a save path
    that rebuilds the whole file from RAM persists whatever else was reset on the
    way past, which is how selecting an input takes presetPreference and
    enableFrameTimeLock with it.

    /uc?5 toggles frame time lock and saves, so toggling twice should land
    exactly where it started, byte for byte.
    """
    before = fs_read(host, PREFS_PATH)
    assert before and len(before) == PREFS_BYTES, (
        f"{PREFS_PATH} is {len(before) if before else 0} bytes, expected "
        f"{PREFS_BYTES}"
    )

    try:
        get(host, "/uc?5")
        time.sleep(2.5)
        middle = fs_read(host, PREFS_PATH)
        assert middle and len(middle) == PREFS_BYTES, "file went malformed mid-toggle"
        assert middle[1] != before[1], (
            f"/uc?5 did not change frame time lock (byte 1 stayed {before[1]!r}); "
            "this test is not exercising a save"
        )
        changed = [i for i in range(PREFS_BYTES) if middle[i] != before[i]]
        assert changed == [1], (
            f"toggling frame time lock also changed bytes {changed}. "
            f"before={before!r} after={middle!r}"
        )
    finally:
        get(host, "/uc?5")
        time.sleep(2.5)

    after = fs_read(host, PREFS_PATH)
    assert after == before, (
        f"preferences did not round-trip.\n  before {before!r}\n  after  {after!r}"
    )


# --- freeze: stop the firmware writing TV5725 registers -----------------------

# PLLAD_MD is the witness rather than anything in the VDS block: every preset
# writes it, it is non-zero in every mode, and it survives HD bypass. VDS_HSYNC_RST
# and VDS_HSCALE both read 0 on a bypassed source -- the VDS block is not driving
# the output at all -- so they cannot tell "no preset load" from "bypass mode".
PRESET_WITNESS = (5, 0x12, 0, 12)  # PLLAD_MD


def _freeze_state(host):
    """Whether automation is frozen, or None if the unit has no /freeze."""
    status, payload = get_json(host, "/freeze")
    if status != 200 or not isinstance(payload, dict):
        return None
    return payload.get("frozen")


def test_freeze_can_be_armed_and_reports_its_state(host):
    """/freeze reports the flag, and setting it takes effect.

    Observability first: without a readable flag neither a test nor a human can
    tell a frozen unit from one that merely happens to be quiet, and "quiet" is
    the normal state of a locked source. The flag is deliberately not persisted,
    so this leaves nothing behind that a reboot would not clear anyway.
    """
    before = _freeze_state(host)
    assert before is not None, (
        "GET /freeze did not return a JSON object with a 'frozen' key. The "
        "firmware has no freeze support, or it is not reporting it."
    )

    try:
        status, payload = get_json(host, "/freeze?on=1")
        assert status == 200 and payload.get("frozen") is True, (
            f"/freeze?on=1 returned {status} {payload}; expected frozen=true"
        )
        assert _freeze_state(host) is True, "freeze did not stay armed"
    finally:
        get(host, "/freeze?on=0")

    assert _freeze_state(host) is False, "freeze did not disarm"


@pytest.mark.freeze
def test_frozen_firmware_does_not_load_presets(host, source):
    """Frozen, a command that would load a preset must change nothing.

    This is the property the mode exists for: registers stay where you put them
    so a targeted experiment is measuring the chip rather than the firmware
    racing you. /sc?# calls applyPresets(13) directly and writes no flash, which
    makes it a deterministic trigger over HTTP -- no source mode change needed.

    Opt-in because the un-frozen half of the behaviour would corrupt the picture;
    here the whole point is that it does not, but a red test means it did.
    """
    assert _freeze_state(host) is not None, "unit has no /freeze support"

    # Baseline BEFORE arming. Reading it afterwards cannot tell a unit that was
    # never configured from one this test froze before it finished detecting.
    before = read_field(host, *PRESET_WITNESS)
    assert before, (
        f"PLLAD_MD reads {before}; refusing to run without a configured unit to "
        "observe. Let the source lock first."
    )

    get(host, "/freeze?on=1")
    assert _freeze_state(host) is True, "could not arm the freeze"
    try:
        get(host, "/sc?#")
        time.sleep(2.0)

        after = read_field(host, *PRESET_WITNESS)
        assert after == before, (
            f"frozen, /sc?# still changed PLLAD_MD {before} -> {after}. "
            "applyPresets() ran when it should have been inert."
        )
    finally:
        # /sc?# also sets rto->videoStandardInput = 13 outside applyPresets(), so
        # unfreezing lets the sync watcher notice and re-detect. That is the
        # intended way back, not a leak.
        get(host, "/freeze?on=0")


# IF_HB_SP2 -- the left edge of the captured line, and the register the framing
# moves first. Reading the framing back is not enough: /geometry reports the
# state it just stored whether or not anything reached the chip.
CAPTURE_WITNESS = (1, 0x1A, 0, 11)  # IF_HB_SP2


def _capture_sp(host):
    return read_field(host, *CAPTURE_WITNESS)


@pytest.mark.freeze
def test_an_explicit_framing_request_applies_while_frozen(host, source):
    """Frozen, /geometry must move the picture, not queue a write for later.

    Freeze exists so a geometry experiment measures the chip instead of racing
    the firmware, and /geometry is the only way to drive the engine -- a freeze
    that disarms it cannot be used for the thing it was built for. The same rule
    already covers /setreg, /uc and /sc: an explicit request is honoured frozen,
    automation is not.

    What it did instead, measured on the bench 2026-08-08: the framing state
    moved, the registers did not, and the write landed on its own four seconds
    after /freeze?on=0 -- capture 90..1042 through the whole frozen window, then
    106..1058 once unfrozen. So an experiment finished carrying a live geometry
    write into whatever ran next. Cause: geometrySolvePending's only consumer is
    geometrySolveIfPending() inside runSyncWatcher(), behind the freeze gate.
    """
    assert _freeze_state(host) is not None, "unit has no /freeze support"

    status, framing = get_json(host, "/geometry")
    assert status == 200 and isinstance(framing, dict), (
        f"GET /geometry returned {status} {framing}; no framing to restore to"
    )
    restore = dict(framing)

    get(host, "/freeze?on=1")
    assert _freeze_state(host) is True, "could not arm the freeze"
    try:
        before = _capture_sp(host)
        assert before is not None, "could not read IF_HB_SP2"

        # A pan rather than a zoom: it moves the window without resizing it, so
        # a failure to apply cannot hide behind a scale that rounded to the same
        # place. 16 units is two of the firmware's own pan steps.
        wanted = int(restore["ph"]) + 16
        status, payload = get_json(host, f"/geometry?ph={wanted}")
        assert status == 200 and payload.get("ph") == wanted, (
            f"/geometry?ph={wanted} returned {status} {payload}"
        )

        moved = wait_for(lambda: _capture_sp(host) != before, timeout=6.0)
        assert moved, (
            f"IF_HB_SP2 stayed at {before} for 6 s after /geometry?ph={wanted} "
            f"while frozen, and /geometry reported ph={wanted}. The framing "
            f"state and the chip now disagree, and the write is queued: it will "
            f"fire the moment the freeze lifts."
        )
    finally:
        get(host, f"/geometry?ph={restore['ph']}")
        get(host, "/freeze?on=0")


# ADC_SOGCTRL, the sync-on-green slice level: s5_02 bits [5:1]. The
# source-recovery poll steps it down one notch every 500 ms while the firmware
# believes nothing is plugged in, sweeping for a level that finds sync.
SOG_LEVEL = (5, 0x02, 1, 5)
DAC_POWER = (0, 0x44, 0, 1)  # DAC_RGBS_PWDNZ, 0 in low power

# Six poll intervals. The ratchet is one step per 500 ms, so a leaking firmware
# has moved the level by about six by the time this elapses -- far enough that
# no rounding or single missed sample can explain a pass.
SOG_OBSERVATION_SECONDS = 3.0

# Above anything the ratchet resets to (it floors at 3 and jumps back to 6), so
# a value that has not moved cannot be a coincidence of the sweep passing
# through where we put it.
SOG_WITNESS = 24


def _sog_level(host):
    return read_field(host, *SOG_LEVEL)


@pytest.mark.freeze
def test_frozen_firmware_does_not_ratchet_the_sog_level(host, source):
    """Frozen, the source-recovery poll must not run -- it writes ADC_SOGCTRL.

    The freeze's guards sit on applyPresets(), runSyncWatcher(),
    runAutoBestHTotal(), detectAndSwitchToActiveInput() and runAutoGain(). None
    of them covers loop()'s own source-recovery block, which calls
    inputAndSyncDetect() DIRECTLY rather than through runSyncWatcher() and steps
    ADC_SOGCTRL beside it. Measured on the bench 2026-08-13 with /freeze
    reporting {"frozen":true}:

        wrote ADC_SOGCTRL 24 -> read back 21, 19, 17, 15, 13, 11 ...

    The guard on detectAndSwitchToActiveInput() makes it worse rather than
    better: frozen it returns 0, which is exactly what tells inputAndSyncDetect()
    nothing is plugged in, so the firmware powers the DAC down and calls
    setResetParameters() -- while reporting itself frozen.

    It leaks only while sourceDisconnected is true, **which is precisely when you
    are recovering**, so the documented "freeze, then restore a snapshot" recipe
    is undone by the firmware that is supposed to be holding still: a
    hand-restored picture decays into colour noise with every config register
    still reading correct.

    /sc?~ is the trigger: it runs goLowPowerWithInputDetection() directly, which
    is an explicit request and so is honoured frozen, and it is the only route to
    sourceDisconnected that is not an OLED button. That makes this test drop the
    picture on purpose -- hence @freeze -- and unfreezing is what brings it back.
    """
    assert _freeze_state(host) is not None, "unit has no /freeze support"

    get(host, "/freeze?on=1")
    assert _freeze_state(host) is True, "could not arm the freeze"
    try:
        # Reach the disconnected state, and confirm it through the DAC rather
        # than through the ratchet -- reading the leak to prove the leak's
        # precondition would pass by construction.
        get(host, "/sc?~")
        went_dark = wait_for(
            lambda: read_field(host, *DAC_POWER) == 0, timeout=10.0
        )
        assert went_dark, (
            "DAC_RGBS_PWDNZ never reached 0 after /sc?~, so the unit did not "
            "enter low power and this test never reached the state it is about."
        )

        write_reg(host, 5, 0x02, (read_reg(host, 5, 0x02) & ~0x3E) | (SOG_WITNESS << 1))
        assert _sog_level(host) == SOG_WITNESS, "could not place the SOG witness"

        # Sample across the window rather than reading once at the end. A single
        # closing read cannot tell a level that never moved from one the sweep
        # walked back through, and it mostly returns None anyway: the leaking
        # firmware is inside detection's 6000 ms getVideoMode() sweep, /getreg
        # defers to loop() through RegisterQueue, and the read times out. That
        # silence is a symptom, not a flake, so it gets its own assertion instead
        # of being averaged into the value.
        samples, blocked = [], 0
        deadline = time.monotonic() + SOG_OBSERVATION_SECONDS
        while time.monotonic() < deadline:
            level = _sog_level(host)
            if level is None:
                blocked += 1
            else:
                samples.append(level)

        assert samples, (
            f"frozen, every ADC_SOGCTRL read in {SOG_OBSERVATION_SECONDS} s "
            f"timed out ({blocked} of them). /getreg defers to loop(), so loop() "
            f"is busy -- it is running detection's sweep behind the freeze."
        )
        walked = sorted({level for level in samples if level != SOG_WITNESS})
        assert not walked, (
            f"frozen, ADC_SOGCTRL walked off {SOG_WITNESS} to {walked} within "
            f"{SOG_OBSERVATION_SECONDS} s with nobody asking. loop()'s "
            f"source-recovery block ran behind the freeze."
        )
    finally:
        get(host, "/freeze?on=0")
        # Leave the bench with a picture: unfrozen, the recovery poll is
        # supposed to run, and it is the thing that finds the source again.
        wait_for(
            lambda: read_field(host, *DAC_POWER) == 1, timeout=LOCK_TIMEOUT
        )


def test_the_console_delivers_anything_at_all(console):
    """The web console must actually broadcast, not merely accept a connection.

    SerialMirror only calls broadcastTXT above a free-heap threshold, and this
    fork's globals take 47584 bytes of an 81920 byte heap: a threshold of 20000
    is permanently out of reach, so the socket connects, stays connected and
    delivers nothing for the life of the session. That is the web UI sitting on
    its splash screen with the red disconnected indicator.

    Connected-but-silent is the exact case _console_diagnosis() cannot tell from
    a quiet firmware, so it needs its own test: on a GBS_DEBUG=1 build FrameSync
    prints continuously, and receiving nothing means the gate shut.

    **THE `console` FIXTURE MUST BE PER-TEST, NOT PER-SESSION.** One socket held
    across the run reads silence from a dead connection in every later console
    test once SerialMirror has dropped it, which looks exactly like this failure.
    Fixed in conftest.py.

    It retries rather than skipping: a genuinely shut gate stays shut, so no
    number of retries makes this pass, and only the settling case recovers.
    Three attempts over ~30 s.
    """
    received = []
    for attempt in range(ATTEMPTS := 3):
        console.drain()
        received = console.collect(seconds=6)
        if received:
            break
        if attempt + 1 < ATTEMPTS:
            time.sleep(5)

    assert received, (
        f"the console connected but delivered 0 bytes in 6 s, on {ATTEMPTS} "
        "separate attempts. On a GBS_DEBUG=1 build FrameSync prints "
        "every cycle, so this is the heap gate in SerialMirror refusing to "
        "broadcast, not a quiet firmware. Check /bootlog for 'free heap'."
    )


def test_bypass_does_not_leave_the_scaling_flag_set(host, source):
    """Entering RGBHV bypass clears GBS_OPTION_SCALING_RGBHV.

    Measured on the bench 2026-08-15, on a real 320x256 -> 800x600 source change:
    the unit landed in bypass with GBS_PRESET_ID 0x22 and the scaling flag still
    reading 1. bypassModeSwitch_RGBHV() never wrote it -- only
    setResetParameters() does, and that is the low-power path, not this one. The
    >535-line branch that sends the source here sets rto->isValidForScalingRGBHV
    = false in RAM and leaves the register alone.

    So a bypass register dump claims the unit is scaling RGBHV when video is
    routing around the VDS entirely. It is not cosmetic: five sites read the bit
    back to make decisions, including PresetLoad via writeProgramArrayNew() and
    the autoBestHtotal guard in doPostPresetLoadSteps().

    /sc?k calls bypassModeSwitch_RGBHV() directly, which is what makes this
    reachable without a source mode change. It drops the picture on purpose;
    /sc?~ at the end forces a fresh detection pass to get it back.

    **AUTOMATION IS FROZEN FOR THE MEASUREMENT, AND WITHOUT THAT THIS TEST IS A
    COIN TOSS.** It was, from the day it was written: measured 2026-08-14 on the
    firmware it shipped with, four runs gave three passes and a failure, and a
    tighter probe on the same build caught the flag reading 0 a full 1.6 s BEFORE
    GBS_PRESET_ID reached 0x22 and back to 1 by the time it did.

    The race is not a defect in the thing under test. /sc?k forces bypass onto a
    311-line source that is perfectly scalable, so detection re-runs, decides the
    source is fine, sets GBS_OPTION_SCALING_RGBHV back to 1 (:6508, :6557, :6651)
    and reloads a scaling preset -- which is correct. The window in which both
    halves of the invariant hold is however long it takes loop() to notice, and
    reading it through /getreg cannot even sample cleanly, because register reads
    are deferred to loop() and block exactly while detection is busy.

    Freezing removes the other party rather than widening the window. /sc is
    honoured while frozen and detectAndSwitchToActiveInput() is one of the six
    guarded entry points, so bypassModeSwitch_RGBHV() runs and nothing undoes it.
    That leaves the assertion asking what the commit that added it claimed:
    entering bypass writes the flag.
    """
    write_reg(host, 1, 0x2C, read_reg(host, 1, 0x2C) | 0x02)
    assert read_field(host, 1, 0x2C, 1, 1) == 1, (
        "could not set the scaling flag, so the test cannot show it being "
        "cleared rather than merely being clear already")

    try:
        get(host, "/freeze?on=1")
        get(host, "/sc?k")  # bypassModeSwitch_RGBHV()

        assert wait_for(lambda: read_field(host, 1, 0x2B, 0, 7) == 0x22,
                        timeout=15.0), (
            "GBS_PRESET_ID never became PresetBypassRGBHV: bypass did not run, "
            "so the flag says nothing either way")

        assert read_field(host, 1, 0x2C, 1, 1) == 0, (
            "in RGBHV bypass with GBS_OPTION_SCALING_RGBHV still 1: the register "
            "claims the unit is scaling while video routes around the VDS")
    finally:
        # Unfreeze FIRST: frozen, /sc?~ switches the mux and stops, because the
        # re-detection it relies on is exactly what the freeze suspends.
        get(host, "/freeze?on=0")
        get(host, "/sc?~")  # re-detect, which recovers the picture
