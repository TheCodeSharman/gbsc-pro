"""The /testbus sweep, run against a live unit.

    pytest --host=<ip> test_testbus.py

Covers what makes a reading trustworthy rather than the transition counts
themselves. Two properties, both of which a sweep can fail silently:

A selector reads zero transitions when the block is dead AND when the pad the
signal leaves the chip on is off, so the sweep has to drive PAD_BOUT_EN itself
-- otherwise a sweep taken after `calibrateAdcOffset()` reports every one of the
32 selectors dead and says nothing about anything.

And a sync-processor stage carries several signals, so a sweep that selects a
module without a signal reports whichever SP_TEST_SIGNAL_SEL the last caller
left. Two sweeps are then comparable only by luck.

The route is behind GBS_DEBUG, so a 404 or an empty 500 is a build without it.
The pad test needs --freeze: a running engine measures the field rate every
pass and each measurement drives the pad, so the cleared state cannot be held
while automation runs. Nothing here writes flash or moves the picture.
"""

import time

import pytest

import setfield
from gbs_unit import get, read_named

SWEEP_MS = 10          # 32 selectors, so the whole sweep is about a third of a second
COLLECT_S = 8.0

# Restored by every sweep, so a sweep is a reading rather than a state change.
BORROWED = ["PAD_BOUT_EN", "TEST_BUS_SEL", "TEST_BUS_EN", "SP_TEST_MODULE",
            "SP_TEST_SIGNAL_SEL", "SP_TEST_EN", "IF_TEST_SEL", "IF_TEST_EN"]


def queue(host, query):
    status, body = get(host, query)
    if status == 404 or (status == 500 and not body.strip()):
        pytest.skip("no /testbus on this firmware; it is behind GBS_DEBUG")
    assert status == 200, f"{query} answered {status}: {body}"
    assert "queued" in body, f"{query} answered {body}"


def sweep(host, console, sp=None, sig=None, if_=None):
    """The header and the per-selector transition counts of one sweep."""
    query = f"/testbus?ms={SWEEP_MS}"
    if sp is not None:
        query += f"&sp={sp}"
    if sig is not None:
        query += f"&sig={sig}"
    if if_ is not None:
        query += f"&if={if_}"

    console.drain()
    queue(host, query)

    # Drained once and then re-read whole: collect() returns everything since
    # the last drain, so draining between polls loses whatever arrived in the
    # gap -- which shows up as a sweep that never finished.
    deadline = time.time() + COLLECT_S
    while time.time() < deadline:
        header, rows, done = None, {}, False
        for line in console.collect(0.25):
            if line.startswith("tb,header"):
                header = line
            elif line == "tb,done":
                done = True
            elif line.startswith("tb,"):
                parts = line.split(",")
                if len(parts) == 6:
                    rows[int(parts[1])] = int(parts[2])
        if done:
            return header, rows
    pytest.fail(f"{query} never finished: {len(rows)} rows")


def write_named(host, name, value):
    spec = setfield.load_map()[name]
    assert setfield.apply(host, name, spec, value, dry_run=False), \
        f"could not write {name}={value}"


def test_the_sweep_reports_every_selector(host, console):
    _, rows = sweep(host, console)

    assert sorted(rows) == list(range(32)), f"reported {sorted(rows)}"


def test_the_sweep_names_the_signal_it_read(host, console):
    # A count without the signal it was taken on cannot be compared with
    # another, which is what makes two sweeps agree by luck.
    header, _ = sweep(host, console, sp=4, sig=5)

    assert header is not None, "no tb,header line"
    assert "sp=4" in header and "sig=5" in header, header


@pytest.mark.freeze
def test_the_sweep_drives_the_pad_it_needs(host, console):
    """With PAD_BOUT_EN clear the pin carries nothing, so a sweep that leaves
    the bit alone reports 0 on all 32 selectors -- every block dead, where what
    is off is the pad. `calibrateAdcOffset()` clears it at boot.

    Frozen, because every field-rate measurement drives the pad and the engine
    takes one per pass.
    """
    was = read_named(host, "PAD_BOUT_EN")
    get(host, "/freeze?on=1")
    try:
        write_named(host, "PAD_BOUT_EN", 0)
        assert read_named(host, "PAD_BOUT_EN") == 0, "could not clear the pad"

        _, rows = sweep(host, console)

        assert max(rows.values()) > 0, \
            f"every selector dead with the pad cleared beforehand: {rows}"
    finally:
        write_named(host, "PAD_BOUT_EN", was)
        get(host, "/freeze?on=0")


def test_a_sweep_puts_back_everything_it_borrowed(host, console):
    before = {name: read_named(host, name) for name in BORROWED}

    sweep(host, console, sp=4, sig=1, if_=0)

    after = {name: read_named(host, name) for name in BORROWED}
    assert after == before
