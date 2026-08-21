#include "DisplayClock.h"

#include "../../gbs_types.h"
#include "../clock/ClockGen.h"

namespace Tv5725 {

const uint32_t DisplayClock::CeilingHz;
const uint8_t DisplayClock::ExternalPclkIn;
const uint32_t DisplayClock::FallbackHz;
const uint8_t DisplayClock::SeedCount;

// Ascending clock: 40.5, 54, 64.8, 81, 108, 129.6, 162 MHz.
const uint8_t DisplayClock::Seeds[DisplayClock::SeedCount] = {
    0x25, 0x45, 0x55, 0x65, 0x85, 0x95, 0xA5
};

uint32_t DisplayClock::hzFor(uint8_t divider)
{
    // Lifted verbatim from the if-chain in externalClockGenResetClock(), which
    // is the only place it existed. 0x35 and 0x00 are bytes the firmware writes
    // itself -- setOutModeHdBypass() and the reset path -- and upstream maps
    // both to 81 MHz deliberately, so they are entries rather than fallbacks.
    switch (divider) {
    case 0x25: return 40500000;
    case 0x45: return 54000000;
    case 0x55: return 64800000;
    case 0x65: return 81000000;
    case 0x85: return 108000000;
    case 0x95: return 129600000;
    case 0xA5: return 162000000;
    case 0x35: return 81000000;
    case 0x00: return 81000000;
    default:   return 0;
    }
}

uint16_t DisplayClock::horizontalTotalFor(uint32_t hz, uint16_t frameLines,
                                 float fieldRateHz)
{
    if (hz == 0 || frameLines == 0 || fieldRateHz <= 0.0f)
        return 0;

    // Clocks available per frame, then per line. Done in float because the
    // field rate is measured rather than nominal -- 50.02 Hz is a normal
    // reading and truncating it to 50 would move the answer by a pixel.
    float perFrame = (float)hz / fieldRateHz;
    float perLine = perFrame / (float)frameLines;

    if (perLine < 1.0f)
        return 0;

    // Truncation IS the floor here, and it is what the budget asks for.
    uint32_t horizontalTotal = (uint32_t)perLine;

    // VDS_HSYNC_RST is twelve bits, so the raster total cannot exceed 4096.
    return horizontalTotal > 4096 ? 4096 : (uint16_t)horizontalTotal;
}

DisplayClock::DisplayClock()
    : generator_(0), hzNow_(0), seed_(0), known_(false)
{
}

void DisplayClock::driveWith(Clock::ClockGen &generator) { generator_ = &generator; }

void DisplayClock::hold(uint8_t seed)
{
    seed_ = seed;
    known_ = true;
}

void DisplayClock::adopt()
{
    uint8_t selected = GBS::PLL648_CONTROL_01::read();

    // PCLKIN names no frequency -- hzFor() answers 0 -- so adopting it would
    // send reset() to FallbackHz. What the part is running on is the
    // generator's rate, and the held seed is still the target it steers to.
    if (selected == ExternalPclkIn)
        return;

    seed_ = selected;
    known_ = true;
}

bool DisplayClock::driving() const { return generator_ != 0; }

void DisplayClock::select()
{
    GBS::PLL648_CONTROL_01::write(generator_ == 0 ? seed_ : ExternalPclkIn);
}

uint8_t DisplayClock::seed() const { return seed_; }

bool DisplayClock::known() const { return known_; }

uint32_t DisplayClock::hz() const { return known_ ? hzFor(seed_) : 0; }

uint32_t DisplayClock::reset()
{
    if (generator_ == 0)
        return 0;

    uint32_t target = hz();
    if (target == 0)
        target = FallbackHz;

    // Preloads through an intermediate where the target needs one -- see
    // Clock::ClockRamp::preloadFor for the little that is known about that.
    generator_->setFrequency(target);

    // ClockGen::begin() leaves the output disabled and setFrequency() does not
    // enable it, so handing the display clock over is the source and the pad
    // together.
    generator_->enable();
    GBS::PAD_CKIN_ENZ::write(0);
    hzNow_ = target;
    return target;
}

uint32_t DisplayClock::hzNow() const { return hzNow_; }

void DisplayClock::assumeHz(uint32_t hz) { hzNow_ = hz; }

void DisplayClock::slewTo(uint32_t hz, void (*pump)())
{
    if (generator_ == 0) {
        hzNow_ = hz;
        return;
    }

    // The loop always finishes ON the target: a clock left even 500 Hz out
    // shows as a rolling bar. The stepping policy is Clock::ClockRamp's.
    generator_->slewTo(hzNow_, hz, pump);
    hzNow_ = hz;
}

}  // namespace Tv5725
