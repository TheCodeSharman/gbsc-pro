// Host-compiled unit tests for src/tv5725/MemoryWindow.h -- `make -C test memory-window`.
//
// The region of SDRAM a capture occupies: the layout, whether the frame about
// to be captured fits in the buffer the chip was told to put it in, and the
// playback burst structure that reads it back. PB_FETCH_NUM sets the memory
// read request rate, which is one side of the beat that tore the picture across
// 80 of 493 framings. Pure arithmetic.
// docs/investigations/horizontal-scale-corruption.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/MemoryWindow.h"

using namespace Tv5725;

// --- the layout -------------------------------------------------------------

TEST_CASE("the address space is the 21 bits the part actually has")
{
    // 2^21 words of 32 bits = 8 MB, which is the single EM638325TS the schematic
    // fits -- "2M x 32 SDRAM", four banks, one chip select. The address space
    // matches the part exactly, with nothing spare and nothing short.
    CHECK(MemoryWindow::SpaceWords == 2097152u);
}

TEST_CASE("a pixel costs one word, because the chip is told to add one per pixel")
{
    // CAP_ADR_ADD_2 (s4_21[7]) reads 0 on the bench and in all twelve tables, so
    // 24-bit RGB on a 32-bit bus takes a whole word with 8 bits unused. Read off
    // the register: a frame-arithmetic argument gives 16 bpp and is wrong by a
    // factor of two.
    CHECK(MemoryWindow::WordsPerPixel == 1);
    CHECK(MemoryWindow::wordsFor(1009, 527) == 531743u);
}

TEST_CASE("the capture region is everything above the field store")
{
    // The field store keeps its measured guard at 0x052000; the capture buffer
    // starts directly above it and runs to the top of the space, so nothing is
    // stranded between them. Capture is the region that can actually run out.
    CHECK(MemoryWindow::CaptureStart == 0x060000u);
    CHECK(MemoryWindow::CaptureGuard == MemoryWindow::SpaceWords - 1);
    CHECK(MemoryWindow::captureWords() == 1703936u);
}

TEST_CASE("the capture buffer starts clear of the field store's guard")
{
    // The invariant that makes the map safe at all: capture must begin above
    // where the write FIFO is allowed to reach, or the two regions overlap and
    // the corruption is a frame appearing inside the deinterlacer's field.
    CHECK(MemoryWindow::CaptureStart > MemoryWindow::FieldStoreGuard);
    CHECK(MemoryWindow::marginWords() == 57344u);  // 0x060000 - 0x052000
}

TEST_CASE("the framing the bench actually runs fits, with room")
{
    // 1009 x 527, the measured capture on the RiscPC at 320x256@50.
    CHECK(MemoryWindow::captureFits(1009, 527));
}

TEST_CASE("a capture that would run past the top of memory is refused")
{
    // At 527 lines the region holds 3233 samples per line -- 3233 x 527 =
    // 1,703,791 against 1,703,936 available -- and one more does not fit. It has
    // to be caught before it reaches the chip, whose answer to an overrun is to
    // wrap: a wrong address on screen and no report.
    CHECK(MemoryWindow::captureFits(3233, 527));
    CHECK_FALSE(MemoryWindow::captureFits(3234, 527));
}

TEST_CASE("the widest capture allowed is reported, so a caller can clamp")
{
    // Refusing is not enough on its own -- the engine needs to know what it may
    // have instead, or the only options are "works" and "black screen".
    CHECK(MemoryWindow::maxCaptureWidth(527) == 3233u);
    CHECK(MemoryWindow::captureFits(MemoryWindow::maxCaptureWidth(527), 527));
    CHECK_FALSE(MemoryWindow::captureFits(MemoryWindow::maxCaptureWidth(527) + 1, 527));
}

TEST_CASE("the old map would have refused captures the new one allows")
{
    // Why the base moved. 0x100000 left 1,048,576 words, so at 527 lines
    // anything past 1989 samples overran -- and PLLAD_MD goes to 4095, so that
    // was reachable rather than theoretical.
    const uint32_t oldRegion = MemoryWindow::SpaceWords - 0x100000u;
    CHECK(oldRegion == 1048576u);
    CHECK(MemoryWindow::wordsFor(1990, 527) > oldRegion);
    CHECK(MemoryWindow::captureFits(1990, 527));
}

TEST_CASE("a capture too wide for memory is narrowed, not refused")
{
    // Clamp rather than fail, as readCapture() does against the source line: a
    // black screen is a worse answer than a slightly narrower picture. HORIZONTAL
    // is what gives, because capture width is in ADC samples and PLLAD_MD is 12
    // bits, while the line count is the source's.
    CHECK(MemoryWindow::clampWidth(5000, 527) == 3233u);
    CHECK(MemoryWindow::captureFits(MemoryWindow::clampWidth(5000, 527), 527));
}

TEST_CASE("a capture that already fits is returned untouched")
{
    // The clamp must be invisible in the normal case, or it becomes a second
    // owner of the capture width and the framing stops being the truth.
    CHECK(MemoryWindow::clampWidth(1009, 527) == 1009u);
}

TEST_CASE("zero-sized captures are not a special case")
{
    // Degenerate rather than dangerous: a solver mid-iteration can ask, and the
    // answer is yes rather than a division or an underflow.
    CHECK(MemoryWindow::wordsFor(0, 0) == 0u);
    CHECK(MemoryWindow::captureFits(0, 0));
}

// --- how playback reads it back ---------------------------------------------

// The bench output raster: VDS_HSYNC_RST 1444, so 1445 px per line.
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
    CHECK(MemoryWindow::fetchFor(804) == 201);
    CHECK(MemoryWindow::fetchFor(1009) == 253);

    SUBCASE("it rounds UP, because the floor is a floor") {
        // A line one pixel short still repeats. Truncating would put the value
        // under the requirement for three captures in every four.
        CHECK(MemoryWindow::fetchFor(1000) == 250);
        CHECK(MemoryWindow::fetchFor(1001) == 251);
        CHECK(MemoryWindow::fetchFor(1004) == 251);
    }

    SUBCASE("every recorded verdict is reproduced") {
        // The two failures must come out ABOVE what they were set to, and the
        // three clean readings at or below. This is the whole evidence base.
        CHECK(MemoryWindow::fetchFor(1009) > 236);   // wrapped at 236
        CHECK(MemoryWindow::fetchFor(931) > 200);    // shredded at 200
        CHECK(MemoryWindow::fetchFor(737) <= 204);   // clean at 204
        CHECK(MemoryWindow::fetchFor(1009) <= 256);  // clean at 256
    }

    SUBCASE("deep zoom holds the RATIO, which is the quantity measured") {
        // Swept by hand at capture 456, automation frozen: 114 and 128 clean,
        // 140 breaking up, and 150 putting blocks of other content through flat
        // colour. A constant floor holds the VALUE still and lets the ratio
        // fall, and the ratio is what every verdict was taken on.
        CHECK(MemoryWindow::fetchFor(456) == 114);
        CHECK(MemoryWindow::fetchFor(279) == 70);
        CHECK(MemoryWindow::fetchFor(336) == 84);
    }

    SUBCASE("the ratio stays inside the band at every framing") {
        // Clean to 4.04 and tearing from 4.28 above; 3.56 clean and 3.26
        // breaking up below. Expressed in integers so the check is exact:
        // 100 x capture / fetch lands within [340, 404].
        for (uint16_t capture = 250; capture <= 1200; ++capture) {
            uint32_t ratio = 100u * capture / MemoryWindow::fetchFor(capture);
            CHECK(ratio >= 340);
            CHECK(ratio <= 404);
        }
    }

    SUBCASE("it never leaves the register's range") {
        for (uint32_t capture = 0; capture <= 4095; capture += 3)
            CHECK(MemoryWindow::fetchFor((uint16_t)capture) <= MemoryWindow::FetchMax);
    }

    SUBCASE("it is continuous -- one pixel of capture never moves it far") {
        // Stepping is not good enough -- values between the steps were measured
        // not working, so it has to ramp. A pad press changes the capture by one
        // unit, so the fetch must change by at most one.
        uint16_t previous = MemoryWindow::fetchFor(250);
        for (uint16_t capture = 251; capture <= 1200; ++capture) {
            uint16_t fetch = MemoryWindow::fetchFor(capture);
            CHECK(fetch >= previous);
            CHECK(fetch - previous <= 1);
            previous = fetch;
        }
    }

    SUBCASE("the burst covers the capture, whatever raster it is shown on") {
        // Whether 4 requests per line is right may depend on the raster; that
        // the burst must cover the line's source pixels does not. fetchFor()
        // takes no raster, so there is no gate left to switch the rule off.
        CHECK(MemoryWindow::fetchFor(1185) == 297);
        CHECK(MemoryWindow::fetchFor(1009) == 253);
        CHECK(MemoryWindow::fetchFor(279) == 70);
    }

    SUBCASE("a dropped capture read is not a framing") {
        // Capture 0 has no ratio to hold, and 0 would stop playback entirely.
        CHECK(MemoryWindow::fetchFor(0) == MemoryWindow::DefaultFetch);
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
            CHECK(MemoryWindow::strideFor(line) >= MemoryWindow::fetchFor(capture));
    }

    SUBCASE("it is a property of the LINE, so a zoom cannot move it") {
        // The alternative is the widest capture actually reachable, which
        // subtracts the hsync pulse -- and that comes from a live measurement
        // that moves by a unit between solves, so the stride would be rewritten
        // on an arbitrary pad press, re-laying the buffer out under the picture.
        CHECK(MemoryWindow::strideFor(line) > MemoryWindow::fetchFor(line - 80));
    }

    SUBCASE("it stays inside the register, at any line the divider allows") {
        CHECK(MemoryWindow::strideFor(line) <= MemoryWindow::OffsetMax);
        CHECK(MemoryWindow::strideFor(MemoryWindow::FetchMax * 8) <= MemoryWindow::OffsetMax);
    }

    SUBCASE("a dropped capture read gives a stride, not nothing") {
        CHECK(MemoryWindow::strideFor(0) == MemoryWindow::DefaultFetch);
    }
}
