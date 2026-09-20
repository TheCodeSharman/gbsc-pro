// Host-compiled unit tests for src/tv5725/VideoSignal.h -- `make -C test video-signal`.
//
// What counts as a video signal, and the line rate a count and a field rate
// imply. Pure arithmetic over two measured numbers: no registers, no chip.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoSignal.h"

using namespace Tv5725;

TEST_CASE("the line rate is the field rate over the whole frame")
{
    // STATUS_SYNC_PROC_VTOTAL is zero based: the frame is one line longer than
    // it counts. Reading it as the frame is 1/312 low on the bench source,
    // which is the whole of the accuracy HPERIOD_IF has over this route.
    // Two instruments settle the zero base: HPERIOD_IF reads the bench source
    // at 15625 Hz, and 15625/312 is the 50.08 the field rate measures where
    // 15625/311 is 50.24, which it does not.
    CHECK(VideoSignal::lineRateFor(311, 50.08f) == 15624u);
    CHECK(VideoSignal::lineRateFor(524, 60.0f) == 31500u);
}

TEST_CASE("a count and a rate video runs at is a video signal")
{
    CHECK(VideoSignal::isVideo(311, 50.08f));

    SUBCASE("the line count does not decide what the rate may be") {
        // 311 lines runs at 50 Hz here and 60 Hz elsewhere, and 262 the other
        // way about. Both are real sources, so both are measurements rather
        // than errors.
        CHECK(VideoSignal::isVideo(311, 60.0f));
        CHECK(VideoSignal::isVideo(312, 50.0f));
        CHECK(VideoSignal::isVideo(262, 50.0f));
        CHECK(VideoSignal::isVideo(262, 59.94f));
    }

    SUBCASE("the 97/98 a preset load leaves behind is refused outright") {
        // Documented as normal for a moment after a load, and the reason
        // solveRaster() defers rather than solving. Below
        // SourceVerticalTotalMin, so it never reaches the rate check at all.
        CHECK_FALSE(VideoSignal::isVideo(97, 50.0f));
        CHECK_FALSE(VideoSignal::isVideo(98, 50.0f));
    }

    SUBCASE("a field rate that is neither 50 nor 60 is still a field rate") {
        // 640x480@75 on the VGA input: 499 lines, measured 75.088 Hz. Rejecting
        // it leaves the previous mode's raster and clock in place, so the output
        // runs at a rate no display locks to and the screen goes blank.
        CHECK(VideoSignal::isVideo(499, 75.088f));
    }

    SUBCASE("a rate no video source runs at is still refused") {
        CHECK_FALSE(VideoSignal::isVideo(311, 4.0f));
        CHECK_FALSE(VideoSignal::isVideo(311, 400.0f));
    }

    SUBCASE("the rate counted off a composite-sync source's V pin is refused") {
        // A composite-sync source drives no V pin, so what is counted there is
        // not its field rate: measured at 640x480@60 on composite sync, 15.31
        // to 15.33 Hz and 20.80 to 21.92 Hz. Admitted, the pair implies a line
        // rate of 8043 Hz, which sizes the ADC PLL's crossover row for 16.5 MHz
        // against a source wanting 65.7 -- PLLAD_KS 3 where 1 is due, and
        // STATUS_SYNC_PROC_HTOTAL 809 against a divider of 2039.
        CHECK_FALSE(VideoSignal::isVideo(524, 15.32f));
        CHECK_FALSE(VideoSignal::isVideo(524, 21.92f));
    }
}

TEST_CASE("the count and the rate can each be asked on their own")
{
    // The count is what the scan mode and the sampling clock are chosen from,
    // and both are needed BEFORE a field rate has been measured at all.
    CHECK(VideoSignal::countIsSource(311));
    CHECK_FALSE(VideoSignal::countIsSource(97));
    CHECK_FALSE(VideoSignal::countIsSource(0));

    CHECK(VideoSignal::fieldRateIsSource(50.08f));
    CHECK_FALSE(VideoSignal::fieldRateIsSource(0.0f));
}

TEST_CASE("the tolerance is the caller's, and the same pair answers both ways")
{
    // No default: the sites asking this are not asking the same question, and
    // one number answering all of them is what forgave a deliberate 3.9%
    // divider change. 15625 against 15624 is 0.06%.
    CHECK(VideoSignal::ratesAgree(15625u, 15624u, 1));
    CHECK_FALSE(VideoSignal::ratesAgree(15625u, 18100u, 50));

    // 1876 and 1952 are 480p's divider and 576p's on the bench source: inside
    // the 5% a measurement is forgiven, and not one part of it noise.
    CHECK(VideoSignal::ratesAgree(1876u, 1952u, 50));
    CHECK_FALSE(VideoSignal::ratesAgree(1876u, 1952u, 2));
}
