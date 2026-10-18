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
    // The FRAME, not what STATUS_SYNC_PROC_VTOTAL counts -- that register is
    // zero based and SyncProcessor::frameLines() corrects it at the boundary,
    // so nothing downstream carries the correction.
    //
    // Two instruments settle the zero base: HPERIOD_IF reads the bench source
    // at 15625 Hz, and 15625/312 is the 50.08 the field rate measures where
    // 15625/311 is 50.24, which it does not.
    CHECK(VideoSignal::lineRateFor(312, 50.08f) == 15624u);
    CHECK(VideoSignal::lineRateFor(525, 60.0f) == 31500u);
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

TEST_CASE("a count that is a whole multiple of another is a lost vertical pulse")
{
    // A separator missing a vertical pulse leaves the counter running into the
    // next field, so the reading lands on twice the frame or three times it.
    // Measured on `vga` at 800x600@60 composite in pass-through: a solve of 627
    // read 1246 and 1870 among its idle samples.
    CHECK(VideoSignal::countIsMultipleOf(1246, 627));
    CHECK(VideoSignal::countIsMultipleOf(1870, 627));

    SUBCASE("the multiple is of the count the pulse was lost from") {
        // The exact multiples of a solved 627 are 1254 and 1881, and the
        // readings sit 8 and 11 short of them -- the count the separator lost
        // the pulse from is the raw one rather than the restored one. So the
        // tolerance is wider than a count's own spread and still nowhere near
        // the 627 that separates one multiple from the next.
        CHECK_FALSE(VideoSignal::countIsMultipleOf(1333, 627));
        CHECK_FALSE(VideoSignal::countIsMultipleOf(1691, 627));
    }

    SUBCASE("one is not a multiple") {
        // Whether the source moved at an unchanged frame is the steadiness
        // run's question, and answering it here would refuse every count.
        CHECK_FALSE(VideoSignal::countIsMultipleOf(627, 627));
        CHECK_FALSE(VideoSignal::countIsMultipleOf(630, 627));
    }

    SUBCASE("a real mode change to a count near twice is still a multiple") {
        // 311 lines at 50 Hz to 628 at 60: 2.02 times the count, which this
        // cannot tell from a lost pulse. What separates them is the line
        // period, which doubles here and does not move on a lost pulse.
        CHECK(VideoSignal::countIsMultipleOf(628, 311));
    }

    SUBCASE("a count that is not near a multiple is a move") {
        CHECK_FALSE(VideoSignal::countIsMultipleOf(524, 311));
        CHECK_FALSE(VideoSignal::countIsMultipleOf(451, 627));
    }

    SUBCASE("nothing to compare against says nothing") {
        CHECK_FALSE(VideoSignal::countIsMultipleOf(1246, 0));
        CHECK_FALSE(VideoSignal::countIsMultipleOf(0, 627));
    }
}
