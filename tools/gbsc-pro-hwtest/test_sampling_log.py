"""The /samplinglog monitor stream, run against a live unit.

    pytest --host=<ip> test_sampling_log.py

Covers the shape of the log line and, more usefully, that the IF status byte in
it holds the registers it claims to. That byte is why the stream exists in this
form: HPERIOD_IF gives no indication of its own validity -- STATUS_IF_HT_OK
reads 1 while the value is garbage -- so the bit that does say has to arrive in
the same sample as the value it judges.

Monitor mode is used throughout, never sweep: it writes no register, so this is
safe on a working unit and needs no opt-in flag.

The endpoint itself is compiled in only at GBS_SAMPLING_LOG=1, so against a
default build every test here skips.
"""

import json
import os

import pytest

from gbs_unit import field_from, get, read_segment

CATALOGUE = json.load(open(
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "tv5725_registers.json")))

# Bit position in the packed byte -> field name, as SamplingLog.cpp packs them.
IF_STATUS_BITS = ["STATUS_IF_HT_OK", "STATUS_IF_VT_OK", "STATUS_IF_HT_BAD",
                  "STATUS_IF_VT_BAD", "STATUS_IF_NO_SYNC"]

INTERVAL_MS = 16          # 2 ms asks for more lines a second than the console carries
DURATION_MS = 3000
COLLECT_S = 6.0


def start_monitor(host):
    """Queue a monitor run, or skip if this firmware has no /samplinglog.

    An unregistered route answers 500 with an empty body, not 404: the sketch
    registers no onNotFound, so ESPAsyncWebServer's catch-all handler runs with
    no callback set and sends 500 (WebHandlerImpl.h, handleRequest).
    """
    status, body = get(host, f"/samplinglog?ms={INTERVAL_MS}&for={DURATION_MS}")
    if status == 500 and not body.strip():
        pytest.skip("no /samplinglog on this firmware; rebuild with "
                    "make -C build GBS_SAMPLING_LOG=1")
    assert status == 200, f"/samplinglog answered {status}: {body}"


def rows(host, console):
    """The header's column names, and the data rows as lists of ints."""
    start_monitor(host)
    lines = console.collect(COLLECT_S)
    header, data = None, []
    for line in lines:
        if not line.startswith("smp,"):
            continue
        parts = line.split(",")
        if parts[1] == "header":
            header = parts[2:]
        elif parts[1] == "done":
            continue
        elif header is not None:
            try:
                data.append([int(p) for p in parts[1:]])
            except ValueError:
                pass
    return header, data, lines


@pytest.fixture
def stream(host, console):
    console.drain()
    header, data, lines = rows(host, console)
    assert header, (
        f"/samplinglog printed no header in {COLLECT_S}s. Console "
        f"{'connected' if console.alive else f'dropped ({console.failure})'}. "
        f"Raw: {lines[:20]!r}")
    assert data, f"header but no samples. Raw: {lines[:20]!r}"
    return header, data


def test_the_log_line_carries_the_if_status_byte(stream):
    header, data = stream
    assert header[-1] == "ifbits", (
        f"last column is {header[-1]!r}, expected 'ifbits' — "
        f"header was {header}")
    for row in data:
        assert len(row) == len(header), (
            f"row has {len(row)} fields against {len(header)} header columns: {row}")
        assert 0 <= row[-1] < (1 << len(IF_STATUS_BITS)), (
            f"ifbits {row[-1]} does not fit {len(IF_STATUS_BITS)} bits")


def test_the_if_status_byte_holds_the_registers_it_names(host, stream):
    """Decoded, the byte must agree with the same fields read over /getregs.

    Bracketed and compared only where a bit held still across the window, the
    same way the console timings are checked: these are live measurements, and a
    bit that moved between the log and the read proves nothing either way.
    """
    _, data = stream

    def read_bits():
        registers = read_segment(host, 0)
        if not registers:
            return None
        out = {}
        for name in IF_STATUS_BITS:
            f = CATALOGUE[name]
            out[name] = field_from(registers, f["reg"], f["off"], f["width"])
        return out

    before, after = read_bits(), read_bits()
    assert before and after, "could not read segment 0"

    steady = {n: before[n] for n in IF_STATUS_BITS if before[n] == after[n]}
    assert steady, f"every IF status bit moved while sampling: {before} then {after}"

    logged = data[-1][-1]
    disagreed = {
        name: (value, (logged >> IF_STATUS_BITS.index(name)) & 1)
        for name, value in steady.items()
        if value != (logged >> IF_STATUS_BITS.index(name)) & 1
    }
    assert not disagreed, (
        f"ifbits {logged:#07b} disagrees with /getregs on "
        f"{ {n: f'reg={r} log={l}' for n, (r, l) in disagreed.items()} }")
