#include "SourceStandard.h"

#include <Arduino.h>

#include "Adc.h"
#include "Tv5725.h"
#include "Deinterlacer.h"
#include "InputFormatter.h"
#include "SourceMeasurement.h"
#include "SyncProcessor.h"
#include "VideoProcessor.h"

namespace Tv5725 {

namespace {

// The oversampling a source gets where its standard asks for nothing else.
const uint8_t DefaultOversample = 2;

// Interlaced SD carries the least horizontal detail of anything the chip
// takes, so it has the most room to oversample -- and the crossover row that
// clock lands on is fixed, not inherited.
const uint8_t SdPostDivider = 2;
const uint8_t SdOversample = 4;

const uint8_t ProgressivePostDivider = 1;
const uint8_t ProgressiveOversample = 2;

// Past this the clock the progressive post divider produces is outside its
// crossover row, and the octave below is the one that fits.
const uint16_t TallLines = 650;
const uint8_t TallPostDivider = 0;

// ADC_FLTR 3 is the 40 MHz corner, the narrowest RD-5725-1.1 offers; 1 is the
// 110 MHz one, which is what a line carrying HD detail needs.
const uint8_t AnalogFilter40MHz = 3;
const uint8_t AnalogFilter110MHz = 1;

// PLLAD_KS 0 is the top crossover row, 162..80 MHz.
const uint8_t HdPostDivider = 0;

const uint8_t PllRateSamples = 8;
const uint32_t PllRateLowGainFrom = 200;
const uint32_t PllRateLowGainTo = 1800;

// Whether the ADC PLL is running where the low VCO gain suits it better than
// the high one. Averaged, because one sample costs little and the rate decides
// a bit that is not revisited.
bool pllWantsLowGain()
{
    uint32_t rate = 0;
    for (uint8_t i = 0; i < PllRateSamples; ++i)
        rate += getPllRate();
    rate /= PllRateSamples;
    return rate > PllRateLowGainFrom && rate < PllRateLowGainTo;
}

// Read twice with a settle between. A single count caught while the source is
// changing mode would move the divider for a source that is not tall.
bool sourceIsTall()
{
    if (SourceMeasurement::measureSourceLines() <= TallLines)
        return false;
    delay(20);
    return SourceMeasurement::measureSourceLines() > TallLines;
}

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

uint8_t SourceStandard::apply(uint8_t postDivider) const
{
    if (isSd())
        return applySd();
    if (isProgressive())
        return applyProgressive();

    const uint8_t oversample = Adc::applyOversample(postDivider, DefaultOversample);
    if (isHd())
        applyHd();
    return oversample;
}

void SourceStandard::applyHd() const
{
    Adc::ADC_FLTR::write(AnalogFilter110MHz);
    if (standard_ == 6 || standard_ == 7)
        Adc::PLLAD_KS::write(HdPostDivider);
    InputFormatter::IF_PRGRSV_CNTRL::write(1);
    InputFormatter::IF_HS_DEC_FACTOR::write(0);
    Tv5725::INPUT_FORMATTER_02::write(0x74);
    VideoProcessor::VDS_Y_DELAY::write(3);
}

uint8_t SourceStandard::applySd() const
{
    Adc::ADC_FLTR::write(AnalogFilter40MHz);
    Adc::PLLAD_KS::write(SdPostDivider);
    const uint8_t oversample = Adc::applyOversample(SdPostDivider, SdOversample);

    InputFormatter::IF_SEL_WEN::write(0);

    // Only a component source arrives with luma and chroma on separate paths,
    // so only a component source needs them realigned.
    if (inputIsYpBpR_) {
        InputFormatter::IF_HS_TAP11_BYPS::write(0);
        InputFormatter::IF_HS_Y_PDELAY::write(2);
        VideoProcessor::VDS_V_DELAY::write(0);
        VideoProcessor::VDS_Y_DELAY::write(3);
    }

    return oversample;
}

uint8_t SourceStandard::applyProgressive() const
{
    Adc::ADC_FLTR::write(AnalogFilter40MHz);
    Adc::PLLAD_KS::write(ProgressivePostDivider);

    SyncProcessor::writeSdVsyncStart(14);
    SyncProcessor::writeSdVsyncStop(11);
    InputFormatter::IF_HB_SP::write(0);

    const uint8_t oversample =
        Adc::applyOversample(ProgressivePostDivider, ProgressiveOversample);

    InputFormatter::IF_SEL_WEN::write(1);
    InputFormatter::IF_HS_SEL_LPF::write(0);
    InputFormatter::IF_HS_TAP11_BYPS::write(0);
    InputFormatter::IF_HS_Y_PDELAY::write(3);
    VideoProcessor::VDS_V_DELAY::write(1);
    Deinterlacer::MADPT_Y_DELAY_UV_DELAY::write(1);
    VideoProcessor::VDS_Y_DELAY::write(3);

    // Only the post divider moves; PLLAD_CKOS keeps the tap chosen against the
    // one above it, so the two describe different ratios from here on.
    if (standard_ == 9 && sourceIsTall())
        Adc::PLLAD_KS::write(TallPostDivider);

    if (standard_ == 3) {
        SyncProcessor::writeSdVsyncStart(16);
        SyncProcessor::writeSdVsyncStop(13);
        InputFormatter::IF_HB_ST::write(30);
        InputFormatter::IF_HBIN_ST::write(0x20);
        InputFormatter::IF_HBIN_SP::write(0x60);
    } else if (standard_ == 4) {
        InputFormatter::IF_HBIN_SP::write(0x40);
        InputFormatter::IF_HBIN_ST::write(0x20);
        InputFormatter::IF_HB_ST::write(0x30);
    } else if (standard_ == 8) {
        if (pllWantsLowGain())
            Adc::PLLAD_FS::write(0);

        Adc::PLLAD_ICP::write(6);
        Adc::ADC_FLTR::write(AnalogFilter110MHz);
        InputFormatter::IF_HB_ST::write(30);
        InputFormatter::IF_HBIN_SP::write(0x60);
    }

    return oversample;
}

}  // namespace Tv5725
