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

TEST_CASE("a pass that found nothing while a signal reaches the chip holds the run")
{
    // Neither evidence. STATUS_SYNC_PROC_HSACT is not a signal-presence test and
    // SyncProcessor::signalPresent() counts activity on the test bus instead, so
    // a source whose sync is arriving but which detection has not claimed is a
    // source to keep looking for rather than one to power down for.
    SourceAbsence absence;
    absence.missed();
    absence.missed();

    absence.undecided();
    absence.undecided();

    CHECK(absence.passes() == 2);
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
