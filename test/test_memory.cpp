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

TEST_CASE("the measured pair is used whole, not reconstructed")
{
    // Both halves come from the same measurement rather than one being derived
    // from the other. The agreement with the firmware's fetch + 4 invariant is a
    // coincidence, not a rule: the offset was clean across 190..256 against a
    // fetch of 204.
    CHECK(Memory::offsetFor(Line1080p) == 250);

    SUBCASE("the offset does NOT follow the fetch, and that is deliberate") {
        // Measured 2026-08-09: the offset is not critical -- clean anywhere
        // across 190..256 against a fetch of 204. So it stays the verified
        // constant rather than becoming a second register that moves on every
        // pad press for no reason anyone has measured.
        for (uint16_t capture = 256; capture <= 1009; capture += 97)
            CHECK(Memory::offsetFor(Line1080p) == Offset1080pExpected);
    }

    SUBCASE("and nothing can overflow its ten bits") {
        for (uint32_t line = 0; line < 4096; line += 7)
            CHECK(Memory::offsetFor((uint16_t)line) <= Memory::OffsetMax);
    }
}
