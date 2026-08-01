"""Regression tests for this fork's firmware changes, run against a live unit.

    pytest --host=gbscontrol.local
    pytest --host=192.168.1.20 --preset-save

Covers the /getreg and /setreg endpoints, printVideoTimings() reaching the web
console, and the preset save path. The timings tests need a GBS_DEBUG=1
build; they fail rather than skip if the console is silent, because a silent
console is the regression they exist to catch.
"""

import re
import time

import pytest

from gbs_unit import (
    get,
    get_json,
    parse_timings,
    read_reg,
    read_word,
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

LOCK_TIMEOUT = 45.0  # the firmware's own RGBHV retry loop is slow
HOLD_SECONDS = 15.0  # long enough to catch a lock the sync watcher takes back

# Console label -> the registers behind each printed column, as
# (segment, low byte, field mask). Widths are from tv5725.h. Both the scaling
# and the HD bypass blocks are here so the cross-check runs in either mode.
TIMING_REGISTERS = {
    "HT / scale": [(3, 0x01, 0x0FFF), (3, 0x16, 0x03FF)],  # VDS_HSYNC_RST, VDS_HSCALE
    "VT / scale": [(3, 0x02, 0x07FF), (3, 0x17, 0x03FF)],  # VDS_VSYNC_RST, VDS_VSCALE
    "HD_HSYNC_RST": [(1, 0x37, 0x07FF)],
    "HD_INI_ST": [(1, 0x39, 0x07FF)],
}


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


# --- printVideoTimings() ----------------------------------------------------


@pytest.fixture(scope="session")
def timings(host, console):
    """The block /sc?, prints to the web console."""
    console.drain()
    get(host, "/sc?,")
    lines = console.collect()
    rows = parse_timings(lines)
    assert rows, (
        "/sc?, printed nothing parseable to the web console. Is this a "
        f"GBS_DEBUG=1 build? Raw output: {lines!r}"
    )
    return rows


def test_sc_comma_prints_timings(timings):
    assert len(timings) >= 5, f"only got {list(timings)}"


def _read_all(host):
    """Every register the console block covers, read over HTTP."""
    return {
        (label, index): read_word(host, segment, register, mask)
        for label, columns in TIMING_REGISTERS.items()
        for index, (segment, register, mask) in enumerate(columns)
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
    console.drain()
    get(host, "/sc?,")
    rows = parse_timings(console.collect())
    after = _read_all(host)

    compared, moved = {}, {}
    for label, columns in TIMING_REGISTERS.items():
        if label not in rows:
            continue
        for index, (segment, register, _) in enumerate(columns):
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


def test_the_sync_processor_holds_a_lock(host, source):
    """H and V active at the same moment is what every downstream stage waits
    for. The no-sync fault shows up here twice over: as a lock that never
    arrives, and as one that arrives and collapses seconds later when the sync
    watcher reconfigures the sync processor underneath it."""
    locked = wait_for(
        lambda: (read_reg(host, *STATUS_16) or 0) & LOCKED == LOCKED, timeout=LOCK_TIMEOUT
    )
    assert locked, f"no H and V lock within {LOCK_TIMEOUT:.0f}s of asking"

    samples = []
    deadline = time.monotonic() + HOLD_SECONDS
    while time.monotonic() < deadline:
        samples.append(read_reg(host, *STATUS_16) or 0)

    held = sum(1 for s in samples if s & LOCKED == LOCKED)
    assert held == len(samples), (
        f"the lock did not hold: {held} of {len(samples)} samples over "
        f"{HOLD_SECONDS:.0f}s, values seen {sorted({s for s in samples})}"
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
