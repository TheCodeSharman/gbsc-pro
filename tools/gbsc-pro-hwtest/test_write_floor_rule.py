"""The write-floor rule, and that a session is scored by the rule under test.

Two rules are live on this bench and they govern different marks. The width rule
governs the zoom shear above the clamp; the floor scale rule governs the write
floor, where the width rule explicitly says it has nothing to offer. A session
that records one rule's prediction against the other's marks reads as a
refutation that never happened.

docs/known-issues.md carries what each one is measured on, and the corner rule
it replaced -- refuted by 11 marks with the register jogged four units either
side of the write origin and the picture clean at all of them.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shear


def a_state(hscale=320, dis_hb_sp=143, hb_sp=8, capture=534, hb_st=1851):
    """Scale 320 on the write floor: clean, at any corner value."""
    return {"VDS_HB_SP": hb_sp, "VDS_HB_ST": hb_st,
            "VDS_DIS_HB_SP": dis_hb_sp, "VDS_DIS_HB_ST": hb_st,
            "VDS_HSCALE": hscale, "IF_HB_SP2": 613, "IF_HB_ST2": 613 + capture}


def test_a_multiple_of_64_is_called_clean():
    assert shear.floor_scale(a_state(hscale=320)) == "clean"
    assert shear.floor_scale(a_state(hscale=256)) == "clean"


def test_every_other_scale_on_the_floor_is_called_corrupt():
    for hscale in (257, 288, 304, 316, 321, 324, 333):
        assert shear.floor_scale(a_state(hscale=hscale)) == "corrupt"


def test_the_corner_does_not_change_the_call():
    # The refutation, kept as a test: jogging the corner four units either side
    # of the write origin left the picture clean at all 11 marks.
    for corner in range(139, 147):
        assert shear.floor_scale(a_state(dis_hb_sp=corner)) == "clean"


def test_the_two_readings_separate_above_the_swept_band():
    # 384 and 448 are multiples of 64 that do not divide 25600; 400 divides
    # 25600 and is not a multiple of 64. A mark at any of them decides.
    assert shear.floor_scale(a_state(hscale=384)) == "clean"
    assert shear.floor_scale(a_state(hscale=448)) == "clean"
    assert shear.floor_scale(a_state(hscale=400)) == "corrupt"


def test_a_session_is_scored_by_the_rule_it_was_given():
    session = shear.Session("host", "", tool="creep_corner",
                            rule=shear.FloorScaleRule)
    session.mark(144, "clean", a_state(dis_hb_sp=144))

    assert session.marks[0]["predicted"] == "clean"
    assert "the floor scale rule" in session.score()


def test_a_session_defaults_to_the_width_rule():
    session = shear.Session("host", "", rule=None)
    session.mark(8, "clean", a_state())

    assert session.marks[0]["predicted"] == shear.predict(a_state())


def test_a_margin_moves_the_window_and_the_corner_together():
    # Moving VDS_HB_SP alone opens the display window before the write starts,
    # which shows unwritten memory down the left and is an artefact of its own.
    targets = shear.margin_targets(a_state(hb_sp=8, dis_hb_sp=143), 4)

    assert targets == {"VDS_HB_SP": 12, "VDS_DIS_HB_SP": 147}


def test_a_margin_step_holds_the_memory_windows_parity():
    # An even width shears, so a margin that flips the parity measures the shear
    # rather than the floor.
    held = a_state(hb_sp=12)
    for delta in (2, 4, 6, -2):
        targets = shear.margin_targets(held, delta)
        assert (held["VDS_HB_ST"] - targets["VDS_HB_SP"]) % 2 == 1


def test_a_margin_below_the_floor_is_refused():
    assert shear.margin_targets(a_state(hb_sp=8), -2) is None


def test_a_session_records_what_the_verdict_was_about():
    # A verdict with no feature named is what let one sweep's marks stand for
    # several different artefacts at once.
    session = shear.Session("host", "", tool="creep_zoom",
                            judging="image stability")
    session.mark(320, "corrupt", a_state())

    assert session.marks[0]["judging"] == "image stability"


def test_a_session_says_when_nothing_was_named():
    session = shear.Session("host", "")
    session.mark(320, "corrupt", a_state())

    assert session.marks[0]["judging"] == "unstated"
