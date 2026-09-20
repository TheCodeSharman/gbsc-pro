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

    TestBus::select(TestBus::InputVsync);

    CHECK(Chip::PAD_BOUT_EN::read() == 1);
}

TEST_CASE("selector zero is a selection like any other")
{
    Wire.reset();
    TestBus::select(TestBus::SyncProcessor);

    TestBus::select(TestBus::InputVsync);

    CHECK(TestBus::selected() == 0);
    CHECK(TestBus::enabled());
    CHECK(Chip::PAD_BOUT_EN::read() == 1);
}
