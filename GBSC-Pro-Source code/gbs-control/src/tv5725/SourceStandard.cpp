#include "SourceStandard.h"

#include "Adc.h"
#include "Tv5725.h"
#include "Deinterlacer.h"
#include "InputFormatter.h"
#include "SyncProcessor.h"
#include "VideoProcessor.h"

namespace Tv5725 {

namespace {

// ADC_FLTR 3 is the 40 MHz corner, the narrowest RD-5725-1.1 offers; 1 is the
// 110 MHz one, which is what a line carrying HD detail needs.
const uint8_t AnalogFilter40MHz = 3;
const uint8_t AnalogFilter110MHz = 1;

}  // namespace

SourceStandard::SourceStandard(uint8_t videoStandardInput, bool inputIsYpBpR)
    : standard_(videoStandardInput), inputIsYpBpR_(inputIsYpBpR)
{
}

bool SourceStandard::isSd() const
{
    return standard_ == 1 || standard_ == 2;
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
    if (isSd())
        applySd();
    else if (isProgressive())
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

void SourceStandard::applySd() const
{
    Adc::ADC_FLTR::write(AnalogFilter40MHz);
    InputFormatter::IF_SEL_WEN::write(0);

    // Only a component source arrives with luma and chroma on separate paths,
    // so only a component source needs them realigned.
    if (inputIsYpBpR_) {
        InputFormatter::IF_HS_TAP11_BYPS::write(0);
        InputFormatter::IF_HS_Y_PDELAY::write(2);
        VideoProcessor::VDS_V_DELAY::write(0);
        VideoProcessor::VDS_Y_DELAY::write(3);
    }
}

void SourceStandard::applyProgressive() const
{
    Adc::ADC_FLTR::write(AnalogFilter40MHz);

    SyncProcessor::writeSdVsyncStart(14);
    SyncProcessor::writeSdVsyncStop(11);

    InputFormatter::IF_SEL_WEN::write(1);
    InputFormatter::IF_HS_SEL_LPF::write(0);
    InputFormatter::IF_HS_TAP11_BYPS::write(0);
    InputFormatter::IF_HS_Y_PDELAY::write(3);
    VideoProcessor::VDS_V_DELAY::write(1);
    Deinterlacer::MADPT_Y_DELAY_UV_DELAY::write(1);
    VideoProcessor::VDS_Y_DELAY::write(3);

    if (standard_ == 3) {
        SyncProcessor::writeSdVsyncStart(16);
        SyncProcessor::writeSdVsyncStop(13);
    } else if (standard_ == 8) {
        Adc::ADC_FLTR::write(AnalogFilter110MHz);
    }
}

}  // namespace Tv5725
