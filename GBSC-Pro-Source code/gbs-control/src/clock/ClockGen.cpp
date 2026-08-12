#include "ClockGen.h"

#include <Arduino.h>
#include <Wire.h>

#include "../si5351mcu.h"
#include "ClockRamp.h"

namespace Clock {

const uint8_t ClockGen::CrystalLoad;
const uint32_t ClockGen::ReferenceHz;
const uint8_t ClockGen::OutputPower;

ClockGen::ClockGen(Si5351mcu &device) : device_(device), present_(false) {}

bool ClockGen::present() const { return present_; }

bool ClockGen::detect()
{
    present_ = false;

    // Does anything ACK at the address?
    Wire.beginTransmission(SIADDR);
    if (Wire.endTransmission() != 0)
        return false;

    // Register 0, top bit: SYS_INIT. Set means the part is still bringing itself
    // up, and it answers on the bus while in that state -- so an address probe
    // alone is not detection.
    Wire.beginTransmission(SIADDR);
    Wire.write(0);
    Wire.endTransmission();

    if (Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false) != 1)
        return false;

    uint8_t status = Wire.read();
    if ((status & 0x80) != 0)
        return false;

    present_ = true;
    return true;
}

void ClockGen::begin(uint32_t initialHz)
{
    device_.init(ReferenceHz);

    // Crystal load capacitance. Raw Wire rather than the driver, because
    // si5351mcu exposes no accessor for register 183.
    Wire.beginTransmission(SIADDR);
    Wire.write(183);
    Wire.write(CrystalLoad);
    Wire.endTransmission();

    device_.setPower(0, OutputPower);
    device_.setFreq(0, initialHz);

    // Left OFF on purpose: the TV5725 has to be switched to an external display
    // clock before this output means anything, and enabling first puts a clock on
    // a pin nothing is listening to.
    device_.disable(0);
}

void ClockGen::setFrequency(uint32_t hz)
{
    uint32_t preload = ClockRamp::preloadFor(hz);
    if (preload != 0) {
        device_.setFreq(0, preload);
        delay(1);
    }
    device_.setFreq(0, hz);
}

void ClockGen::slewTo(uint32_t fromHz, uint32_t toHz, void (*pump)())
{
    uint32_t current = fromHz;
    while (current != toHz) {
        current = ClockRamp::advance(current, toHz);
        device_.setFreq(0, current);
        if (pump != 0)
            pump();
    }
}

void ClockGen::enable() { device_.enable(0); }

void ClockGen::disable() { device_.disable(0); }

}  // namespace Clock
