#ifndef TEST_SI5351_STUBS_H_
#define TEST_SI5351_STUBS_H_

// Tv5725::DisplayClock steers a Clock::ClockGen, which drives one of these, so
// any host binary linking DisplayClock.cpp needs the part to resolve.
// si5351mcu.cpp is not linked: it is the half the host cannot have.
//
// Header-only and defining its symbols, the way BenchGeometry.h does -- every
// host test is a single-translation-unit binary. A suite wanting to SEE what
// reached the part defines these itself and does not include this.

#include "../GBSC-Pro-Source code/gbs-control/src/si5351mcu.h"

void Si5351mcu::init(uint32_t) {}
void Si5351mcu::setFreq(uint8_t, uint32_t) {}
void Si5351mcu::setPower(uint8_t, uint8_t) {}
void Si5351mcu::enable(uint8_t) {}
void Si5351mcu::disable(uint8_t) {}

#endif  // TEST_SI5351_STUBS_H_
