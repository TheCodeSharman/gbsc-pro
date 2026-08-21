// Host-compiled unit tests for src/tv5725/Memory.h -- `make -C test memory`.
//
// The playback stage's burst structure: how much it pulls from SDRAM per
// request, which sets the read request rate. One side of the beat that tears
// the picture when PB_FETCH_NUM is left at an upstream table's 256.
// docs/investigations/hscale-tearing-characterisation.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Memory.h"

using namespace Tv5725;

// The bench output raster: VDS_HSYNC_RST 1444, so 1445 px per line.
static const uint16_t Line1080p = 1445;
static const uint16_t Offset1080pExpected = 250;

TEST_CASE("the fetch is the source pixels the line needs, over the request budget")
{
    // PB_FETCH_NUM is pixels per playback burst (docs/tv5725-chip.md). Playback
    // reads the whole capture width out of SDRAM every output line and gets
    // RequestsPerLine bursts to do it in; short of that the line does not finish
    // and repeats, the start of the picture reappearing at the right.
    //
    // Derived from every recorded verdict rather than fitted: capture / fetch of
    // 3.61, 3.94 and 4.04 is clean, 4.28 wraps and 4.66 shreds, so the boundary
    // sits between 4.04 and 4.28. It also reproduces the two values tuned by
    // hand -- capture 804 -> 201 against 200, capture 1009 -> 253 against 250.
    //
    // And it is why there is no band table: the beat quantity is
    // n = window x HSCALE / (1024 x fetch), and substituting fetch = capture / 4
    // and capture = produced x HSCALE / 1024 gives n = 4 x window / produced.
    // HSCALE cancels out, so the ratio does not move with the zoom.
    CHECK(Memory::fetchFor(Line1080p, 804) == 201);
    CHECK(Memory::fetchFor(Line1080p, 1009) == 253);

    SUBCASE("it rounds UP, because the floor is a floor") {
        // A line one pixel short still repeats. Truncating would put the value
        // under the requirement for three captures in every four.
        CHECK(Memory::fetchFor(Line1080p, 1000) == 250);
        CHECK(Memory::fetchFor(Line1080p, 1001) == 251);
        CHECK(Memory::fetchFor(Line1080p, 1004) == 251);
    }

    SUBCASE("every recorded verdict is reproduced") {
        // The two failures must come out ABOVE what they were set to, and the
        // three clean readings at or below. This is the whole evidence base.
        CHECK(Memory::fetchFor(Line1080p, 1009) > 236);   // wrapped at 236
        CHECK(Memory::fetchFor(Line1080p, 931) > 200);    // shredded at 200
        CHECK(Memory::fetchFor(Line1080p, 737) <= 204);   // clean at 204
        CHECK(Memory::fetchFor(Line1080p, 1009) <= 256);  // clean at 256
    }

    SUBCASE("deep zoom is held at the measured floor, never extrapolated down") {
        // capture / 4 keeps falling as the zoom goes in -- 70 at the deepest
        // framing -- and nobody has ever run the part that low. The measured
        // scheme goes flat at 150 below HSCALE 581, so that is where this stops
        // rather than at an arithmetic limit.
        CHECK(Memory::fetchFor(Line1080p, 279) == Memory::FetchFloor);
        CHECK(Memory::fetchFor(Line1080p, 336) == Memory::FetchFloor);
        CHECK(Memory::fetchFor(Line1080p, 0) == Memory::FetchFloor);
        CHECK(Memory::FetchFloor == 150);
    }

    SUBCASE("it never leaves the register's range") {
        for (uint32_t capture = 0; capture <= 4095; capture += 3) {
            uint16_t fetch = Memory::fetchFor(Line1080p, (uint16_t)capture);
            CHECK(fetch >= Memory::FetchMin);
            CHECK(fetch <= Memory::FetchMax);
        }
    }

    SUBCASE("it is continuous -- one pixel of capture never moves it far") {
        // Stepping is not good enough -- values between the steps were measured
        // not working, so it has to ramp. A pad press changes the capture by one
        // unit, so the fetch must change by at most one.
        uint16_t previous = Memory::fetchFor(Line1080p, 150);
        for (uint16_t capture = 151; capture <= 1200; ++capture) {
            uint16_t fetch = Memory::fetchFor(Line1080p, capture);
            CHECK(fetch >= previous);
            CHECK(fetch - previous <= 1);
            previous = fetch;
        }
    }

    SUBCASE("the floor applies at EVERY output raster, swept or not") {
        // Gating the rule on the swept 1445 px line was the bug: a preset load
        // onto a 1435 px raster switched it off and PB_FETCH_NUM sat at 256,
        // short of the 297 a capture of 1185 needs, and the glitches came back.
        // DefaultFetch is not safe, it is tuned for a raster it is not, and it
        // can be below the floor. Whether 4 requests per line is right may depend
        // on the raster; that the burst must cover the line's source pixels does
        // not.
        for (uint16_t line : {(uint16_t)1435, (uint16_t)1650, (uint16_t)858}) {
            CHECK(Memory::fetchFor(line, 1185) == 297);
            CHECK(Memory::fetchFor(line, 1009) == 253);
            CHECK(Memory::fetchFor(line, 279) == Memory::FetchFloor);
        }
    }

    SUBCASE("a dropped capture read cannot produce a short burst") {
        // Capture 0 is a failed read, not a framing. The floor is the honest
        // answer -- returning 0 would stop playback entirely.
        CHECK(Memory::fetchFor(1445, 0) == Memory::FetchFloor);
        CHECK(Memory::fetchFor(1435, 0) == Memory::FetchFloor);
    }
}

TEST_CASE("the stride covers the widest fetch the line can ask for")
{
    // The stride is the per-line allocation and the fetch is what playback
    // reads into it, so a stride below the fetch overlaps successive lines and
    // each overwrites its predecessor's tail. The fetch follows the capture and
    // the capture grows as the picture zooms OUT, so the stride is sized for a
    // capture of the WHOLE line: 230 against a zoomed-out capture of 1044 is a
    // fetch of 261 into a stride of 230, measured on the bench as a green band
    // down the right of the picture.
    const uint16_t line = 1126;   // the bench line at PLLAD_MD 2250

    SUBCASE("no framing of that line can ask for more than the stride") {
        for (uint16_t capture = 128; capture <= line; capture += 4)
            CHECK(Memory::offsetFor(line) >= Memory::fetchFor(1915, capture));
    }

    SUBCASE("it is a property of the LINE, so a zoom cannot move it") {
        // The alternative is the widest capture actually reachable, which
        // subtracts the hsync pulse -- and that comes from a live measurement
        // that moves by a unit between solves, so the stride would be rewritten
        // on an arbitrary pad press, re-laying the buffer out under the picture.
        CHECK(Memory::offsetFor(line) > Memory::fetchFor(1915, line - 80));
    }

    SUBCASE("it stays inside the register, at any line the divider allows") {
        CHECK(Memory::offsetFor(line) <= Memory::OffsetMax);
        CHECK(Memory::offsetFor(Memory::FetchMax * 8) <= Memory::OffsetMax);
    }

    SUBCASE("a dropped capture read gives the floor, not a stride of nothing") {
        CHECK(Memory::offsetFor(0) >= Memory::FetchFloor);
    }
}
