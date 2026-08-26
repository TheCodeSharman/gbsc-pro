"""Host tests for trace_oracle -- no hardware."""

import pytest

from trace_oracle import common_subsequence, oracle, parse


def test_a_segment_select_places_the_writes_that_follow_it():
    assert parse(["1 W F0:01", "2 W 65:BE"]) == [(1, 0x65, 0xBE)]


def test_a_segment_select_is_not_itself_a_write():
    assert parse(["1 W F0:05"]) == []


def test_the_segment_persists_until_the_next_select():
    trace = ["1 W F0:03", "2 W 10:AA", "3 W 11:BB", "4 W F0:04", "5 W 12:CC"]
    assert parse(trace) == [(3, 0x10, 0xAA), (3, 0x11, 0xBB), (4, 0x12, 0xCC)]


def test_a_multi_byte_write_covers_consecutive_registers():
    assert parse(["1 W F0:02", "2 W 20:AABBCC"]) == [
        (2, 0x20, 0xAA), (2, 0x21, 0xBB), (2, 0x22, 0xCC)]


def test_a_write_before_any_segment_select_is_marked_not_dropped():
    """A trace that starts mid-stream must look wrong, not merely short."""
    assert parse(["1 W 65:BE"]) == [(-1, 0x65, 0xBE)]


def test_lines_that_are_not_writes_are_ignored():
    assert parse(["=== TRACE BEGIN std=1 ===", "1 W F0:01", "2 W 65:BE",
                  "running frame sync"]) == [(1, 0x65, 0xBE)]


def test_the_common_subsequence_keeps_order():
    a = [("a",), ("b",), ("c",), ("d",)]
    b = [("a",), ("x",), ("c",), ("d",)]
    assert common_subsequence(a, b) == [("a",), ("c",), ("d",)]


def test_the_common_subsequence_of_disjoint_runs_is_empty():
    assert common_subsequence([("a",)], [("b",)]) == []


def test_the_oracle_keeps_only_what_every_run_wrote():
    runs = [
        ["1 W F0:01", "2 W 10:AA", "3 W 11:01"],   # 11 varies
        ["1 W F0:01", "2 W 10:AA", "3 W 11:02"],
        ["1 W F0:01", "2 W 10:AA", "3 W 11:03"],
    ]
    result = oracle(runs)
    assert result["stable"] == [(1, 0x10, 0xAA)]
    assert result["runLengths"] == [2, 2, 2]
    assert result["variable"] == [1, 1, 1]


def test_a_write_every_run_makes_survives_even_when_others_move():
    runs = [
        ["1 W F0:00", "2 W 40:01", "3 W 41:AA", "4 W 42:09"],
        ["1 W F0:00", "2 W 40:01", "3 W 41:BB", "4 W 42:09"],
        ["1 W F0:00", "2 W 40:01", "4 W 42:09"],
    ]
    assert oracle(runs)["stable"] == [(0, 0x40, 0x01), (0, 0x42, 0x09)]


def test_one_run_cannot_be_an_oracle():
    """Nothing distinguishes a stable write from a variable one in one run."""
    with pytest.raises(ValueError):
        oracle([["1 W F0:01", "2 W 65:BE"]])
