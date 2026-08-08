#ifndef INPUT_HOLD_RAMP_H_
#define INPUT_HOLD_RAMP_H_

// Hold a key to go faster; tap it to be exact.
//
// Coarse-versus-fine without an extra key, a mode or anything on screen. Four
// properties do the work: the first press of a run is always one unit, so a tap
// means one pixel; nothing ramps until the key is held past a dead time, so a
// lingering press does not take off; the ramp doubles through discrete rates,
// so it can be stopped somewhere predictable; and it caps, because a ramp with
// no ceiling crosses the whole line in one press.
//
// Pure logic with the clock passed in, so all four are tested on the host.

#include <stdint.h>

class HoldRamp {
public:
    // The remote does not resend the key code while a key is held, it sends a
    // repeat marker; IRremoteESP8266 surfaces it as this. Handled here so the
    // IR handler does not have to know, and so the run survives it.
    static const uint32_t RepeatCode = 0xFFFFFFFF;

    // Longer than the remote's ~110 ms repeat interval, so a held key stays one
    // run, and short enough that two deliberate presses are two runs.
    static const unsigned long RunGapMs = 300;

    // Repeat frames to sit through before accelerating: ~4 x 110 ms is a little
    // under half a second, which is about where a press stops feeling like a tap.
    static const uint8_t DeadRepeats = 4;

    static const int16_t MaxMultiplier = 16;

    HoldRamp();

    // How many units this press is worth. Call once per accepted key event.
    int16_t multiplierFor(uint32_t key, unsigned long nowMs);

    // What key a frame really means. The IR handler dispatches on the key code
    // and a repeat frame matches no case, so a held key would be dropped before
    // it ever reached the ramp. Call this before the switch; it changes nothing.
    uint32_t resolve(uint32_t key) const;

    // The key has been let go: the next press starts a fresh, exact run.
    void release();

private:
    // Repeat frames at each rate before doubling to the next.
    static const uint16_t RepeatsPerRate = 4;

    // 2, 4, 8, 16 -- discrete rather than continuous, so a held key can still be
    // stopped somewhere predictable.
    static int16_t rateFor(uint16_t repeatsPastDeadTime);

    uint32_t lastKey_;
    unsigned long lastAt_;
    uint16_t run_;
    bool held_;
};

#endif  // INPUT_HOLD_RAMP_H_
