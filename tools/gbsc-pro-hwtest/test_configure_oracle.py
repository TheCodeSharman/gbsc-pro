"""configure_oracle's field set and its comparison, against synthetic readings.
No hardware."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import configure_oracle as co


def reading(oracle=None, context=None, geometry=None, picture=None):
    base = dict(
        oracle=dict.fromkeys(co.ORACLE, 0),
        context={"PLLAD_MD": 2200, "STATUS_SYNC_PROC_HTOTAL": 2200,
                 "STATUS_SYNC_PROC_VTOTAL": 311, "SP_SOG_MODE": 0},
        geometry={"oh": 237, "eh": 688, "ov": 72, "ev": 512, "ch": 1101,
                  "cv": 624, "fh": 98, "fv": 1, "poh": 2153, "peh": 6249,
                  "pov": 1154, "pev": 8205, "aspect": 13333, "shaped": True,
                  "present": True, "state": "acquired",
                  "lineRateHz": 15625, "lowLineRate": True},
        picture={"left": 242, "right": 254, "top": 2, "bottom": 0,
                 "spread": 109.4, "luma": 81.3, "flat": False},
    )
    for group, moved in (("oracle", oracle), ("context", context),
                         ("geometry", geometry), ("picture", picture)):
        if moved:
            base[group] = dict(base[group], **moved)
    return base


def failures(was, now):
    return [d for d in co.compare(was, now) if d.severity == co.FAIL]


def notes(was, now):
    return [d for d in co.compare(was, now) if d.severity == co.NOTE]


def test_the_coast_pair_is_not_an_oracle_field():
    assert "SP_PRE_COAST" not in co.ORACLE and "SP_POST_COAST" not in co.ORACLE


def test_a_moved_sync_processor_field_fails_the_comparison():
    got = failures(reading(), reading(oracle={"SP_CLAMP_MANUAL": 1}))
    assert [(d.key, d.was, d.now) for d in got] == [("SP_CLAMP_MANUAL", 0, 1)]


def test_a_solved_geometry_key_moving_fails_the_comparison():
    got = failures(reading(), reading(geometry={"ch": 1100}))
    assert [(d.key, d.was, d.now) for d in got] == [("ch", 1101, 1100)]


def test_the_divider_is_computed_and_so_fails_rather_than_notes():
    # A divider left on another mode's value, which is what /sc?~ clears.
    got = failures(reading(), reading(context={"PLLAD_MD": 1822}))
    assert [d.key for d in got] == ["PLLAD_MD"]


def test_the_measured_line_rate_moving_is_a_note_rather_than_a_failure():
    moved = reading(geometry={"lineRateHz": 15620})
    assert not failures(reading(), moved)
    assert [d.key for d in notes(reading(), moved)] == ["lineRateHz"]


def test_a_flat_field_fails_however_the_spread_is_tolerated():
    got = failures(reading(), reading(picture={"flat": True, "spread": 3.1}))
    assert [d.key for d in got] == ["flat"]


def test_a_tenth_of_a_grey_level_is_not_a_difference():
    assert not co.compare(reading(), reading(picture={"spread": 109.5,
                                                      "luma": 81.4}))


def test_a_margin_moving_is_a_note_because_the_link_places_it():
    moved = reading(picture={"top": 3, "bottom": 1})
    assert not failures(reading(), moved)
    assert sorted(d.key for d in notes(reading(), moved)) == ["bottom", "top"]


def test_a_different_sync_type_is_not_the_same_path():
    assert not co.same_path(reading(), reading(context={"SP_SOG_MODE": 1}))


def test_a_different_source_mode_is_not_the_same_path():
    assert not co.same_path(reading(),
                            reading(context={"STATUS_SYNC_PROC_VTOTAL": 309}))


def test_an_interlaced_count_alternating_by_one_line_is_the_same_path():
    assert co.same_path(reading(),
                        reading(context={"STATUS_SYNC_PROC_VTOTAL": 312}))


def test_saving_one_path_leaves_the_others_alone():
    held = {"ypbpr-480i": reading(context={"SP_SOG_MODE": 1})}
    got = co.merged(held, "vga-sync0", reading())
    assert got["ypbpr-480i"] == held["ypbpr-480i"]
    assert got["vga-sync0"]["oracle"] == reading()["oracle"]


def test_the_excluded_pairs_are_reported_and_never_compared():
    was = reading()
    was["excluded"] = {"SP_PRE_COAST": 7, "SP_POST_COAST": 6}
    now = reading()
    now["excluded"] = {"SP_PRE_COAST": 9, "SP_POST_COAST": 9}
    assert co.compare(was, now) == []


BLACK = {"spread": 0.0, "luma": 0.0, "flat": True,
         "left": 1920, "right": 0, "top": 1080, "bottom": 0}
CARD = dict(BLACK, spread=109.4, luma=81.3, flat=False,
            left=242, right=254, top=2, bottom=0)


def bursts(*rounds):
    held = list(rounds)
    return lambda: held.pop(0) if held else []


def test_a_black_frame_inside_a_burst_is_passed_over():
    got = co.settled_picture(bursts([BLACK, BLACK, CARD]), pause=lambda _: None)
    assert got["flat"] is False and got["spread"] == 109.4


def test_an_all_black_burst_is_retried_before_the_field_is_called_flat():
    got = co.settled_picture(bursts([BLACK, BLACK], [BLACK], [CARD]),
                             pause=lambda _: None)
    assert got["flat"] is False


def test_a_field_that_stays_flat_is_reported_flat_rather_than_polled_for_ever():
    got = co.settled_picture(lambda: [BLACK], pause=lambda _: None, limit_s=0.0)
    assert got["flat"] is True


def test_a_count_alternating_by_one_line_moves_the_coast_window_within_tolerance():
    # 312 held lines place it at 1672 and 313 at 1666, and a count alternating
    # by one is the same source -- SteadyRun::agree().
    assert not co.compare(reading(oracle={"SP_H_CST_SP": 1672}),
                          reading(oracle={"SP_H_CST_SP": 1666}))


def test_a_coast_window_left_where_init_put_it_is_a_failure():
    got = failures(reading(oracle={"SP_H_CST_SP": 1672}),
                   reading(oracle={"SP_H_CST_SP": 0x100}))
    assert [d.key for d in got] == ["SP_H_CST_SP"]


def test_a_small_rate_derived_field_still_tolerates_one_unit():
    # The firmware places the clamp with its own withinOneOf guard.
    assert not co.compare(reading(oracle={"SP_CS_CLP_SP": 18}),
                          reading(oracle={"SP_CS_CLP_SP": 19}))


def test_a_switch_is_exact_and_tolerates_nothing():
    got = failures(reading(oracle={"SP_EXT_SYNC_SEL": 0}),
                   reading(oracle={"SP_EXT_SYNC_SEL": 1}))
    assert [d.key for d in got] == ["SP_EXT_SYNC_SEL"]


# The 480i Wii, two acquisitions of one source on one image: the held count
# lands on either member of the alternating pair and the capture window follows
# it. Measured cv 520 / ev 485 against one and 522 / 487 against the other, with
# the framing proportions rounding by three ten-thousandths beside them.
INTERLACED = {"STATUS_SYNC_PROC_VTOTAL": 259, "SP_SOG_MODE": 1}
LANDED = {"ov": 35, "ev": 485, "cv": 520, "fv": 1, "pov": 673, "pev": 9327}
ALTERNATED = dict(LANDED, ev=487, cv=522, pov=670, pev=9330)


def test_a_held_count_one_line_apart_is_within_the_capture_windows_tolerance():
    was = reading(context=INTERLACED, geometry=LANDED)
    now = reading(context=INTERLACED, geometry=ALTERNATED)
    assert not failures(was, now)


def test_the_framing_proportions_note_rather_than_fail_because_a_solve_rounds_them():
    was = reading(context=INTERLACED, geometry=LANDED)
    now = reading(context=INTERLACED, geometry=ALTERNATED)
    assert sorted(d.key for d in notes(was, now)) == ["pev", "pov"]


def test_a_capture_height_further_than_one_line_out_still_fails():
    was = reading(context=INTERLACED, geometry=LANDED)
    now = reading(context=INTERLACED, geometry=dict(LANDED, cv=540))
    assert [d.key for d in failures(was, now)] == ["cv"]
