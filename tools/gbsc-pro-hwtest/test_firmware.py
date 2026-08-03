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
    spiffs_dir,
    spiffs_read,
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
LOCKED = HS_ACTIVE | VS_ACTIVE

SYNC_PROC_00 = (5, 0x20)
EXT_SYNC_SEL = 1 << 3  # take V sync off the HS line instead of the VS input

PLLAD_LOCK = 1 << 7  # seg 0 0x09
# HPERIOD_IF .. VTOTAL: everything the sync check correlates against, in one burst
SYNC_SAMPLE_RANGE = (0x06, 0x1C)

LOCK_TIMEOUT = 45.0  # the firmware's own RGBHV retry loop is slow
HOLD_SECONDS = 15.0  # long enough to catch a lock the sync watcher takes back

# Console label -> the registers behind each printed column, as
# (segment, low byte, bit offset, width), copied from the UReg declarations in
# tv5725.h. Both the scaling and the HD bypass blocks are here so the cross-check
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
    Registers that move on their own would make this flap, so it reads segment 3,
    which is output configuration and holds still."""
    registers = read_segment(host, 3, 0x00, 0x0F)

    assert registers is not None, "/getregs did not answer with a usable range"
    mismatched = {
        f"s3:{reg:#04x}": (burst, read_reg(host, 3, reg))
        for reg, burst in registers.items()
        if burst != read_reg(host, 3, reg)
    }
    assert not mismatched, f"/getregs vs /getreg (burst, single): {mismatched}"


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
        lines = console.collect()
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


@pytest.fixture(scope="session")
def timings(host, console):
    """The block /sc?, prints to the web console."""
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
    """H and V active at the same moment is what every downstream stage waits
    for. The no-sync fault shows up here twice over: as a lock that never
    arrives, and as one that arrives and collapses seconds later when the sync
    watcher reconfigures the sync processor underneath it.

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

FIRMWARE = Path(__file__).resolve().parents[2] / "GBSC-Pro-Source code" / "gbs-control"

PRESET_DISPLAY_CLOCK = (1, 0x2D)  # GBS_PRESET_DISPLAY_CLOCK in tv5725.h

# The eight values externalClockGenResetClock() maps to a frequency, plus the
# sentinel it parks at. A divider showing up in the stash register means someone
# has started using it to hold state again.
DISPLAY_CLOCK_DIVIDERS = {0x25, 0x35, 0x45, 0x55, 0x65, 0x85, 0x95, 0xA5}
DISPLAY_CLOCK_SENTINEL = 0x75

# The only values the preset arrays themselves put at s1_2D: 0 in the twelve
# scaling presets, 0x25 in pal_downscale/ntsc_downscale.
PRESET_STASH_VALUES = {0x00, 0x25}


def _function_body(source, name):
    """The braced body of a C function, by brace matching from its opening `{`."""
    start = source.index(f"void {name}(")
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace : index + 1]
    raise AssertionError(f"unbalanced braces in {name}()")


def test_display_clock_stash_is_not_a_preset_register():
    """The divider must not be stashed anywhere a preset load can overwrite.

    Asserts both halves, so neither can drift: that the presets really do write
    s1_2D (making it unusable as a stash), and that the firmware no longer
    writes a divider there.
    """
    segment, register = PRESET_DISPLAY_CLOCK

    tv5725 = (FIRMWARE / "tv5725.h").read_text(errors="replace")
    declaration = re.search(
        r"UReg<(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),[^>]*>\s*GBS_PRESET_DISPLAY_CLOCK", tv5725
    )
    assert declaration, "GBS_PRESET_DISPLAY_CLOCK is not declared in tv5725.h"
    assert (int(declaration.group(1), 16), int(declaration.group(2), 16)) == (segment, register), (
        f"GBS_PRESET_DISPLAY_CLOCK moved to s{declaration.group(1)}_{declaration.group(2)}; "
        "recheck whether the new address is inside preset coverage before trusting this test"
    )

    label = f"// s{segment}_{register:02X}"
    clobbering = sorted(
        path.name
        for path in FIRMWARE.glob("*.h")
        if label in path.read_text(errors="replace").upper().replace("// S", "// s")
    )
    assert clobbering, (
        f"no preset array writes s{segment}_{register:02X} any more. If that is "
        "deliberate the register is safe to stash in again, but this test's premise "
        "is gone and it should be rewritten rather than deleted."
    )

    sketch = (FIRMWARE / "gbs-control.ino").read_text(errors="replace")
    writes = [
        line.strip()
        for line in sketch.splitlines()
        if "GBS_PRESET_DISPLAY_CLOCK::write" in line
    ]
    assert not writes, (
        f"the display clock is being stashed in s{segment}_{register:02X} again, which "
        f"{len(clobbering)} preset arrays overwrite ({', '.join(clobbering[:3])}...). "
        f"A preset load wipes it, the restore is skipped, and the Si5351 keeps a stale "
        f"frequency — blank output with correct-looking registers. Offending lines: {writes}"
    )


def test_display_clock_lookup_has_no_silent_fallthrough():
    """externalClockGenResetClock() must assign a frequency on every path.

    The lookup is a chain of `if (activeDisplayClock == ...)`. Without a final
    unconditional `else`, an unrecognised value — the 0x75 sentinel above all —
    leaves rto->freqExtClockGen holding whatever the last preset put there, and
    that stale value is programmed into the Si5351 regardless.
    """
    sketch = (FIRMWARE / "gbs-control.ino").read_text(errors="replace")
    body = _function_body(sketch, "externalClockGenResetClock")

    assert "freqExtClockGen" in body, "externalClockGenResetClock() no longer sets the frequency"

    # a terminal `else` — i.e. one not immediately followed by `if`
    fallthrough_guard = re.search(r"\belse\s*(?!if\b)\{", body)
    assert fallthrough_guard, (
        "no terminal `else` in externalClockGenResetClock(): an unmapped display "
        "clock silently reuses the previous frequency. Add a fallback that assigns "
        "rto->freqExtClockGen and says so, rather than falling through."
    )


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


# --- preset save ------------------------------------------------------------


def test_no_leftover_temp_presets(host):
    """savePresetToSPIFFS() writes '<preset>~' and renames it into place. One
    left behind means a save failed part way and did not clean up."""
    files = spiffs_dir(host)

    assert files is not None, "/spiffs/dir did not answer"
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
    before = spiffs_dir(host)
    assert before is not None, "/spiffs/dir did not answer before the save"
    prefs_before = spiffs_read(host, "/preferencesv2.txt")
    assert prefs_before is not None, "could not read the preferences file"

    console.drain()
    status, _ = get(host, "/uc?4")
    assert status == 200
    output = console.collect(5.0)

    after = spiffs_dir(host)
    assert after is not None, "/spiffs/dir did not answer after the save"
    assert [f for f in after if f.endswith("~")] == [], f"temp file left behind: {after}"

    presets = [f for f in after if f.startswith("/preset_")]
    if not presets:
        # Refused. The preference must not have moved, and it must have said why.
        assert spiffs_read(host, "/preferencesv2.txt") == prefs_before, (
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
    content = spiffs_read(host, written)
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
