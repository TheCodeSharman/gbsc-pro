// Host-compiled unit tests for src/tv5725/MemoryMap.h -- `make -C test memory-map`.
//
// The SDRAM layout, and whether the frame about to be captured fits in the
// buffer the chip was told to put it in. The engine computes the capture window
// from the source and the framing; without this, nothing compares the two.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/MemoryMap.h"

using Tv5725::MemoryMap;

TEST_CASE("the address space is the 21 bits the part actually has")
{
    // 2^21 words of 32 bits = 8 MB, which is the single EM638325TS the schematic
    // fits -- "2M x 32 SDRAM", four banks, one chip select. The address space
    // matches the part exactly, with nothing spare and nothing short.
    CHECK(MemoryMap::SpaceWords == 2097152u);
}

TEST_CASE("a pixel costs one word, because the chip is told to add one per pixel")
{
    // CAP_ADR_ADD_2 (s4_21[7]) reads 0 on the bench and in all twelve tables, so
    // 24-bit RGB on a 32-bit bus takes a whole word with 8 bits unused. Read off
    // the register: a frame-arithmetic argument gives 16 bpp and is wrong by a
    // factor of two.
    CHECK(MemoryMap::WordsPerPixel == 1);
    CHECK(MemoryMap::wordsFor(1009, 527) == 531743u);
}

TEST_CASE("the capture region is everything above the field store")
{
    // The field store keeps its measured guard at 0x052000; the capture buffer
    // starts directly above it and runs to the top of the space, so nothing is
    // stranded between them. Capture is the region that can actually run out.
    CHECK(MemoryMap::CaptureStart == 0x060000u);
    CHECK(MemoryMap::CaptureGuard == MemoryMap::SpaceWords - 1);
    CHECK(MemoryMap::captureWords() == 1703936u);
}

TEST_CASE("the capture buffer starts clear of the field store's guard")
{
    // The invariant that makes the map safe at all: capture must begin above
    // where the write FIFO is allowed to reach, or the two regions overlap and
    // the corruption is a frame appearing inside the deinterlacer's field.
    CHECK(MemoryMap::CaptureStart > MemoryMap::FieldStoreGuard);
    CHECK(MemoryMap::marginWords() == 57344u);  // 0x060000 - 0x052000
}

TEST_CASE("the framing the bench actually runs fits, with room")
{
    // 1009 x 527, the measured capture on the RiscPC at 320x256@50.
    CHECK(MemoryMap::captureFits(1009, 527));
}

TEST_CASE("a capture that would run past the top of memory is refused")
{
    // At 527 lines the region holds 3233 samples per line -- 3233 x 527 =
    // 1,703,791 against 1,703,936 available -- and one more does not fit. It has
    // to be caught before it reaches the chip, whose answer to an overrun is to
    // wrap: a wrong address on screen and no report.
    CHECK(MemoryMap::captureFits(3233, 527));
    CHECK_FALSE(MemoryMap::captureFits(3234, 527));
}

TEST_CASE("the widest capture allowed is reported, so a caller can clamp")
{
    // Refusing is not enough on its own -- the engine needs to know what it may
    // have instead, or the only options are "works" and "black screen".
    CHECK(MemoryMap::maxCaptureWidth(527) == 3233u);
    CHECK(MemoryMap::captureFits(MemoryMap::maxCaptureWidth(527), 527));
    CHECK_FALSE(MemoryMap::captureFits(MemoryMap::maxCaptureWidth(527) + 1, 527));
}

TEST_CASE("the old map would have refused captures the new one allows")
{
    // Why the base moved. 0x100000 left 1,048,576 words, so at 527 lines
    // anything past 1989 samples overran -- and PLLAD_MD goes to 4095, so that
    // was reachable rather than theoretical.
    const uint32_t oldRegion = MemoryMap::SpaceWords - 0x100000u;
    CHECK(oldRegion == 1048576u);
    CHECK(MemoryMap::wordsFor(1990, 527) > oldRegion);
    CHECK(MemoryMap::captureFits(1990, 527));
}

TEST_CASE("a capture too wide for memory is narrowed, not refused")
{
    // Clamp rather than fail, as readCapture() does against the source line: a
    // black screen is a worse answer than a slightly narrower picture. HORIZONTAL
    // is what gives, because capture width is in ADC samples and PLLAD_MD is 12
    // bits, while the line count is the source's.
    CHECK(MemoryMap::clampWidth(5000, 527) == 3233u);
    CHECK(MemoryMap::captureFits(MemoryMap::clampWidth(5000, 527), 527));
}

TEST_CASE("a capture that already fits is returned untouched")
{
    // The clamp must be invisible in the normal case, or it becomes a second
    // owner of the capture width and the framing stops being the truth.
    CHECK(MemoryMap::clampWidth(1009, 527) == 1009u);
}

TEST_CASE("zero-sized captures are not a special case")
{
    // Degenerate rather than dangerous: a solver mid-iteration can ask, and the
    // answer is yes rather than a division or an underflow.
    CHECK(MemoryMap::wordsFor(0, 0) == 0u);
    CHECK(MemoryMap::captureFits(0, 0));
}
