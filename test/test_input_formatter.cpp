// Host-compiled unit tests for src/tv5725/InputFormatter.cpp
// -- `make -C test input-formatter`.
//
// Same fake-Wire seam as test_memory_bus.cpp and test_frame_buffer.cpp: poison
// every bank, run init(), and ask the fake what was touched.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "LoggedLines.h"
#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"

static Tv5725::InputFormatter inputFormatter;

using Tv5725::InputFormatter;

// What the output can display, in the units the capture is counted in.
static uint16_t showableIn(uint16_t frameLines)
{
    return Tv5725::AxisVertical.maximumCapture(frameLines, 0, 0);
}

// Leaves IF_LD_ST reading 1, which is neither the 5 this writes nor the 3 the
// NTSC tables carry, so the assertion below can fail.
static const uint8_t Poison = 0xC2;

struct FreshChip {
    FreshChip()
    {
        Wire.reset();
        Wire.poison(Poison);
        inputFormatter.init();
    }
};

TEST_CASE("the line double write reset position is owned, one value for every mode")
{
    FreshChip chip;

    CHECK(Wire.field(1, 0x0C, 1, 4) == 5);  // IF_LD_ST
    CHECK(Wire.touched[1][0x0C]);
}

TEST_CASE("the rest of s1_0c is left to its own owners")
{
    FreshChip chip;

    // IF_LD_RAM_BYPS is bit 0 and IF_INI_ST is bits 7-5 of the same byte, and
    // both are written by doPostPresetLoadSteps(). Read-modify-write is what
    // keeps three owners in one byte from clobbering each other, and this is
    // the assertion that says so rather than assuming it.
    CHECK(Wire.field(1, 0x0C, 0, 1) == ((Poison >> 0) & 0x1));
    CHECK(Wire.field(1, 0x0C, 5, 3) == ((Poison >> 5) & 0x7));
}

TEST_CASE("the input formatter stays inside segment 1")
{
    // The input formatter is segment 1, so a write anywhere else is a wrong-bank
    // write, which is the defect the fake bus exists to catch.
    FreshChip chip;

    for (uint8_t s = 0; s < FakeTwoWire::Segments; ++s) {
        if (s == 1)
            continue;
        for (int r = 0; r < 256; ++r) {
            CAPTURE(s);
            CAPTURE(r);
            REQUIRE_FALSE(Wire.touched[s][r]);
        }
    }
}

TEST_CASE("the input formatter writes the addresses it owns and no others")
{
    // The three blanking sets split three ways, and this list is where that is
    // stated. Set 2 at s1_18/s1_1a is the capture window and the ENGINE's, so it
    // is absent along with IF_LINE_SP. Set 0 at s1_10/s1_12 and the scale-down
    // pair at s1_24/s1_26 are constants with no scaling-path writer, so they are
    // here. Set 1 at s1_14/s1_16 is measured inert and belongs to nobody, which
    // test_bringup.cpp asserts.
    FreshChip chip;

    const uint8_t owned[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                             0x08, 0x09, 0x0A, 0x0B, 0x0C,
                             0x10, 0x11, 0x12, 0x13,
                             0x24, 0x25, 0x26, 0x27, 0x28};
    bool expected[256] = {false};
    for (size_t i = 0; i < sizeof(owned) / sizeof(owned[0]); ++i)
        expected[owned[i]] = true;

    for (int r = 0; r < 256; ++r) {
        CAPTURE(r);
        CHECK(Wire.touched[1][r] == expected[r]);
    }
}

// --- the scan mode, which is four registers deciding one thing ----------------

TEST_CASE("a line-doubled source undoes every progressive setting")
{
    FreshChip chip;

    inputFormatter.applyLineDoubling(false, false);
    inputFormatter.applyLineDoubling(true, false);

    CHECK(Wire.field(1, 0x0B, 4, 2) == 1);  // IF_HS_DEC_FACTOR
    CHECK(Wire.field(1, 0x0B, 7, 1) == 0);  // IF_LD_SEL_PROV
    CHECK(Wire.field(1, 0x0C, 0, 1) == 0);  // IF_LD_RAM_BYPS
    CHECK(Wire.field(1, 0x00, 6, 1) == 0);  // IF_PRGRSV_CNTRL
}

TEST_CASE("a progressive source undoes every line-doubled setting")
{
    FreshChip chip;

    inputFormatter.applyLineDoubling(true, false);
    inputFormatter.applyLineDoubling(false, false);

    CHECK(Wire.field(1, 0x0B, 4, 2) == 0);  // IF_HS_DEC_FACTOR
    CHECK(Wire.field(1, 0x0B, 7, 1) == 1);  // IF_LD_SEL_PROV
    CHECK(Wire.field(1, 0x0C, 0, 1) == 1);  // IF_LD_RAM_BYPS
    CHECK(Wire.field(1, 0x00, 6, 1) == 1);  // IF_PRGRSV_CNTRL
}

TEST_CASE("the line doubler's write enable follows the scan mode")
{
    // IF_SEL_WEN is the write enable FOR the line double, so which way it goes
    // is the same fact applyLineDoubling() is already given. Measured on both bench
    // sources: 0 on the 15 kHz RGBHV raster, 1 on component 480p.
    FreshChip chip;

    inputFormatter.applyLineDoubling(false, false);
    CHECK(Wire.field(1, 0x02, 0, 1) == 1);  // IF_SEL_WEN

    inputFormatter.applyLineDoubling(true, false);
    CHECK(Wire.field(1, 0x02, 0, 1) == 0);
}

TEST_CASE("the horizontal low-pass follows the scan mode the other way")
{
    FreshChip chip;

    inputFormatter.applyLineDoubling(false, false);
    CHECK(Wire.field(1, 0x02, 1, 1) == 0);  // IF_HS_SEL_LPF

    inputFormatter.applyLineDoubling(true, false);
    CHECK(Wire.field(1, 0x02, 1, 1) == 1);
}

TEST_CASE("a line-doubled component source takes a shorter luma delay")
{
    // Only a component source arrives with luma and chroma on separate paths,
    // so only a component source needs them realigned -- and only where the
    // line doubler adds the stage that puts them out.
    FreshChip chip;

    inputFormatter.applyLineDoubling(true, true);
    CHECK(Wire.field(1, 0x02, 5, 2) == 2);  // IF_HS_Y_PDELAY

    inputFormatter.applyLineDoubling(true, false);
    CHECK(Wire.field(1, 0x02, 5, 2) == 3);

    inputFormatter.applyLineDoubling(false, true);
    CHECK(Wire.field(1, 0x02, 5, 2) == 3);
}

TEST_CASE("a progressive source blanks nothing at the head of the captured line")
{
    FreshChip chip;

    inputFormatter.applyLineDoubling(false, false);

    // With the line-double FIFO bypassed IF_HBIN_SP is a blanking edge in the
    // capture window's own units, so anything it holds is a second left crop
    // nothing asked for. 0 blanks the whole line against IF_HBIN_ST 0.
    CHECK(Wire.field(1, 0x26, 0, 12) == 2);
}

TEST_CASE("a line-doubled source keeps the line-double FIFO's reset position")
{
    FreshChip chip;

    inputFormatter.applyLineDoubling(false, false);
    inputFormatter.applyLineDoubling(true, false);

    CHECK(Wire.field(1, 0x26, 0, 12) == 272);
}

TEST_CASE("a line-doubled source blanks the tail of the captured line")
{
    FreshChip chip;

    Wire.bank[1][0x24] = 50;
    inputFormatter.applyLineDoubling(true, false);

    CHECK(Wire.field(1, 0x24, 0, 12) == InputFormatter::DoubledTailBlanking);
}

TEST_CASE("a progressive source blanks no tail at all")
{
    FreshChip chip;

    Wire.bank[1][0x24] = 50;
    inputFormatter.applyLineDoubling(false, false);

    CHECK(Wire.field(1, 0x24, 0, 12) == 0);
}

// --- the per-load state, which shares bytes with owners that are not here -----

TEST_CASE("the vertical timing leaves the scan mode alone")
{
    // IF_VS_SEL is bit 5 of s1_00 and IF_PRGRSV_CNTRL is bit 6 of the same byte,
    // written by applyLineDoubling() from a different caller at a different time.
    FreshChip chip;

    inputFormatter.applyLineDoubling(false, false);
    inputFormatter.applyVerticalTiming(InputFormatter::VcrTiming);

    CHECK(Wire.field(1, 0x00, 5, 1) == 0);  // IF_VS_SEL
    CHECK(Wire.field(1, 0x00, 6, 1) == 1);  // IF_PRGRSV_CNTRL
}

TEST_CASE("normal vertical timing is the other value of the same field")
{
    FreshChip chip;

    inputFormatter.applyVerticalTiming(InputFormatter::VcrTiming);
    inputFormatter.applyVerticalTiming(InputFormatter::NormalTiming);

    CHECK(Wire.field(1, 0x00, 5, 1) == 1);  // IF_VS_SEL
    CHECK(Wire.field(1, 0x01, 0, 1) == 1);  // IF_VS_FLIP
}

TEST_CASE("disabling the auto offset leaves the rest of its bytes alone")
{
    FreshChip chip;

    inputFormatter.disableAutoOffset();

    CHECK(Wire.field(1, 0x29, 0, 1) == 0);  // IF_AUTO_OFST_EN
    CHECK(Wire.field(1, 0x29, 1, 1) == 0);  // IF_AUTO_OFST_PRD
    CHECK(Wire.field(1, 0x2A, 0, 8) == 0);  // both detection ranges
    CHECK(Wire.field(1, 0x29, 2, 6) == ((Poison >> 2) & 0x3F));
}

// Which scan mode a source of a given line count is captured in.

TEST_CASE("line doubling is decided by the source line count")
{
    // Line doubling exists so there are enough lines for the rest of the chain
    // to reach the output resolution. That is a question about how many lines
    // arrive, not how fast they arrive.
    //
    // Measured over the RISC PC's modes: 261, 311 and 363 total lines are
    // captured doubled; 448, 524, 533 and 627 are not. The boundary sits in
    // that gap. It is reproduced rather than derived, so that moving it is a
    // deliberate change with its own acceptance test.
    CHECK(InputFormatter::shouldDoubleLine(261));
    CHECK(InputFormatter::shouldDoubleLine(311));
    CHECK(InputFormatter::shouldDoubleLine(363));
    CHECK_FALSE(InputFormatter::shouldDoubleLine(448));
    CHECK_FALSE(InputFormatter::shouldDoubleLine(524));
    CHECK_FALSE(InputFormatter::shouldDoubleLine(533));
    CHECK_FALSE(InputFormatter::shouldDoubleLine(627));

    // An interlaced PAL frame is 625 lines, which is plenty. What it needs is
    // DEINTERLACING, which is a separate register and a separate decision.
    CHECK_FALSE(InputFormatter::shouldDoubleLine(625));

    // No measurement yet. The default is the one a low-line-count source needs,
    // because that is the source a wrong guess leaves without enough lines.
    CHECK(InputFormatter::shouldDoubleLine(0));
}
TEST_CASE("a source is not doubled into an output that cannot show the result")
{
    // Doubling turns a 311-line source into 624 units, and the part cannot
    // minify: an output with less room than that shows the top of the doubled
    // frame and nothing else, with the control dead in both directions. So the
    // question is not only how many lines arrive, but how many can be shown.
    CHECK(InputFormatter::shouldDoubleLine(311, showableIn(1125)));  // 1080p
    CHECK(InputFormatter::shouldDoubleLine(311, showableIn(750)));   // 720p
    CHECK_FALSE(InputFormatter::shouldDoubleLine(311, showableIn(525)));  // 480p
    CHECK_FALSE(InputFormatter::shouldDoubleLine(311, showableIn(625)));  // 576p

    SUBCASE("a shorter source still doubles into the same output") {
        // 288 lines doubled is 578, which a 625-line frame holds.
        CHECK(InputFormatter::shouldDoubleLine(288, showableIn(625)));
    }

    SUBCASE("no output raster asks the source alone") {
        // Bypass, and every caller that has not solved a raster yet.
        CHECK(InputFormatter::shouldDoubleLine(311, 0));
        CHECK_FALSE(InputFormatter::shouldDoubleLine(524, 0));
    }
}

// The input formatter's own vertical measurement. It has one owner because two
// blocks measure the frame and only this one is gated on the measurement
// completing.
TEST_CASE("the vertical period is zero until the measurement completes")
{
    Wire.reset();
    Wire.bank[0][0x07] = (uint8_t)((624 & 0x7F) << 1);
    Wire.bank[0][0x08] = (uint8_t)((624 >> 7) & 0x0F);
    Wire.bank[0][0x00] |= 0x01;   // STATUS_IF_VT_OK

    CHECK(inputFormatter.verticalPeriod() == 624);

    SUBCASE("and a measurement that did not complete claims nothing") {
        // VPERIOD_IF is debris on a separate-sync source, where it reads values
        // like 20 against a true 311 -- so the register's value is not the
        // thing to judge it by.
        Wire.bank[0][0x00] &= (uint8_t)~0x01;
        CHECK(inputFormatter.verticalPeriod() == 0);
    }
}
