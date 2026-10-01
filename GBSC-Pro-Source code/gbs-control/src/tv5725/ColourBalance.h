#ifndef TV5725_COLOUR_BALANCE_H
#define TV5725_COLOUR_BALANCE_H

#include "VideoProcessor.h"

namespace Tv5725 {

// The user's picture colour: a red, green and blue balance and a luma gain. The
// chip takes the balance as three YUV offsets, so it is held in the basis the
// menu shows and converted on the way out. Owns VDS_Y_OFST, VDS_U_OFST,
// VDS_V_OFST and VDS_Y_GAIN. docs/osd-menu.md
class ColourBalance {
public:
    enum { Neutral = 128, Limit = 255 };

    // Where a neutral balance sits, which the colour space decides: component
    // video arrives with a pedestal and RGB does not. A change of rest leaves
    // the balance where the user put it.
    enum Rest { Rgb, Component, ComponentOutput };

    ColourBalance();

    void restFor(Rest rest);

    // Each takes a signed number of steps, as a remote's hold ramp supplies it.
    void nudgeRed(int16_t steps);
    void nudgeGreen(int16_t steps);
    void nudgeBlue(int16_t steps);
    void nudgeLumaGain(int16_t steps);

    uint8_t red() const;
    uint8_t green() const;
    uint8_t blue() const;
    uint8_t lumaGain() const;

    void adopt(uint8_t red, uint8_t green, uint8_t blue, uint8_t lumaGain);
    void reset();

    void apply() const;

private:
    static uint8_t stepped(uint8_t from, int16_t steps);

    Rest rest_;

    uint8_t red_;
    uint8_t green_;
    uint8_t blue_;
    uint8_t lumaGain_;
};

}  // namespace Tv5725

#endif  // TV5725_COLOUR_BALANCE_H
