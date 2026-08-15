#include "Sampling.h"

#include "Geometry.h"   // Capture's settling bounds, so there is one owner of them

#include "../../gbs_types.h"

namespace Tv5725 {

const uint32_t Sampling::MaxSampleRateHz;
const uint16_t Sampling::DividerMax;
const uint16_t Sampling::RecommendedPercent;
const uint16_t Sampling::RetimeStopPercent;

// A dropped read of ADC_CLK_ICLK1X/2X arrives as 0. Treating that as "no
// oversampling" keeps the ceiling honest; treating it as a divisor would make
// the limit infinite, which is the wrong way to be wrong about a rating.
static uint8_t atLeastOne(uint8_t oversample)
{
    return oversample == 0 ? 1 : oversample;
}

uint16_t Sampling::ifLineFor(uint16_t divider)
{
    return (uint16_t)(divider / 2);
}

// Integer, because the ESP8266 has no FPU and this runs on every solve. The
// float form it replaces truncated too, and 4095 x 93 is well inside 32 bits.
uint16_t Sampling::retimeStopFor(uint16_t divider)
{
    return (uint16_t)(((uint32_t)divider * RetimeStopPercent) / 100);
}

uint32_t Sampling::sampleRateHz(uint16_t divider, uint32_t lineRateHz,
                                uint8_t oversample)
{
    return (uint32_t)divider * lineRateHz * atLeastOne(oversample);
}

bool Sampling::withinLimit(uint16_t divider, uint32_t lineRateHz,
                           uint8_t oversample)
{
    if (lineRateHz == 0)
        return false;
    return sampleRateHz(divider, lineRateHz, oversample) <= MaxSampleRateHz;
}

uint16_t Sampling::maxDivider(uint32_t lineRateHz, uint8_t oversample)
{
    if (lineRateHz == 0)
        return 0;

    // getSourceFieldRate() reports 0 with no lock and that reaches here as a
    // line rate, so the divide is guarded above. Everything below is integer:
    // the ESP8266 has no FPU and this runs on every solve.
    uint32_t perLine = lineRateHz * atLeastOne(oversample);
    uint32_t largest = MaxSampleRateHz / perLine;

    if (largest > DividerMax)
        return DividerMax;
    return (uint16_t)largest;
}

uint32_t Sampling::lineRateFrom(uint16_t sourceLines, float fieldRateHz)
{
    if (sourceLines < Capture::SourceVerticalTotalMin
        || sourceLines > Capture::SourceVerticalTotalMax)
        return 0;

    // Which rate the line count makes plausible, then agreement within 2%.
    // Capture::PalVerticalTotalMin is the same split the firmware's mode detect uses.
    float nominal = sourceLines > Capture::PalVerticalTotalMin ? 50.0f : 60.0f;
    float error = fieldRateHz > nominal ? fieldRateHz / nominal
                                        : nominal / fieldRateHz;
    if (!(fieldRateHz > 0.0f) || !(error < 1.02f))
        return 0;

    return (uint32_t)(fieldRateHz * (float)sourceLines);
}

uint16_t Sampling::recommendedDivider(uint32_t lineRateHz, uint8_t oversample)
{
    uint16_t ceiling = maxDivider(lineRateHz, oversample);
    if (ceiling == 0)
        return 0;

    uint16_t backed = (uint16_t)(((uint32_t)ceiling * RecommendedPercent) / 100);

    // Even, so ifLineFor() divides exactly. An odd divider leaves the IF half a
    // sample out from the line the ADC is delivering.
    return (uint16_t)(backed & ~1u);
}

// --- the chosen divider, held ----------------------------------------------

Sampling::Sampling() : divider_(0) {}

bool Sampling::solve(uint32_t lineRateHz, uint8_t oversample)
{
    uint16_t chosen = recommendedDivider(lineRateHz, oversample);
    if (chosen == 0)
        return false;
    divider_ = chosen;
    return true;
}

void Sampling::adopt()
{
    divider_ = GBS::PLLAD_MD::read();
}

bool Sampling::usable() const { return divider_ != 0; }

uint16_t Sampling::divider() const { return divider_; }

uint16_t Sampling::ifLine() const { return ifLineFor(divider_); }

uint16_t Sampling::retimeStop() const { return retimeStopFor(divider_); }

void Sampling::write() const
{
    if (!usable())
        return;

    GBS::PLLAD_MD::write(divider_);
    GBS::IF_HSYNC_RST::write(ifLine());
    GBS::SP_RT_HS_SP::write(retimeStop());
}

}  // namespace Tv5725
