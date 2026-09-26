"""A selection prepares the chip BEFORE detection runs on the arriving source.

The preparation a new source needs -- a reference sampling clock the sync
processor can count in, and the block resets -- used to be reachable only through
detection's FAILURE branch: `syncFound 0` with nothing present goes to
`goLowPowerWithInputDetection()`, which installs it. So a first pass that claimed
the source skipped it, and the chip measured the arriving source through the
previous source's ADC clock -- which the sync processor counts in, leaving nothing
to count. Measured 5 of 44 `ypbpr` acquisitions, taking 25..32 s against a healthy
4.8..7.1 s.

**THE ORDER IS THE TEST, NOT THE DURATION.** A first-pass claim is not
reproducible on demand -- it was 1 in 20, then 4 in 12, then 0 in 12 -- and the
two duration distributions overlap, so a timing threshold flakes rather than
gating. The ordering does not: the preparation either precedes detection's first
report of the selection or it follows it, on every selection, and the two builds
differ there every time.

docs/investigations/a-ypbpr-detection-that-succeeds-first-pass-skips-the-preparation.md
"""

import re
import time

import pytest

from gbs_unit import read_named, select_input, wait_for_acquisition

# What the selection edge says it did, and the divider out of it. **Compared
# against the PREVIOUS source's, never against the constant**: a build that
# stopped installing the reference clock would still report a divider here, and
# the one it reported would be the divider the last source solved.
SELECTION_REPORT = re.compile(r"input selected: .*reference divider (\d+)")

# `DETECT: 455ms, syncFound 0` -- detection reporting a completed pass. The first
# one after a selection is the pass that either claims the arriving source or does
# not, so the preparation has to be in front of it.
DETECTION_REPORT = "DETECT:"

# Long enough for the selection and one detection pass. Measured on the bench, the
# selection reports inside 400 ms and the first DETECT lands 1.0..1.4 s after it;
# a pass that finds nothing takes up to 2.5 s.
TAPE_LIMIT_S = 20.0


def first_index(lines, needle):
    for index, line in enumerate(lines):
        if needle in line:
            return index
    return None


def reported_selection(lines):
    """(index, divider) for the selection edge's own line, or (None, None)."""
    for index, line in enumerate(lines):
        found = SELECTION_REPORT.search(line)
        if found:
            return index, int(found.group(1))
    return None, None


@pytest.fixture
def selection_tape(host, console, on_vga):
    """The divider `vga` solved, and the console across one `vga` -> `ypbpr`
    selection up to and including detection's first report of it.

    One selection serves both assertions: each costs an acquisition, and the
    ordering being asserted is a property of one selection rather than of a run of
    them.
    """
    solved = read_named(host, "PLLAD_MD")
    assert solved, "vga acquired without a divider in PLLAD_MD"

    console.drain()
    select_input(host, "ypbpr")

    started = time.monotonic()
    while time.monotonic() - started < TAPE_LIMIT_S:
        if first_index(console.collect(0), DETECTION_REPORT) is not None:
            break
        time.sleep(0.1)
    lines = console.collect(0)

    assert console.alive, "the console pump died: the tape below is stale"
    assert first_index(lines, DETECTION_REPORT) is not None, (
        "detection never reported a pass inside %.0fs of the selection, so there "
        "is nothing to order the preparation against: %r" % (TAPE_LIMIT_S, lines))

    yield solved, lines

    wait_for_acquisition(host, "ypbpr")


def test_a_selection_installs_a_divider_that_is_not_the_previous_source_s(selection_tape):
    solved, lines = selection_tape
    index, divider = reported_selection(lines)

    assert index is not None, (
        "the selection edge said nothing: nothing marks the reference sampling "
        "clock, the sync reset or the input registers, so neither what was "
        "installed nor its order against detection can be read: %r" % (lines,))
    assert divider != solved, (
        "the selection left the divider at %u, which is what the previous source "
        "solved: the arriving source will be counted through its ADC clock and "
        "there is nothing to count" % solved)


def test_the_chip_is_prepared_before_detection_reports_a_pass(selection_tape):
    _, lines = selection_tape
    index, _ = reported_selection(lines)
    detect = first_index(lines, DETECTION_REPORT)

    assert index is not None, "the selection edge said nothing"
    assert index < detect, (
        "detection reported a pass before the selection had prepared the chip, "
        "so a pass that claims the source measures it through the PREVIOUS "
        "source's ADC clock: %r" % (lines[:detect + 1],))
