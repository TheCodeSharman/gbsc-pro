#include "SourceStandard.h"

#include "Adc.h"
#include "Tv5725.h"
#include "InputFormatter.h"
#include "SyncProcessor.h"
#include "VideoProcessor.h"

namespace Tv5725 {

namespace {

// ADC_FLTR 1 is the 110 MHz corner, which is what a line carrying HD detail
// needs against Adc::init()'s widest.
const uint8_t AnalogFilter110MHz = 1;

}  // namespace

SourceStandard::SourceStandard(uint8_t videoStandardInput, bool inputIsYpBpR)
    : standard_(videoStandardInput), inputIsYpBpR_(inputIsYpBpR)
{
}

bool SourceStandard::isHd() const
{
    return standard_ == 5 || standard_ == 6 || standard_ == 7;
}

bool SourceStandard::isProgressive() const
{
    return standard_ == 3 || standard_ == 4 || standard_ == 8 || standard_ == 9;
}

void SourceStandard::apply() const
{
    if (isProgressive())
        applyProgressive();
    else if (isHd())
        applyHd();
}

void SourceStandard::applyHd() const
{
    Adc::ADC_FLTR::write(AnalogFilter110MHz);
    InputFormatter::IF_PRGRSV_CNTRL::write(1);
    InputFormatter::IF_HS_DEC_FACTOR::write(0);
    Tv5725::INPUT_FORMATTER_02::write(0x74);
    VideoProcessor::VDS_Y_DELAY::write(3);
}

void SourceStandard::applyProgressive() const
{
    SyncProcessor::writeSdVsyncStart(14);
    SyncProcessor::writeSdVsyncStop(11);
}

}  // namespace Tv5725
