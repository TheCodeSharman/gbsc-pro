"""Two snapshots of different coverage must be reported, never silently diffed.

`dump_registers.py` writes the config range and `snapdiff.py --save` writes every
address. Diffed against each other the smaller one is missing hundreds of keys,
and a key absent from one side is not a value that changed -- it is a question
that was never asked. Reported as a change it invents differences at every
address the narrow dump does not carry, and the orphan-byte pass then formats a
None and crashes, so the mismatch that caused it never reaches the reader.

That matters because the conclusions drawn here from register diffs -- "the
raster registers are identical either side, therefore the encoder re-acquires" --
are only as wide as the narrower snapshot.
"""
import io
from contextlib import redirect_stdout

import snapdiff


def run(before, after, register_map=None):
    out = io.StringIO()
    with redirect_stdout(out):
        snapdiff.diff({"config": before}, {"config": after}, register_map or {})
    return out.getvalue()


def test_a_key_only_one_snapshot_carries_is_not_a_change():
    report = run({"0:30": 1}, {"0:30": 1, "5:80": 0xB6})

    assert "0 bytes differ" in report


def test_the_coverage_gap_is_reported():
    report = run({"0:30": 1}, {"0:30": 1, "5:80": 0xB6, "5:81": 0x84})

    assert "2" in report
    assert "coverage" in report.lower()


def test_a_real_change_inside_the_shared_set_still_reports():
    report = run({"0:30": 1, "5:80": 0xB6}, {"0:30": 2, "5:80": 0xB6})

    assert "1 bytes differ" in report


def test_equal_coverage_reports_no_gap():
    report = run({"0:30": 1}, {"0:30": 2})

    assert "coverage" not in report.lower()
