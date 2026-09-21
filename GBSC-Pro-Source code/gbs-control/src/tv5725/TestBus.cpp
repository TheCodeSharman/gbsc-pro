#include "TestBus.h"

#include "Chip.h"
#include "InputFormatter.h"
#include "SyncProcessor.h"
#include "VideoProcessor.h"

namespace Tv5725 {

void TestBus::select(uint8_t signal)
{
    TEST_BUS_SEL::write(signal);
    TEST_BUS_EN::write(1);
    Chip::PAD_BOUT_EN::write(1);
}

void TestBus::driveFormatter(uint8_t signal)
{
    Tv5725::Tie<InputFormatter::IF_TEST_EN,
                InputFormatter::IF_TEST_SEL>::write(1, signal);
}

void TestBus::selectInputVsync()
{
    driveFormatter(FormatterVertical);
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

TestBus::Hold::Hold()
    : sel_(TEST_BUS_SEL::read()),
      pad_(Chip::PAD_BOUT_EN::read()),
      ifSel_(InputFormatter::IF_TEST_SEL::read()),
      ifEn_(InputFormatter::IF_TEST_EN::read()),
      vdsEn_(VideoProcessor::VDS_TEST_EN::read()),
      spModule_(SyncProcessor::SP_TEST_MODULE::read()),
      spSignal_(SyncProcessor::SP_TEST_SIGNAL_SEL::read()),
      spEn_(SyncProcessor::SP_TEST_EN::read()),
      enabled_(TEST_BUS_EN::read() == 1)
{
}

TestBus::Hold::~Hold()
{
    Tv5725::Tie<SyncProcessor::SP_TEST_EN, SyncProcessor::SP_TEST_MODULE,
        SyncProcessor::SP_TEST_SIGNAL_SEL>::write(spEn_, spModule_, spSignal_);
    Tv5725::Tie<InputFormatter::IF_TEST_EN, InputFormatter::IF_TEST_SEL>::write(ifEn_,
                                                                       ifSel_);
    VideoProcessor::VDS_TEST_EN::write(vdsEn_);
    Tv5725::Tie<TEST_BUS_SEL, TEST_BUS_EN>::write(sel_, enabled_ ? 1 : 0);
    Chip::PAD_BOUT_EN::write(pad_);
}

uint16_t TestBus::read() { return TEST_BUS::read(); }

uint8_t TestBus::readHigh() { return TEST_BUS_2F::read(); }

}  // namespace Tv5725
