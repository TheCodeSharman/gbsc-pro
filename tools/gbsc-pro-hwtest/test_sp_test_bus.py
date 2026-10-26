"""sp_test_bus tells a latched value from a live wire before comparing draws.

37 of the sync processor's 64 test-bus pairs dither WITHIN one draw, so a
single read of one varies whether or not the sampling phase did -- which makes
a bare across-draw table read as 44 varying signals when 37 of them are the
read's own timing. No hardware.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import sp_test_bus


def run(*args):
    return subprocess.run(
        [sys.executable, os.path.join(HERE, "sp_test_bus.py"), *args],
        capture_output=True, text=True, timeout=60)


def test_invoking_it_answers_rather_than_exiting_in_silence():
    done = run("--help")
    assert "usage" in done.stdout.lower(), (
        "printed nothing: %r / %r" % (done.stdout[:200], done.stderr[-400:]))


def test_a_missing_host_is_refused():
    assert run().returncode != 0


def test_a_device_row_parses_into_a_pair_and_its_distribution():
    row = sp_test_bus.pairFrom("sptb,6,3,2000,70,16583,8201,6,0,70:12 16583:988")
    assert row == ((6, 3), (2000, 70, 16583, 8201, 6, False))


def test_a_pair_that_held_still_reports_one_distinct_value():
    row = sp_test_bus.pairFrom("sptb,0,0,2000,1856,1856,1856,1,0,1856:2000")
    assert row[1][4] == 1, "distinct is what separates a latched value"


def test_the_header_and_the_done_marker_are_not_readings():
    assert sp_test_bus.pairFrom("sptb,header,module,signal,count reads=2000") is None
    assert sp_test_bus.pairFrom("sptb,done") is None


def test_a_truncated_row_is_not_a_reading():
    """The console drops bursts, and a half-delivered row parsed as a reading
    is a value nothing on the device ever reported."""
    assert sp_test_bus.pairFrom("sptb,6,3,2000,70") is None


def test_every_stage_the_datasheet_names_is_carried():
    assert sp_test_bus.STAGE[6] == "retiming"
    assert sp_test_bus.STAGE[5] == "cs_sep"
    assert len(sp_test_bus.STAGE) == 8


def test_the_sync_processor_is_selected_by_the_value_the_firmware_uses():
    """Tv5725::TestBus::SyncProcessor is 0xa; a scan on another selector reads
    whatever that block drives instead, silently."""
    assert sp_test_bus.SYNC_PROCESSOR == 0xA
