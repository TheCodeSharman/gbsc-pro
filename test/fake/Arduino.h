// The whole of Arduino.h that src/tv5725/ actually needs.
//
// Geometry.h includes <Arduino.h> for `boolean` alone, and that single include
// is half of what kept the engine out of the host build; test/fake/Wire.h is the
// other half.
//
// Deliberately NOT a general Arduino shim. A file under src/tv5725/ wanting
// millis(), delay() or Serial is a design signal -- the geometry is arithmetic
// over injected inputs -- and the answer is to move the dependency out rather
// than widen this header.
//
// delayMicroseconds() and delay() are admitted because they are HARDWARE
// settling times, not dependencies to move out: PLLAD_LAT has to be held low
// before its rising edge, and the Si5351 needs its preload step to take. Faking
// them is what lets Adc's write-before-latch ordering and Clock::ClockGen be
// tested at all.

#ifndef FAKE_ARDUINO_H_
#define FAKE_ARDUINO_H_

#include <stdint.h>

typedef bool boolean;

inline void delayMicroseconds(unsigned int) {}

// Admitted on the same terms: Clock::ClockGen waits out the Si5351's preload
// step, which is the part's settling time rather than a dependency to move out.
inline void delay(unsigned long) {}

#endif  // FAKE_ARDUINO_H_
