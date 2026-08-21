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
// delayMicroseconds() is admitted because it is a HARDWARE pulse width, not a
// dependency to move out: PLLAD_LAT has to be held low before its rising edge.
// Faking it is what lets Adc's write-before-latch ordering be tested at all.

#ifndef FAKE_ARDUINO_H_
#define FAKE_ARDUINO_H_

#include <stdint.h>

typedef bool boolean;

inline void delayMicroseconds(unsigned int) {}

#endif  // FAKE_ARDUINO_H_
