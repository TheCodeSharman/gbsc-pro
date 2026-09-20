#include "TestBus.h"

#include "Chip.h"

namespace Tv5725 {

void TestBus::select(uint8_t signal)
{
    TEST_BUS_SEL::write(signal);
    TEST_BUS_EN::write(1);
    Chip::PAD_BOUT_EN::write(1);
}

uint8_t TestBus::selected() { return TEST_BUS_SEL::read(); }

bool TestBus::enabled() { return TEST_BUS_EN::read() == 1; }

void TestBus::enable(bool on) { TEST_BUS_EN::write(on ? 1 : 0); }

uint16_t TestBus::read() { return TEST_BUS::read(); }

uint8_t TestBus::readHigh() { return TEST_BUS_2F::read(); }

}  // namespace Tv5725
