// Host-compiled tests for Tv5725::TestBus -- `make -C test test-bus`.
//
// The pin is shared and a reader selects what it wants immediately before
// reading it. What "driven" takes is the whole of this class's contract.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/TestBus.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"

void tv5725Log(const char *) {}

using namespace Tv5725;

TEST_CASE("a selection is driven, not merely chosen")
{
    Wire.reset();

    TestBus::select(TestBus::SyncProcessor);

    CHECK(TestBus::selected() == (uint8_t)TestBus::SyncProcessor);
    CHECK(TestBus::enabled());
}

TEST_CASE("the pad the signal leaves the chip on is part of driving it")
{
    // PAD_BOUT_EN gates VB_[7:0], which is test_out_[7:0]. Clear, the pin
    // carries nothing and every selector counts zero transitions -- a reading
    // that says a dead block where the pad is off.
    Wire.reset();
    Chip::PAD_BOUT_EN::write(0);

    TestBus::selectInputVsync();

    CHECK(Chip::PAD_BOUT_EN::read() == 1);
}

TEST_CASE("selector zero is a selection like any other")
{
    Wire.reset();
    TestBus::select(TestBus::SyncProcessor);

    TestBus::selectInputVsync();

    CHECK(TestBus::selected() == 0);
    CHECK(TestBus::enabled());
    CHECK(Chip::PAD_BOUT_EN::read() == 1);
}

TEST_CASE("the source's vertical takes the input formatter's test output with it")
{
    // TEST_BUS_SEL is the top half of a two-level mux. The block that
    // generates the signal has its own enable, and with that clear the pin
    // carries nothing while every register on the bus names the right signal.
    Wire.reset();
    InputFormatter::IF_TEST_EN::write(0);

    TestBus::selectInputVsync();

    CHECK(InputFormatter::IF_TEST_EN::read() == 1);
}

TEST_CASE("the formatter is asked for the vertical, not whatever it carried")
{
    // IF_TEST_SEL picks which of the formatter's signals reaches the bus, and
    // RD-5725-1.1 tabulates no values for it. Several of the selectors this
    // firmware sweeps carry the line rate, so a reader that leaves the choice
    // where it found it can time a line and call it a field.
    Wire.reset();
    InputFormatter::IF_TEST_SEL::write(0);

    TestBus::selectInputVsync();

    CHECK(InputFormatter::IF_TEST_SEL::read() == 3);
}

TEST_CASE("the output's vertical takes the VDS's test output with it")
{
    // Same two-level mux on the display side, and the same hole: nothing on
    // the measuring path enabled the VDS's test output, so timing the output
    // frame rate depended on a reset function having run earlier in the boot.
    Wire.reset();
    VideoProcessor::VDS_TEST_EN::write(0);

    TestBus::selectOutputVsync();

    CHECK(VideoProcessor::VDS_TEST_EN::read() == 1);
}

TEST_CASE("a hold puts back the formatter's selection, not just the selector")
{
    // Three of the borrowers restored TEST_BUS_SEL alone while having changed
    // the sub-selection too, which is the half nothing owned.
    Wire.reset();
    TestBus::selectInputVsync();

    {
        const TestBus::Hold held;
        InputFormatter::IF_TEST_SEL::write(9);
        InputFormatter::IF_TEST_EN::write(0);
    }

    CHECK(InputFormatter::IF_TEST_SEL::read() == 3);
}

TEST_CASE("a hold puts back the sync processor's stage")
{
    Wire.reset();
    SyncProcessor::driveTestBus(SyncProcessor::TestModuleOutProc, 0);

    {
        const TestBus::Hold held;
        SyncProcessor::driveTestBus(SyncProcessor::TestModuleVsActDet, 4);
    }

    CHECK(SyncProcessor::SP_TEST_MODULE::read() ==
          (uint8_t)SyncProcessor::TestModuleOutProc);
}

TEST_CASE("a hold puts back the pad it found cleared")
{
    // The auto-gain path clears the pad and then selects, and select() drives
    // the pad -- so the borrower cannot hold the pad down by hand.
    Wire.reset();
    Chip::PAD_BOUT_EN::write(0);

    {
        const TestBus::Hold held;
        TestBus::select(0x0b);
    }

    CHECK(Chip::PAD_BOUT_EN::read() == 0);
}

TEST_CASE("a hold puts back a bus that was not enabled")
{
    Wire.reset();
    TestBus::enable(false);

    {
        const TestBus::Hold held;
        TestBus::select(0x0b);
    }

    CHECK(!TestBus::enabled());
}

// --- is there a signal at all -------------------------------------------------

// The bus reading, seeded straight into the fake's banks so it is an INPUT
// rather than something the code under test wrote. TEST_BUS is 16 bits at
// s0_2E.
static void seedTestBus(uint16_t value)
{
    Wire.bank[0][0x2E] = (uint8_t)(value & 0xFF);
    Wire.bank[0][0x2F] = (uint8_t)(value >> 8);
}

TEST_CASE("a signal is reported from what the bus counts, not from a status bit")
{
    // STATUS_SYNC_PROC_HSACT reads like the answer and is not one: measured, it
    // held 0 across a 450 ms window of ~45 samples on a YPbPr source that then
    // acquired, and 1 in 40 of 40 on a settled separate-sync one. The bus
    // carries the stage's own activity instead.
    Wire.reset();

    seedTestBus(0x0400);
    CHECK(SyncProcessor::signalPresent());

    seedTestBus(0x0000);
    CHECK_FALSE(SyncProcessor::signalPresent());
}

TEST_CASE("the threshold is the reading above it, not the reading on it")
{
    Wire.reset();

    seedTestBus(SyncProcessor::SignalPresentAbove);
    CHECK_FALSE(SyncProcessor::signalPresent());

    seedTestBus((uint16_t)(SyncProcessor::SignalPresentAbove + 1));
    CHECK(SyncProcessor::signalPresent());
}

TEST_CASE("asking puts the bus back where it was found")
{
    // The pin is shared, and a reader that leaves the bus on its own stage
    // silently changes what the next reader measures.
    Wire.reset();

    TestBus::select(3);   // any other selector; the numbers are undocumented
    SyncProcessor::driveTestBus(SyncProcessor::TestModuleVsActDet, 0);
    const uint8_t selector = TestBus::selected();
    const uint32_t stage = SyncProcessor::SP_TEST_MODULE::read();

    seedTestBus(0x0400);
    CHECK(SyncProcessor::signalPresent());

    CHECK(TestBus::selected() == selector);
    CHECK(SyncProcessor::SP_TEST_MODULE::read() == stage);
}
