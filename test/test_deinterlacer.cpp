// Host-compiled unit tests for src/tv5725/Deinterlacer.cpp
// -- `make -C test deinterlacer`.
//
// This block is asserted as a BYTE IMAGE rather than field by field. The class
// replaced a 64-byte table, and the question that matters is whether any byte
// moved -- which 120 separate field assertions state less directly and less
// completely, since a field nobody thought to list is invisible to them.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Deinterlacer.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ModeDetect.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/FrameBuffer.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

using Tv5725::Deinterlacer;
using Tv5725::FrameBuffer;
using Tv5725::VideoProcessor;

static const uint8_t Poison = 0xA5;

// s2_00..s2_3f as the table shipped them.
static const uint8_t Expected[64] = {
    0xFF, 0x03, 0xEC, 0x00, 0xFF, 0xFF, 0x00, 0x1B,
    0x00, 0x70, 0x00, 0x00, 0x0F, 0x04, 0x7F, 0x14,
    0x18, 0x00, 0x8E, 0x00, 0x00, 0x00, 0x80, 0x00,
    0xC0, 0x61, 0x04, 0x15, 0x00, 0x00, 0x00, 0x10,
    0x30, 0x12, 0x04, 0x0F, 0x04, 0x00, 0x4C, 0x0C,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7F, 0x7F, 0x11, 0x10, 0x03, 0x0B,
    0x04, 0x44, 0x60, 0x04, 0x0F, 0x00, 0x00, 0x00,};

// The bits at each address that carry no field, which the class does not write
// and which therefore keep whatever was there. Five of them are 1 in the table:
// s2_02[3:2], s2_04[7], s2_05[7], s2_12[7] and s2_26[3:2], every one marked
// RESERVED in RD-5725-1.1's own bit table.
static const uint8_t Reserved[64] = {
    0x00, 0x00, 0x1E, 0xE0, 0x80, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x4F, 0x80, 0xC0, 0x80, 0x80, 0x00,
    0x00, 0x07, 0x80, 0x8B, 0x00, 0x00, 0x0C, 0x00,
    0x04, 0x02, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00,
    0x0C, 0xC8, 0xE0, 0xE0, 0xFB, 0xFF, 0x3F, 0x80,
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x80, 0x80, 0xFF, 0xFF, 0xFF,};

struct FreshChip {
    FreshChip()
    {
        Wire.reset();
        Wire.poison(Poison);
        Deinterlacer::init();
    }
};

TEST_CASE("every documented bit of the block comes up as the table left it")
{
    FreshChip chip;

    for (int r = 0; r < 64; ++r) {
        const uint8_t owned = static_cast<uint8_t>(~Reserved[r]);
        CAPTURE(r);
        CHECK((Wire.bank[2][r] & owned) == (Expected[r] & owned));
    }
}

TEST_CASE("reserved bits are left alone rather than written zero")
{
    FreshChip chip;

    for (int r = 0; r < 64; ++r) {
        if (Reserved[r] == 0)
            continue;
        CAPTURE(r);
        CHECK((Wire.bank[2][r] & Reserved[r]) == (Poison & Reserved[r]));
    }
}

TEST_CASE("the deinterlacer stays inside segment 2")
{
    FreshChip chip;

    for (uint8_t s = 0; s < FakeTwoWire::Segments; ++s) {
        if (s == 2)
            continue;
        for (int r = 0; r < 256; ++r) {
            CAPTURE(s);
            CAPTURE(r);
            REQUIRE_FALSE(Wire.touched[s][r]);
        }
    }
}

TEST_CASE("the block writes the addresses it owns and no others")
{
    FreshChip chip;

    for (int r = 0; r < 256; ++r) {
        CAPTURE(r);
        CHECK(Wire.touched[2][r] == (r < 64 && Reserved[r] != 0xFF));
    }
}

TEST_CASE("the motion index fixed value is owned, despite sharing a datasheet name")
{
    // RD-5725-1.1 gives s2_0d and s2_0e the SAME name, MADPT_MI_THRESHOLD, for
    // two different functions. Keying an extraction by name loses one of them,
    // which is how s2_0e came to be absent from the register catalogue while
    // the table quietly wrote it 0x7f.
    FreshChip chip;

    CHECK(Wire.field(2, 0x0D, 0, 7) == 4);    // MADPT_MI_THRESHOLD
    CHECK(Wire.field(2, 0x0E, 0, 7) == 127);  // MADPT_MI_FIXED_VALUE
}

// Scanlines: the chroma and luma deinterlace stages made to drop alternate
// lines, plus the two registers outside this block that the effect needs.

TEST_CASE("scanlines drop alternate lines at the strength asked for")
{
    Wire.reset();
    Wire.poison(Poison);
    Deinterlacer::forgetScanlines();

    Deinterlacer::enableScanlines(0x30);

    CHECK(Deinterlacer::MADPT_UV_MI_OFFSET::read() == 0x30);
    CHECK(Deinterlacer::MADPT_Y_MI_OFFSET::read() == 0x30);
    CHECK(Deinterlacer::MADPT_EN_UV_DEINT::read() == 1);
    CHECK(Deinterlacer::MAPDT_VT_SEL_PRGV::read() == 0);
}

TEST_CASE("scanlines take the deinterlacer RAM out of bypass")
{
    // Left in bypass the effect has no field to drop from. The three bypass
    // bits go together, and leaving one behind fills the screen with random
    // colour while every register still reads correct.
    Wire.reset();
    Wire.poison(Poison);
    Deinterlacer::forgetScanlines();

    Deinterlacer::enableScanlines(0x30);

    CHECK(Deinterlacer::DIAG_BOB_PLDY_RAM_BYPS::read() == 0);
    CHECK(Deinterlacer::MADPT_PD_RAM_BYPS::read() == 0);
    CHECK(Deinterlacer::MADPT_VIIR_BYPS::read() == 0);
}

TEST_CASE("scanlines reach the two registers outside this block")
{
    // The frame buffer has to flip the line it reads back, and the video
    // processor's white level expansion carries the brightening.
    Wire.reset();
    Wire.poison(Poison);
    Deinterlacer::forgetScanlines();

    Deinterlacer::enableScanlines(0x30);

    CHECK(FrameBuffer::RFF_LINE_FLIP::read() == 1);
    CHECK(FrameBuffer::RFF_YUV_DEINTERLACE::read() == 1);
    CHECK(VideoProcessor::VDS_W_LEV_BYPS::read() == 0);
    CHECK(VideoProcessor::VDS_WLEV_GAIN::read() == 0x08);
}

TEST_CASE("turning scanlines off puts every bypass back")
{
    Wire.reset();
    Wire.poison(Poison);
    Deinterlacer::forgetScanlines();
    Deinterlacer::enableScanlines(0x30);
    Wire.poison(Poison);

    Deinterlacer::disableScanlines();

    CHECK(Deinterlacer::MAPDT_VT_SEL_PRGV::read() == 1);
    CHECK(Deinterlacer::DIAG_BOB_PLDY_RAM_BYPS::read() == 1);
    CHECK(Deinterlacer::MADPT_PD_RAM_BYPS::read() == 1);
    CHECK(Deinterlacer::MADPT_VIIR_BYPS::read() == 1);
    CHECK(VideoProcessor::VDS_W_LEV_BYPS::read() == 1);
    CHECK(FrameBuffer::RFF_LINE_FLIP::read() == 0);
}

TEST_CASE("turning scanlines off leaves the chroma deinterlace where it was")
{
    // RFF_YUV_DEINTERLACE is written on the way in and NOT on the way out --
    // the motion-adaptive path owns its value, and clearing it here would take
    // that path's setting with it.
    Wire.reset();
    FrameBuffer::RFF_YUV_DEINTERLACE::write(1);

    Deinterlacer::disableScanlines();

    CHECK(FrameBuffer::RFF_YUV_DEINTERLACE::read() == 1);
}

// The motion-adaptive deinterlacer: two fields in flight, so the frame buffer
// fetches ahead and the write side is told not to invert its start.

static unsigned g_released = 0;
static uint8_t g_progressiveAtRelease = 0xff;
static void releaseStub()
{
    ++g_released;
    g_progressiveAtRelease = (uint8_t)Deinterlacer::MAPDT_VT_SEL_PRGV::read();
}

TEST_CASE("the motion-adaptive path puts two fields in flight")
{
    Wire.reset();
    Wire.poison(Poison);
    g_released = 0;

    Deinterlacer::enableMotionAdapt(4, releaseStub);

    CHECK(FrameBuffer::WFF_ENABLE::read() == 1);
    CHECK(FrameBuffer::RFF_ENABLE::read() == 1);
    CHECK(FrameBuffer::RFF_FETCH_NUM::read() == 0x80);
    CHECK(FrameBuffer::WFF_FF_STA_INV::read() == 0);
}

TEST_CASE("the capture is released before the progressive select is cleared")
{
    // Clearing it first shows the deinterlacer a buffer nothing is filling.
    Wire.reset();
    Wire.poison(Poison);
    g_released = 0;
    g_progressiveAtRelease = 0xff;

    Deinterlacer::enableMotionAdapt(4, releaseStub);

    CHECK(g_released == 1);
    CHECK(g_progressiveAtRelease == 1);
    CHECK(Deinterlacer::MAPDT_VT_SEL_PRGV::read() == 0);
}

TEST_CASE("the vertical tap is written when the caller has one")
{
    Wire.reset();
    Wire.poison(Poison);

    Deinterlacer::enableMotionAdapt(6, releaseStub);

    CHECK(Deinterlacer::MADPT_VTAP2_COEFF::read() == 6);
}

TEST_CASE("a caller with no vertical tap leaves the one in force")
{
    // Upstream writes the coefficient for two source standards and for nothing
    // else, so a caller that cannot name one must not have a default chosen
    // for it.
    Wire.reset();
    Deinterlacer::MADPT_VTAP2_COEFF::write(3);

    Deinterlacer::enableMotionAdapt(Deinterlacer::KeepVerticalTap, releaseStub);

    CHECK(Deinterlacer::MADPT_VTAP2_COEFF::read() == 3);
}

TEST_CASE("turning the motion-adaptive path off stops both fifos")
{
    Wire.reset();
    Wire.poison(Poison);

    Deinterlacer::disableMotionAdapt();

    CHECK(Deinterlacer::MAPDT_VT_SEL_PRGV::read() == 1);
    CHECK(FrameBuffer::WFF_ENABLE::read() == 0);
    CHECK(FrameBuffer::RFF_ENABLE::read() == 0);
    CHECK(FrameBuffer::RFF_FETCH_NUM::read() == 1);
    CHECK(FrameBuffer::WFF_FF_STA_INV::read() == 1);
    CHECK(Deinterlacer::MADPT_Y_MI_DET_BYPS::read() == 1);
}

// Whether the scanline stages are in force. State rather than a register: the
// chip has no bit for it, and the firmware kept one in an undocumented register
// that a preset load cleared behind the RAM copy's back.

TEST_CASE("nothing is applied until the scanlines are switched on")
{
    Wire.reset();
    Deinterlacer::forgetScanlines();

    CHECK_FALSE(Deinterlacer::scanlinesApplied());
}

TEST_CASE("switching the scanlines on records that they are applied")
{
    Wire.reset();
    Deinterlacer::forgetScanlines();

    Deinterlacer::enableScanlines(0x40);

    CHECK(Deinterlacer::scanlinesApplied());
}

TEST_CASE("switching them off again records that they are not")
{
    Wire.reset();
    Deinterlacer::forgetScanlines();
    Deinterlacer::enableScanlines(0x40);

    Deinterlacer::disableScanlines();

    CHECK_FALSE(Deinterlacer::scanlinesApplied());
}

TEST_CASE("switching on what is already on writes nothing")
{
    Wire.reset();
    Deinterlacer::forgetScanlines();
    Deinterlacer::enableScanlines(0x40);
    const size_t applied = Wire.trace.size();

    Deinterlacer::enableScanlines(0x40);

    CHECK(Wire.trace.size() == applied);
}

TEST_CASE("switching off what is already off writes nothing")
{
    Wire.reset();
    Deinterlacer::forgetScanlines();
    const size_t before = Wire.trace.size();

    Deinterlacer::disableScanlines();

    CHECK(Wire.trace.size() == before);
}

TEST_CASE("a preset load rewrote the stages, so what was applied is forgotten")
{
    Wire.reset();
    Deinterlacer::forgetScanlines();
    Deinterlacer::enableScanlines(0x40);

    Deinterlacer::forgetScanlines();

    CHECK_FALSE(Deinterlacer::scanlinesApplied());
}

TEST_CASE("the strength moves under scanlines that are in force")
{
    Wire.reset();
    Deinterlacer::forgetScanlines();
    Deinterlacer::enableScanlines(0x40);

    Deinterlacer::applyScanlineStrength(0x20);

    CHECK(Deinterlacer::MADPT_Y_MI_OFFSET::read() == 0x20);
    CHECK(Deinterlacer::MADPT_UV_MI_OFFSET::read() == 0x20);
}

TEST_CASE("the strength is not written when no scanlines are in force")
{
    Wire.reset();
    Deinterlacer::forgetScanlines();
    const size_t before = Wire.trace.size();

    Deinterlacer::applyScanlineStrength(0x20);

    CHECK(Wire.trace.size() == before);
}

TEST_CASE("the vertical tap follows the total the period sits at")
{
    CHECK(Deinterlacer::verticalTapFor(524) == 6);
    CHECK(Deinterlacer::verticalTapFor(624) == 4);
}

TEST_CASE("a period naming no total leaves the tap where it is")
{
    CHECK(Deinterlacer::verticalTapFor(112)
          == static_cast<uint8_t>(Deinterlacer::KeepVerticalTap));
}

