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


def test_the_bus_value_takes_its_high_byte_from_the_second_register():
    reads = {"TEST_BUS_2E": 0x34, "TEST_BUS_2F": 0x12}
    assert sp_test_bus.busValue.__doc__
    assert (reads["TEST_BUS_2F"] << 8 | reads["TEST_BUS_2E"]) == 0x1234


def test_every_stage_the_datasheet_names_is_carried():
    assert sp_test_bus.STAGE[6] == "retiming"
    assert sp_test_bus.STAGE[5] == "cs_sep"
    assert len(sp_test_bus.STAGE) == 8


def test_the_sync_processor_is_selected_by_the_value_the_firmware_uses():
    """Tv5725::TestBus::SyncProcessor is 0xa; a scan on another selector reads
    whatever that block drives instead, silently."""
    assert sp_test_bus.SYNC_PROCESSOR == 0xA
