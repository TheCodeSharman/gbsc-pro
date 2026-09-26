// Host tests for SourceAbsence -- `make -C test source-absence`.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/videosource/SourceAbsence.h"

TEST_CASE("one missed pass is not a source going away")
{
    // A mux that has just moved looks exactly like an empty socket, and the
    // route is queued to loop() with the switch on the HC32's UART: measured
    // over ten input changes, 0.16 to 1.12 s passes before detection even
    // looks. Tearing the chip down on one pass costs the acquisition twice
    // over, because setResetParameters() zeroes segments 0 and 2 and the rate
    // measured through the result is rejected for seconds afterwards.
    SourceAbsence absence;

    absence.missed();

    CHECK_FALSE(absence.shouldPowerDown());
}

TEST_CASE("absence has to persist before the chip is powered down")
{
    SourceAbsence absence;

    for (uint8_t i = 1; i < SourceAbsence::PassesBeforeLowPower; ++i) {
        absence.missed();
        REQUIRE_FALSE(absence.shouldPowerDown());
    }
    absence.missed();

    CHECK(absence.shouldPowerDown());
}

TEST_CASE("a pass that found a source ends the run")
{
    SourceAbsence absence;
    for (uint8_t i = 0; i < SourceAbsence::PassesBeforeLowPower; ++i)
        absence.missed();
    REQUIRE(absence.shouldPowerDown());

    absence.found();

    CHECK_FALSE(absence.shouldPowerDown());
}

TEST_CASE("a long absence stays an absence")
{
    // The run is a saturating count, not a wrapping one. Powering down later
    // costs nothing but power -- an empty socket stays an empty socket -- but a
    // counter that wraps withdraws the verdict it has already reached and the
    // chip comes back up for as many passes as it takes to climb again.
    SourceAbsence absence;
    for (uint8_t i = 0; i < SourceAbsence::PassesBeforeLowPower; ++i)
        absence.missed();
    REQUIRE(absence.shouldPowerDown());

    bool withdrawn = false;
    for (uint16_t i = 0; i < 600 && !withdrawn; ++i) {
        absence.missed();
        withdrawn = !absence.shouldPowerDown();
    }

    CHECK_FALSE(withdrawn);
}

TEST_CASE("a pass that found nothing while a signal reaches the chip advances the run")
{
    // A source whose sync is arriving but which detection has not claimed is a
    // source the chip is configured wrongly for, and the teardown is what
    // repairs that. Holding the run here is what let `state: absent` stand for
    // 150 s with the source present throughout.
    SourceAbsence absence;
    absence.missed();
    absence.missed();

    absence.undecided();
    absence.undecided();

    CHECK(absence.passes() == 4);
}

TEST_CASE("a deliberate selection spends the patience the run exists for")
{
    // **THE TEARDOWN IS WHAT MAKES THE SOURCE APPEAR AFTER AN INPUT CHANGE.**
    // Measured on the bench selecting the Wii on `ypbpr`: four passes report
    // `det hsact,0` and `det sync present,0` -- nothing on either instrument --
    // then the run reaches its threshold at pass five,
    // goLowPowerWithInputDetection() runs, and sync is present 0.6 s later with
    // detection succeeding in 25 ms. The five seconds are spent waiting for the
    // counter, not waiting for the source.
    //
    // The run exists so that a DROPPED MEASUREMENT does not cost a teardown. A
    // deliberate selection is not a dropped measurement: it is a known event
    // after which the chip demonstrably needs the reset. So the patience is
    // spent up front and the first pass that finds nothing acts.
    SourceAbsence absence;

    absence.selectionChanged();

    REQUIRE_FALSE(absence.shouldPowerDown());
    absence.missed();
    CHECK(absence.shouldPowerDown());
}

TEST_CASE("a selection whose source is already there costs no teardown")
{
    // Switching to an input that already has sync -- the RISC PC on vga --
    // succeeds on the first pass, so the spent patience is never drawn on.
    SourceAbsence absence;
    absence.selectionChanged();

    absence.found();

    CHECK_FALSE(absence.shouldPowerDown());
}

TEST_CASE("the patience returns once the selection has been answered")
{
    // Spending it is for the one pass after the change, not for the life of the
    // input: a dropped measurement seconds later is a dropped measurement again.
    SourceAbsence absence;
    absence.selectionChanged();
    absence.found();

    absence.missed();

    CHECK_FALSE(absence.shouldPowerDown());
}

TEST_CASE("a signal detection cannot claim still reaches the teardown")
{
    // Detection finding nothing while a signal IS reaching the sync processor is
    // the state a teardown repairs: the chip is misconfigured rather than the
    // socket empty, and measured on the bench the run held `state: absent` for
    // 150 s with the source sitting there until `/sc?~` forced one. A run that
    // neither advances nor ends is what left it there.
    SourceAbsence absence;

    for (uint8_t i = 0; i < SourceAbsence::PassesBeforeLowPower; ++i)
        absence.undecided();

    CHECK(absence.shouldPowerDown());
}

TEST_CASE("an unclaimed signal is still one dropped pass, not a source leaving")
{
    SourceAbsence absence;

    absence.undecided();

    CHECK_FALSE(absence.shouldPowerDown());
}

TEST_CASE("a teardown re-arms the run rather than ending it")
{
    // **THE RUN MUST NEVER STALL.** A teardown that did not bring the source
    // back is a teardown to make again, so performing one returns the run to
    // counting instead of leaving it latched at its threshold for ever.
    SourceAbsence absence;
    for (uint8_t i = 0; i < SourceAbsence::PassesBeforeLowPower; ++i)
        absence.undecided();
    REQUIRE(absence.shouldPowerDown());

    absence.poweredDown();

    CHECK_FALSE(absence.shouldPowerDown());
}

TEST_CASE("a source still missing after a teardown asks for another one")
{
    SourceAbsence absence;
    for (uint8_t i = 0; i < SourceAbsence::PassesBeforeLowPower; ++i)
        absence.undecided();
    absence.poweredDown();

    for (uint8_t i = 0; i < SourceAbsence::PassesBeforeLowPower; ++i)
        absence.undecided();

    CHECK(absence.shouldPowerDown());
}
