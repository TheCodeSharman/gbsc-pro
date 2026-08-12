#ifndef CLOCK_CLOCK_GEN_H_
#define CLOCK_CLOCK_GEN_H_

// The Si5351 as this board uses it: detection, bring-up, and every write to it.
// The arithmetic half is Clock::ClockRamp.

#include <stdint.h>

class Si5351mcu;

namespace Clock {

class ClockGen {
public:
    explicit ClockGen(Si5351mcu &device);

    // Crystal load capacitance, written to register 183 during bring-up. 0xD2 is
    // upstream's and unexplained; its top bits are reserved and the datasheet
    // asks for them to be preserved, which a blind write does not do.
    static const uint8_t CrystalLoad = 0xD2;

    // The reference crystal, and the drive strength the board asks for.
    static const uint32_t ReferenceHz = 25000000;
    static const uint8_t OutputPower = 2;  // SIOUT_6mA

    // Two checks: the address must ACK, and register 0's SYS_INIT bit must be
    // clear. A part still initialising answers on the bus but is not ready, and
    // driving it then leaves the display clock somewhere undefined.
    bool detect();

    bool present() const;

    // The bring-up sequence, in the order the board needs it: reference, crystal
    // load, drive strength, an initial frequency, then OFF. It is left disabled
    // because the TV5725 has to be told to take an external clock first --
    // enabling before that puts a clock on a pin nothing is listening to.
    void begin(uint32_t initialHz);

    // Set the output, preloading through an intermediate where the target needs
    // one. See ClockRamp::preloadFor for the little that is known about that.
    void setFrequency(uint32_t hz);

    // Walk from `fromHz` to `toHz` in ClockRamp's steps, landing exactly. `pump`
    // is called between writes: a slew of 750 steps is 750 I2C transactions,
    // long enough that WiFi and the watchdog need servicing or the frequency
    // change becomes a reboot.
    void slewTo(uint32_t fromHz, uint32_t toHz, void (*pump)());

    void enable();
    void disable();

private:
    Si5351mcu &device_;
    bool present_;
};

}  // namespace Clock

#endif  // CLOCK_CLOCK_GEN_H_
