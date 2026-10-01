#include "ColourBalance.h"

namespace Tv5725 {

namespace {

// The RGB-to-YUV matrix in thousandths, which is the one the sketch mixed in
// floating point. Thousandths because an ESP8266 pays for a float multiply and
// the offsets are whole counts either way.
const int32_t Thousand = 1000;
const int32_t YfromR = 299, YfromG = 587, YfromB = 114;
const int32_t UfromR = -169, UfromG = -331, UfromB = 500;
const int32_t VfromR = 500, VfromG = -419, VfromB = -81;

// What a neutral balance writes, per colour space. Component video arrives with
// a pedestal, and a component OUTPUT is mixed down -- the luma gain is the only
// one of the four that differs there.
struct Resting {
    int8_t y, u, v;
    uint8_t lumaGain;
};

const Resting Rests[] = {
    {0x00, 0x00, 0x00, 0x80},  // Rgb
    {0x0E, 0x03, 0x04, 0x80},  // Component
    {-2, 0x01, 0x04, 0x64},    // ComponentOutput
};

int8_t clampedOffset(int32_t value)
{
    if (value > 127)
        return 127;
    if (value < -128)
        return -128;
    return (int8_t)value;
}

}  // namespace

ColourBalance::ColourBalance()
    : rest_(Rgb), red_(Neutral), green_(Neutral), blue_(Neutral),
      lumaGain_(Neutral)
{
}

void ColourBalance::restFor(Rest rest) { rest_ = rest; }

uint8_t ColourBalance::stepped(uint8_t from, int16_t steps)
{
    const int32_t wanted = (int32_t)from + steps;
    if (wanted > Limit)
        return Limit;
    if (wanted < 0)
        return 0;
    return (uint8_t)wanted;
}

void ColourBalance::nudgeRed(int16_t steps) { red_ = stepped(red_, steps); }

void ColourBalance::nudgeGreen(int16_t steps) { green_ = stepped(green_, steps); }

void ColourBalance::nudgeBlue(int16_t steps) { blue_ = stepped(blue_, steps); }

void ColourBalance::nudgeLumaGain(int16_t steps)
{
    lumaGain_ = stepped(lumaGain_, steps);
}

uint8_t ColourBalance::red() const { return red_; }

uint8_t ColourBalance::green() const { return green_; }

uint8_t ColourBalance::blue() const { return blue_; }

uint8_t ColourBalance::lumaGain() const { return lumaGain_; }

void ColourBalance::adopt(uint8_t red, uint8_t green, uint8_t blue,
                          uint8_t lumaGain)
{
    red_ = red;
    green_ = green;
    blue_ = blue;
    lumaGain_ = lumaGain;
}

void ColourBalance::reset()
{
    adopt(Neutral, Neutral, Neutral, Neutral);
}

void ColourBalance::apply() const
{
    const Resting &rest = Rests[rest_];
    const int32_t red = (int32_t)red_ - Neutral;
    const int32_t green = (int32_t)green_ - Neutral;
    const int32_t blue = (int32_t)blue_ - Neutral;

    VideoProcessor::VDS_Y_OFST::write((uint8_t)clampedOffset(
        rest.y + (YfromR * red + YfromG * green + YfromB * blue) / Thousand));
    VideoProcessor::VDS_U_OFST::write((uint8_t)clampedOffset(
        rest.u + (UfromR * red + UfromG * green + UfromB * blue) / Thousand));
    VideoProcessor::VDS_V_OFST::write((uint8_t)clampedOffset(
        rest.v + (VfromR * red + VfromG * green + VfromB * blue) / Thousand));

    VideoProcessor::VDS_Y_GAIN::write(
        stepped(rest.lumaGain, (int16_t)((int32_t)lumaGain_ - Neutral)));
}

}  // namespace Tv5725
