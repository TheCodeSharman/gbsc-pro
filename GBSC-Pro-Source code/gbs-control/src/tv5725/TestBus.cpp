#include "TestBus.h"

#include "Chip.h"
#include "InputFormatter.h"
#include "VideoProcessor.h"

namespace Tv5725 {

void TestBus::select(uint8_t signal)
{
    TEST_BUS_SEL::write(signal);
    TEST_BUS_EN::write(1);
    Chip::PAD_BOUT_EN::write(1);
}

void TestBus::selectInputVsync()
{
    InputFormatter::IF_TEST_EN::write(1);
    InputFormatter::IF_TEST_SEL::write(FormatterVertical);
    select(InputVsync);
}

void TestBus::selectOutputVsync()
{
    VideoProcessor::VDS_TEST_EN::write(1);
    select(OutputVsync);
}

uint8_t TestBus::selected() { return TEST_BUS_SEL::read(); }

bool TestBus::enabled() { return TEST_BUS_EN::read() == 1; }

void TestBus::enable(bool on) { TEST_BUS_EN::write(on ? 1 : 0); }

uint16_t TestBus::read() { return TEST_BUS::read(); }

uint8_t TestBus::readHigh() { return TEST_BUS_2F::read(); }

}  // namespace Tv5725
