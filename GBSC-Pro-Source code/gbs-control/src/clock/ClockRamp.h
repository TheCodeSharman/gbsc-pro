#ifndef CLOCK_CLOCK_RAMP_H_
#define CLOCK_CLOCK_RAMP_H_

// How the external clock generator's output is allowed to MOVE, as distinct from
// what it is set to.
//
// The pure arithmetic half, so it host-compiles and is tested. The device half
// -- I2C probe, crystal load, PAD_CKIN_ENZ, the FrameSync coupling -- is
// Clock::ClockGen and the sketch.

#include <stdint.h>

namespace Clock {

class ClockRamp {
public:
    // Upstream's, both carried over unexplained.
    static const uint32_t StepHz = 1000;
    static const uint32_t RampLimitHz = 750000;

    // Whether a move from `fromHz` to `toHz` is walked in steps or jumped.
    //
    // Small moves are ramped so the display is not asked to follow a
    // discontinuity; a preset change is a jump because stepping 27 MHz at 1 kHz
    // would be 27,000 I2C transactions.
    static bool ramps(uint32_t fromHz, uint32_t toHz);

    // The next frequency to write on the way to `targetHz`.
    //
    // Returns targetHz once it is within one step, so a caller loops until the
    // answer IS the target and writes each value:
    //
    //     while (current != target) { current = advance(current, target);
    //                                 device.setFreq(current); }
    //
    // It must land EXACTLY: a clock left even 500 Hz out is a bar rolling
    // through the picture every half minute or so.
    static uint32_t advance(uint32_t currentHz, uint32_t targetHz);

    // An intermediate frequency to set before `targetHz`, or 0 for none.
    //
    // 87 MHz before 108, 48.5 before 40.5, each with a 1 ms settle. Unexplained
    // and kept verbatim: it is in every working state measured on this board,
    // and 129.6 MHz reaches sharp with no preload at all, so it looks specific
    // to those two ratios rather than to high frequencies.
    static uint32_t preloadFor(uint32_t targetHz);
};

}  // namespace Clock

#endif  // CLOCK_CLOCK_RAMP_H_
