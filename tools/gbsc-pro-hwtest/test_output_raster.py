"""Unit tests for output_raster.py -- the output raster model, worked out in
Python before being ported into src/tv5725/.

The validation data is the twelve shipped preset tables. They are not a design to
copy -- the point is to stop shipping them -- but they are ten independent worked
examples of the arithmetic.
"""

import pytest

import output_raster as our


# --- the generator the preset tables were built with ------------------------


def test_every_preset_table_agrees_with_the_htotal_formula():
    """htotal = clock / (fieldRate x vtotal), to within half a percent.

    THIS IS THE FINDING THE WHOLE MODEL RESTS ON. Ten tables, five output modes,
    three dividers, two field rates, and every one of them lands within 0.5% of
    its own divider's rated clock. The formula is not an inference from a couple
    of bench readings -- it is the generator the preset family was authored with.
    """
    for preset in our.SHIPPED_PRESETS:
        nominal = our.DIVIDER_HZ[preset.divider]
        error = preset.demanded_hz() / nominal - 1.0
        assert abs(error) < 0.005, (
            f"{preset.name}: raster {preset.htotal} x {preset.vtotal} at "
            f"{preset.field_rate} Hz demands {preset.demanded_hz() / 1e6:.3f} MHz "
            f"against divider {preset.divider:#04x}'s nominal "
            f"{nominal / 1e6:.1f} MHz -- {error:+.2%}"
        )


def test_the_tables_sit_consistently_above_their_nominal_clock():
    """Every preset asks for MORE than its divider's rating, by 0.2% to 0.5%.

    Systematic, not noise -- ten out of ten, and it is why htotal_for() cannot
    simply be checked against the shipped numbers for equality. UNEXPLAINED: it
    may be deliberate headroom, a different assumed field rate (59.94 rather than
    60 accounts for 0.1% of the NTSC rows but nothing in the PAL ones), or an
    authoring convention. Recorded as an open question rather than modelled,
    because a model fitted to an unexplained 0.4% is a model of a mistake.

    It does not matter in practice: the Si5351 is steered to whatever the raster
    demands (externalClockGenSyncInOutRate, unclamped), so the divider is a seed
    and the excess simply becomes the real clock. The bench proves it -- a 50 Hz
    lock read htotal 1445, the table's value, which demands 81.35 MHz rather than
    the 1438 that exactly 81 MHz would afford.
    """
    errors = [p.demanded_hz() / our.DIVIDER_HZ[p.divider] - 1.0
              for p in our.SHIPPED_PRESETS]
    assert all(e > 0 for e in errors), (
        "a preset now sits below its nominal clock; the 'always slightly over' "
        f"observation no longer holds: {[f'{e:+.2%}' for e in errors]}"
    )
    assert max(errors) < 0.005


def test_vtotal_is_a_property_of_the_output_mode_alone():
    """The PAL and NTSC tables for one output mode ship the SAME vtotal.

    1001, 626/526, 751, 1067, 1126 -- so the frame height is chosen by the output
    mode and the field rate only ever moves htotal. That is what makes a single
    per-mode table of frame heights legitimate rather than a coincidence.
    """
    by_mode = {}
    for preset in our.SHIPPED_PRESETS:
        by_mode.setdefault(preset.mode, set()).add(preset.vtotal)

    shared = {mode: heights for mode, heights in by_mode.items() if len(heights) > 1}
    assert not shared, f"a mode ships two different frame heights: {shared}"

    # **ONE LINE BELOW THE TABLES, FOR EVERY MODE, AND THAT IS THE CORRECTION.**
    # RD-5725-1.1 on VDS_VSYNC_RST: "This field contains vertical total value
    # minus 1", so the standard's total written into it costs a line -- which is
    # what every shipped table does, six for six:
    #
    #     1080p 1126/1125   1024p 1067/1066   480p 526/525
    #      720p  751/750     960p 1001/1000   576p 626/625
    #
    # Asserted as exactly one rather than merely "different", so a mode whose
    # blanking is entered wrongly fails here rather than looking like more of the
    # same defect.
    for mode, heights in by_mode.items():
        shipped = next(iter(heights))
        assert our.MODE_VTOTAL[mode] == shipped - 1, (
            f"MODE_VTOTAL[{mode!r}] is {our.MODE_VTOTAL[mode]}; the tables ship "
            f"{shipped}, and the standard total is one less than the tables'"
        )


# --- the model itself -------------------------------------------------------


def test_htotal_is_floored_against_the_frame_budget():
    """A raster needs htotal x vtotal x fieldRate hertz, so htotal floors.

    Rounding up asks the part for a clock above the target, which is exactly the
    0.2-0.5% the shipped tables do. This model declines to inherit that.
    """
    assert our.htotal_for(108_000_000, 1126, 50.0) == 1918
    assert our.htotal_for(108_000_000, 1126, 60.0) == 1598
    assert our.htotal_for(81_000_000, 1126, 50.0) == 1438


def test_a_raster_that_will_not_fit_its_register_is_refused():
    """VDS_HSYNC_RST is 12 bits. A 240p-class frame height at 162 MHz overflows,
    and returning a wrapped value would roll the picture rather than fail."""
    assert our.htotal_for(162_000_000, 262, 50.0) is None
    assert our.htotal_for(108_000_000, 0, 50.0) is None
    assert our.htotal_for(108_000_000, 1126, 0.0) is None


def test_the_divider_is_the_largest_seed_the_ceiling_allows():
    """The seed only has to put the Si5351 in range, so take the most clock
    available -- which is now 0x95, since the bench demonstrated 129.6 MHz."""
    assert our.divider_for(1126, 50.0) == 0x95
    assert our.divider_for(1126, 60.0) == 0x95

    capped = our.divider_for(1126, 50.0, ceiling_hz=108_000_000)
    assert capped == 0x85, "a lowered ceiling must actually hold the seed down"

    raised = our.divider_for(1126, 50.0, ceiling_hz=162_000_000)
    assert raised == 0xA5, "a raised ceiling must actually reach the higher seeds"


def test_each_swept_clock_reproduces_the_raster_the_bench_ran():
    """The three rasters actually applied to the unit, pinned by clock.

    `ceiling_hz` is passed explicitly rather than relying on the default, because
    these assert what the BENCH did -- tying them to the default made them fail
    the moment the measured ceiling moved from 108 to 129.6 MHz, which is a test
    coupled to a config value rather than to a measurement.

    **`vtotal` is now passed for the same reason.** The bench ran these at 1126
    lines; the model's 1080p is 1125 since the one-line correction, and at 1125 it
    produces 1920/2304/2880 instead. Rewriting the expected numbers would have
    quietly turned three measurements into three predictions. Pinning the frame
    height the bench actually used keeps them measurements, and they still pass
    unchanged -- which is the real claim: the model reproduces the bench when
    given the bench's inputs.
    """
    for mhz, expected_htotal in ((108, 1918), (129.6, 2301), (162, 2877)):
        raster = our.raster_for("1080p", 50.0, ceiling_hz=int(mhz * 1e6), vtotal=1126)
        assert (raster.htotal, raster.vtotal) == (expected_htotal, 1126), (
            f"{mhz} MHz should give {expected_htotal} x 1126"
        )


def test_the_default_raster_is_the_best_measured_state():
    """2304 x 1125 at 129.6 MHz: under both limits, and filling more of its raster
    than any other step in the sweep. 1.59x the shipped 1445-px line.

    The bench measured 2301 x 1126 on 2026-08-11 and the sweep above still pins
    that at the frame height it was measured with. This is the same state after
    the one-line correction: the frame gives up a line and the line takes three
    pixels, because htotal is clock / (vtotal x rate).
    """
    raster = our.raster_for("1080p", 50.0)
    assert (raster.htotal, raster.vtotal) == (2304, 1125)
    assert raster.demanded_hz() <= our.WORKING_CEILING_HZ

    shipped = our.preset("pal_1920x1080")
    assert raster.htotal / shipped.htotal > 1.55


def test_iso_frame_rasters_hold_the_frame_time_still():
    """The sweep that needs NO firmware change.

    Changing htotal alone changes the output frame rate, which the firmware then
    fights -- runFrequency() is clamped to 0.06% per pass, so a 33% move would
    take hundreds of iterations. Holding htotal x vtotal constant instead keeps
    the frame time and the pixel clock exactly where they are, so the only thing
    varying is the raster SHAPE the TV has to lock to. That is testable today with
    /setreg and /geometry alone.
    """
    shapes = our.iso_frame_rasters(1918, 1126, tolerance=0.002)

    assert (1918, 1126) in shapes, "the starting shape must be in its own sweep"

    target = 1918 * 1126
    for htotal, vtotal in shapes:
        assert abs(htotal * vtotal / target - 1.0) <= 0.002
        assert htotal <= our.HTOTAL_MAX and vtotal <= our.VTOTAL_MAX

    assert len({h for h, _ in shapes}) == len(shapes), "duplicate htotals in the sweep"


def test_the_sweep_is_short_enough_to_actually_run():
    """A sweep of a thousand one-pixel steps is not an experiment.

    Nearly every htotal has a vtotal pairing inside a 0.2% tolerance, so the
    unfiltered set is ~1279 entries for the 1080p raster -- over two hours at the
    6 s settling rule, to compare shapes that differ by one pixel. Bounded and
    spread instead.
    """
    shapes = our.iso_frame_rasters(1918, 1126, count=9)
    assert len(shapes) <= 10, f"{len(shapes)} shapes is a dense walk, not a sweep"

    htotals = [h for h, _ in shapes]
    gaps = [b - a for a, b in zip(htotals, htotals[1:])]
    assert min(gaps) > 20, (
        f"neighbouring shapes are indistinguishable, so the sweep wastes settling "
        f"time: gaps {gaps}"
    )


def test_the_sweep_is_ordered_and_covers_both_sides_of_the_start():
    """Wider-and-shorter and narrower-and-taller both need trying, because
    nothing says which way a TV's tolerance runs."""
    shapes = our.iso_frame_rasters(1918, 1126, tolerance=0.002)
    htotals = [h for h, _ in shapes]

    assert htotals == sorted(htotals)
    assert min(htotals) < 1918 < max(htotals)


# --- what is NOT modelled ---------------------------------------------------


def test_the_sync_pulse_comes_from_the_standard_not_from_the_tables():
    """CEA-861's sync and back porch, converted to the clock the line will run at.

    Conformant timing is the best available blind guess: EDID is unreachable, so
    the encoder's tolerance cannot be queried.

    **THE CONVERSION IS UNAMBIGUOUS BECAUSE THE STANDARD IS A TIME.** 1080p sync is
    44 pixels at 148.5 MHz = 296.3 ns, and the back porch 148 px = 996.6 ns. Both
    are identical at 50 and 60 Hz -- only the front porch absorbs the rate
    difference -- so sync + back porch is a fixed 1.293 us of every 1080p line. At
    108 MHz that is 32 and 108 pixels.

    Pinned at 108 MHz explicitly: the pulse is a fixed TIME, so its pixel count
    scales with the clock and 0..32 is only the answer at that one clock. The
    bench reads 0..38 at 129.6 MHz and 0..48 at 162, the same 296 ns.
    """
    raster = our.raster_for("1080p", 50.0, ceiling_hz=108_000_000)

    assert (raster.hsync_start, raster.hsync_stop) == (0, 32)
    assert raster.active_start == 32 + 108

    at60 = our.raster_for("1080p", 60.0, ceiling_hz=108_000_000)
    assert at60.hsync_stop == raster.hsync_stop, (
        "sync width is the same time in both standards, so at the same clock it "
        "must be the same pixel count"
    )


def test_the_pulse_is_a_fixed_time_so_it_grows_with_the_clock():
    """0..32 at 108 MHz, 0..38 at 129.6, 0..48 at 162 -- all the same 296 ns.

    These are the values the bench actually applied, so they double as a check
    that the conversion is doing what the standard means rather than what happens
    to look right at one clock.
    """
    for mhz, expected_stop in ((108, 32), (129.6, 38), (162, 48)):
        raster = our.raster_for("1080p", 50.0, ceiling_hz=int(mhz * 1e6))
        assert raster.hsync_stop == expected_stop, f"at {mhz} MHz"
        ns = raster.hsync_stop / raster.demanded_hz() * 1e9
        assert 280 < ns < 320, f"{ns:.0f} ns at {mhz} MHz is not CEA's 296 ns"


def test_the_standard_pulse_is_narrower_than_the_one_shipped():
    """The tables' 20..72 is 1.6x the standard's width at the same clock.

    Recorded as the comparison point for a bench sweep rather than as a default:
    nothing models the output hsync position (CLAUDE.md), 20..72 is one
    measurement at one clock, and the standard is a different claim about what the
    encoder wants. Both need trying.
    """
    standard = our.raster_for("1080p", 50.0)
    measured = our.raster_for("1080p", 50.0, hsync=our.MEASURED_HSYNC_108MHZ)

    standard_width = standard.hsync_stop - standard.hsync_start
    measured_width = measured.hsync_stop - measured.hsync_start

    assert measured_width > standard_width
    assert measured.htotal == standard.htotal, "the pulse must not move the raster"


def test_nobody_should_fit_a_curve_to_the_shipped_sync_widths():
    """Widths across the shipped tables run 52, 104, 128, 132 and 152 with no
    relationship to htotal, and two presets (pal_768x576 196..28, ntsc_720x480
    180..12) stop BEFORE they start. There is no rule in there to find."""
    widths = {p.hsync_stop - p.hsync_start for p in our.SHIPPED_PRESETS}
    assert len(widths) > 3, (
        "the shipped sync widths have become consistent, which would mean there "
        f"IS a rule after all: {sorted(widths)}"
    )

    inverted = [p.name for p in our.SHIPPED_PRESETS if p.hsync_stop < p.hsync_start]
    assert inverted, "the stop-before-start presets have gone; recheck the premise"


# --- what the bench measured, 2026-08-11 ------------------------------------

# Each step applied as a set on the RiscPC at 320x256@50, judged on the TV.
# (clock MHz, htotal, vtotal, capture, HSCALE, picture px, verdict)
SWEPT = [
    (81.35,  1445, 1126, 1009, 823, 1253, "works (shipped)"),
    (107.98, 1918, 1126, 1009, 606, 1702, "works, sharp"),
    (129.55, 2301, 1126, 1124, 553, 2079, "works, sharp"),
    (161.98, 2877, 1126, 1186, 500, 2426, "flickers then black"),
]


def test_the_working_ceiling_is_the_measured_one_not_the_datasheets():
    """108 MHz is not the constraint, and the bench says so.

    DS-5725-3.2 Table 15 rates the CLKOUT *pin*, and PAD_CKOUT_ENZ disables it on
    this board because the MS9288A takes the analog output.

    129.6 MHz is the highest DEMONSTRATED value, so it is a floor on the true
    limit rather than the limit. Nothing between 129.6 and 162 has been tried and
    the mapped dividers offer nothing in between.
    """
    worked = [mhz for mhz, *_, verdict in SWEPT if "black" not in verdict]
    failed = [mhz for mhz, *_, verdict in SWEPT if "black" in verdict]

    assert our.WORKING_CEILING_HZ / 1e6 == pytest.approx(max(worked), abs=0.5), (
        "the ceiling should be the highest clock actually demonstrated"
    )
    assert min(failed) > max(worked), "the sweep does not bracket a wall"
    assert our.WORKING_CEILING_HZ in our.DIVIDER_HZ.values(), (
        "the ceiling has to be a clock a divider can actually seed"
    )


def test_the_scale_floor_caps_the_raster_before_the_clock_does():
    """produced = capture x 1024 / HSCALE, and HSCALE stops at 500.

    So a raster wider than capture x 2.048 cannot be filled and just adds black.
    The sweep shows it as the fill fraction turning over: 88.7%, 90.4%, then DOWN
    to 84.3% at 162 MHz -- more clock became actively worse BEFORE the clock
    itself failed.
    """
    fill = [(mhz, picture / htotal) for mhz, htotal, _, _, _, picture, _ in SWEPT]

    best = max(fill, key=lambda row: row[1])
    assert best[0] == pytest.approx(129.55, abs=0.5), (
        f"129.6 MHz should fill the most of its raster, got {best}"
    )
    assert fill[-1][1] < fill[-2][1], "the fill fraction must turn over at 162 MHz"

    # The widest picture this source can ever make, from the deepest capture the
    # sweep reached with the sync pulse excluded.
    assert int(1186 * our.MAX_MAGNIFICATION) == 2428
    assert 2877 - 2428 == 449, "the 162 MHz raster's unfillable remainder"


def test_hscale_only_reaches_its_floor_at_the_failing_step():
    """Only the 162 MHz raster demands this model's scale floor, which is what
    makes that step confounded: it hits the artifact floor AND the clock wall at
    once, so a soft picture there would not distinguish them. The screen goes
    black, so the clock is what fails -- but the step is not evidence about the
    floor.
    """
    at_floor = [mhz for mhz, _, _, _, hscale, _, _ in SWEPT if hscale <= our.HSCALE_FLOOR]
    assert at_floor == [161.98]
