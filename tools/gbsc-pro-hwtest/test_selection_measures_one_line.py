"""The input formatter's scan never half-describes the line, across a selection.

Five registers carry the scan: three route the line doubler, and the counter and
the decimation size the line for it -- a count of IF units beside what an IF unit
IS. The source's field rate is timed off this block's vertical, so a half-applied
scan is measured rather than merely stored.

The reference sampling clock cannot be represented undoubled -- 2506 ADC samples
truncate to 458 in an eleven-bit counter -- so the scan a selection leaves is that
clock's and not the arriving source's. Two writers used to put half of each on the
chip: the selection installed the counter for the reference line and the solve
then wrote the three path registers for the source, the counter write refusing.
Measured at that time: `PRGRSV` 1, `LD_RAM_BYPS` 1, `LD_SEL_PROV` 1 against
`HS_DEC_FACTOR` 1 and a counter of 1253.

**THIS IS NOT THE DOUBLED FIELD RATE**, which survives the fix and is refuted
against the mixed scan, the doubled scan, the divider and the sync arrangement in
turn. What this guards is the single owner.

docs/investigations/the-field-rate-reads-exactly-double-after-a-sync-reset.md
"""

import time

import pytest

from gbs_unit import read_fields, select_input, wait_for_acquisition

# The three the line doubler is routed by, and the two that size the line for it.
SCAN = ["IF_PRGRSV_CNTRL", "IF_LD_RAM_BYPS", "IF_LD_SEL_PROV",
        "IF_HS_DEC_FACTOR", "IF_HSYNC_RST", "PLLAD_MD"]

# A 0.4 s poll resolves a 300 ms window badly and is still the right instrument
# for CONFIGURATION registers: the engine writes them and they are stable between
# writes, so a sample either side of one write is a sample of two settled states.
POLL_S = 0.08

# Long enough for the selection, detection and the solve. Measured, `ypbpr`
# reaches its last divider install 5.7..6.2 s after the request.
TAPE_LIMIT_S = 30.0


def scan_disagrees(got):
    """Whether these five describe two different lines.

    Doubled, the three path registers read 0 and an IF unit is two ADC samples;
    progressive they read 1 and a unit is one sample. Either the whole set says
    one thing or the block is counting a line that is not arriving.
    """
    if any(value is None for value in got.values()):
        return None
    doubled = got["IF_HS_DEC_FACTOR"] == 1
    want = 0 if doubled else 1
    path = (got["IF_PRGRSV_CNTRL"], got["IF_LD_RAM_BYPS"], got["IF_LD_SEL_PROV"])
    return path != (want, want, want)


@pytest.fixture
def scan_samples(host, console, on_vga):
    """Every reading of the five taken across one `vga` -> `ypbpr` selection.

    Polled from the same process that issues the selection, because the window
    the two writers left open is a few hundred milliseconds inside it.
    """
    console.drain()
    select_input(host, "ypbpr")

    started = time.monotonic()
    samples = []
    while time.monotonic() - started < TAPE_LIMIT_S:
        try:
            got = read_fields(host, SCAN)
        except Exception:  # noqa: BLE001 - /getreg blocks while loop() searches
            got = None
        if got:
            samples.append((time.monotonic() - started, got))
        time.sleep(POLL_S)

    assert console.alive, "the console pump died during the selection"
    assert wait_for_acquisition(host, "ypbpr", limit_s=TAPE_LIMIT_S) is not None, (
        "ypbpr did not acquire, so the selection this judges did not complete")

    yield samples


def test_the_scan_describes_one_line_throughout_a_selection(scan_samples):
    mixed = [(at, got) for at, got in scan_samples if scan_disagrees(got)]

    assert scan_samples, (
        "no register read came back across the whole selection, so nothing was "
        "sampled: /getreg is deferred to loop(), which detection can hold for "
        "seconds at a time")
    assert not mixed, (
        "%u of %u samples read a scan describing two lines, the first at %.2fs: "
        "%r -- the three path registers and the decimation disagree, so the block "
        "counts several lines per line and the field rate timed off its vertical "
        "comes back a multiple of the truth"
        % (len(mixed), len(scan_samples), mixed[0][0], mixed[0][1]))
