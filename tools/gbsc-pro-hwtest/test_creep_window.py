"""The creep session has to record itself, or the framing is reconstructed later.

A mark that says only "1699, magenta here" is unreadable a day later: the value
means nothing without the capture window, the scale and the divider that were
live when it was taken. Reconstructing those from a dump afterwards gives the
framing at dump time, which is not the same thing, and three sessions were
misread that way.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import creep_window


def a_bank(values):
    """A segment as read_segment() returns it, defaulting to zero."""
    bank = {r: 0 for r in range(0x100)}
    bank.update(values)
    return bank


def a_device(hscale=524, capture=(234, 1124), pllad=2250):
    """The bench: PLLAD_MD 2250, a 890 unit capture, HSCALE 524."""
    segments = {s: a_bank({}) for s in range(6)}
    segments[5][0x12] = pllad & 0xFF
    segments[5][0x13] = pllad >> 8
    segments[1][0x1A] = capture[0] & 0xFF
    segments[1][0x1B] = capture[0] >> 8
    segments[1][0x18] = capture[1] & 0xFF
    segments[1][0x19] = capture[1] >> 8
    segments[3][0x16] = hscale & 0xFF
    segments[3][0x17] = hscale >> 8
    return segments


def reader_for(segments):
    return lambda segment: segments.get(segment)


def test_a_mark_records_every_register_the_arithmetic_uses():
    state = creep_window.read_state(reader_for(a_device()))

    for name in ("PLLAD_MD", "IF_HB_SP2", "IF_HB_ST2", "VDS_HSCALE",
                 "VDS_VSCALE", "VDS_HB_ST", "VDS_DIS_HB_ST", "PB_FETCH_NUM"):
        assert name in state, f"{name} is not recorded"
    assert state["PLLAD_MD"] == 2250
    assert (state["IF_HB_SP2"], state["IF_HB_ST2"]) == (234, 1124)
    assert state["VDS_HSCALE"] == 524


def test_the_state_is_read_one_segment_at_a_time():
    """Simultaneity, not speed: a capture from one solve paired with a window
    from another invents discrepancies that were never on the unit."""
    segments = a_device()
    reads = []

    creep_window.read_state(lambda s: (reads.append(s), segments.get(s))[1])

    assert len(reads) == len(set(reads)), f"a segment was read twice: {reads}"
    assert set(reads) <= set(range(6))


def test_the_derived_framing_comes_from_the_same_pass():
    derived = creep_window.derive(creep_window.read_state(reader_for(a_device())))

    assert derived["capture"] == 890
    assert abs(derived["magnification"] - 1024 / 524) < 1e-6
    assert abs(derived["produced"] - 890 * 1024 / 524) < 0.01


def test_a_dropped_read_is_recorded_as_missing_not_as_zero():
    """A segment that did not answer must not read as a framing of zeros."""
    state = creep_window.read_state(lambda s: None)

    assert state["VDS_HSCALE"] is None
    assert creep_window.derive(state)["magnification"] is None


def test_each_mark_keeps_the_framing_it_was_taken_at():
    """Two marks at two zooms, and neither may show the other's scale."""
    session = creep_window.Session("unit")
    session.mark(1849, 66, "first", creep_window.read_state(reader_for(a_device())))
    session.mark(1691, 224, "second",
                 creep_window.read_state(reader_for(a_device(hscale=453,
                                                             capture=(358, 1124)))))

    first, second = session.record()["marks"]
    assert first["registers"]["VDS_HSCALE"] == 524
    assert second["registers"]["VDS_HSCALE"] == 453
    assert first["derived"]["capture"] == 890
    assert second["derived"]["capture"] == 766


def test_the_session_is_written_after_every_mark(tmp_path):
    """A session lost to a crash or a cable is a session repeated on the bench."""
    path = tmp_path / "session.json"
    session = creep_window.Session("unit", path=str(path))

    session.mark(1849, 66, "first", creep_window.read_state(reader_for(a_device())))
    written = json.loads(path.read_text())

    assert written["marks"][0]["value"] == 1849
    assert written["marks"][0]["registers"]["PLLAD_MD"] == 2250
    assert written["host"] == "unit"


def test_a_note_typed_on_the_same_line_as_the_key_is_still_a_mark():
    """`m garbage disappears` is what a person types. Requiring a bare `m` and
    then a prompt drops the mark on the floor and says "? for the key list",
    which reads as a missed keypress rather than as a lost measurement."""
    assert creep_window.mark_note("m garbage disappears") == "garbage disappears"
    assert creep_window.mark_note("m  two  spaces ") == "two  spaces"
    assert creep_window.mark_note("m") is None       # bare m: prompt for it
    assert creep_window.mark_note("q") is False      # not a mark at all
    assert creep_window.mark_note("memory") is False
