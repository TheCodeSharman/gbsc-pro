// Host-compiled unit tests for src/tv5725/DisplayClock.h -- `make -C test
// display-clock`.
//
// The raster decides what pixel clock the part is being asked for:
//
//     required clock = rasterHorizontalTotal x frameLines x sourceFieldRate
//
// so a clock ceiling is a constraint on which rasters are legal, which is what
// this arithmetic is for.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/DisplayClock.h"

using namespace Tv5725;

TEST_CASE("the divider byte names a pixel clock")
{
    // Every one of these is 648 MHz over an integer, which is where the
    // register's name comes from: 648/16, /12, /10, /8, /6, /5, /4. The
    // mapping is a catalogue rather than a formula because the high nibble is
    // not the divider (2, 4, 5, 6, 8, 9, A against 16, 12, 10, 8, 6, 5, 4).
    CHECK(DisplayClock::hzFor(0x25) == 40500000u);
    CHECK(DisplayClock::hzFor(0x45) == 54000000u);
    CHECK(DisplayClock::hzFor(0x55) == 64800000u);
    CHECK(DisplayClock::hzFor(0x65) == 81000000u);
    CHECK(DisplayClock::hzFor(0x85) == 108000000u);
    CHECK(DisplayClock::hzFor(0x95) == 129600000u);
    CHECK(DisplayClock::hzFor(0xA5) == 162000000u);

    SUBCASE("including the two the firmware writes itself") {
        // 0x35 is written by setOutModeHdBypass() and 0x00 by the reset path.
        // Both mapped deliberately upstream, not fallbacks.
        CHECK(DisplayClock::hzFor(0x35) == 81000000u);
        CHECK(DisplayClock::hzFor(0x00) == 81000000u);
    }
}

TEST_CASE("the external-clock sentinel names no clock at all")
{
    // PLL648_CONTROL_01 is parked at 0x75 while the Si5351 drives the display,
    // with the real divider stashed in rto->presetDisplayClock. Answering 81 MHz
    // for the sentinel is a silent guess that loses the divider, so 0 means
    // "this byte does not name a clock; ask the stash".
    CHECK(DisplayClock::hzFor(DisplayClock::ExternalSentinel) == 0u);

    SUBCASE("and neither does any other unmapped byte") {
        CHECK(DisplayClock::hzFor(0x15) == 0u);
        CHECK(DisplayClock::hzFor(0xFF) == 0u);
    }
}

TEST_CASE("the raster htotal is the frame clock budget over the frame height")
{
    // Clocks available per line are hz / (fieldRate x frameLines) -- pixels per
    // line is what is left once the clock and the frame height are fixed.
    // FLOORED, which is FrameSync's own rule: findBestHTotal() aims for an output
    // frame time as close to the input as possible while still being less, so
    // rounding up would overshoot the budget and leave the loop pulling back.
    CHECK(DisplayClock::horizontalTotalFor(108000000u, 1126, 50.0f) == 1918);
    CHECK(DisplayClock::horizontalTotalFor(81000000u, 1126, 50.0f) == 1438);

    SUBCASE("a zero anywhere is not a raster") {
        CHECK(DisplayClock::horizontalTotalFor(0u, 1126, 50.0f) == 0);
        CHECK(DisplayClock::horizontalTotalFor(108000000u, 0, 50.0f) == 0);
        CHECK(DisplayClock::horizontalTotalFor(108000000u, 1126, 0.0f) == 0);
    }
}

TEST_CASE("the PAL 1080p preset spends 81 of a 108 MHz budget")
{
    // Both rasters are read off this board rather than derived. The two 1080p
    // tables ship theirs in s3_01..s3_03 with a matching seed in s0_41, each
    // internally consistent with its own field rate:
    //
    //   ntsc_1920x1080  0x41/0x56/0x46 -> 1602 x 1126 @ 60 Hz = 108.23 MHz  seed 0x85
    //   pal_1920x1080   0xA4/0x55/0x46 -> 1445 x 1126 @ 50 Hz =  81.35 MHz  seed 0x65
    //
    // The live unit's registers are byte-identical to the NTSC row, so nothing
    // trimmed them: the 1445 raster is not drift, a lost sentinel or a
    // preset-load clobber, it is what the table asks for.
    const uint16_t Lines1080p = 1126;

    // What each seed's clock affords at its own field rate. The NTSC table takes
    // essentially all of it; the PAL table takes all of a budget three quarters
    // the size.
    CHECK(DisplayClock::horizontalTotalFor(DisplayClock::hzFor(0x85), Lines1080p, 60.0f)
          == 1598);
    CHECK(DisplayClock::horizontalTotalFor(DisplayClock::hzFor(0x65), Lines1080p, 50.0f)
          == 1438);

    SUBCASE("and 108 MHz at 50 Hz affords a third more line") {
        // The number the whole change rests on. 1918 against 1445 is 33% more
        // horizontal resolution, at a clock this board is running right now on
        // the NTSC preset -- so the encoder locking to it is observed, not hoped.
        CHECK(DisplayClock::horizontalTotalFor(DisplayClock::hzFor(0x85), Lines1080p,
                                      50.0f) == 1918);
    }

    SUBCASE("both shipped rasters are inside the ceiling, which is why they work") {
        // 1602 x 1126 x 60 is 108.23 MHz -- 0.2% over the rated maximum, and it
        // runs. Worth recording as the measured margin rather than pretending
        // the ceiling is a cliff.
        CHECK((uint32_t)1445 * Lines1080p * 50 < DisplayClock::CeilingHz);
        CHECK((uint32_t)1602 * Lines1080p * 60 > DisplayClock::CeilingHz);
    }
}

TEST_CASE("108 MHz is the highest display clock with evidence behind it")
{
    // DS-5725-3.2 Table 15 rates the CLKOUT pin at 108 MHz / 20pF, and Tvia's
    // current-measurement condition reads "162MHz 32bit memory, 108MHz Display
    // clock". A PAD rating, not a proven limit on the internal VCLK: CLKOUT is
    // disabled on this board because the MS9288A takes the analog output, and
    // what bounds VCLK is stated nowhere. Table 14's 80 MHz CLKIN is the digital
    // video INPUT port and the 162 MHz FBCLK is the memory interface, so neither
    // is a candidate either.
    CHECK(DisplayClock::CeilingHz == 108000000u);

    SUBCASE("two mapped dividers are above it, and that is a question not a bug") {
        // 0x95 and 0xA5 are register-reachable and shipped by ntsc_1280x1024 and
        // ntsc_240p. The 2026-08-11 sweep settled them: 129.6 MHz is sharp and
        // 162 MHz flickers then goes black, which is why
        // OutputRaster::WorkingCeilingHz is the higher of the two and not this.
        CHECK(DisplayClock::hzFor(0x95) > DisplayClock::CeilingHz);
        CHECK(DisplayClock::hzFor(0xA5) > DisplayClock::CeilingHz);
    }

    SUBCASE("108 MHz at 1080p50 sits inside it by construction") {
        // 1918 x 1126 x 50 = 107,983,400 -- under the ceiling, and the reason
        // 1920 is the answer rather than a round number someone liked.
        CHECK((uint32_t)1918 * 1126 * 50 <= DisplayClock::CeilingHz);
        CHECK((uint32_t)1919 * 1126 * 50 > DisplayClock::CeilingHz);
    }
}
