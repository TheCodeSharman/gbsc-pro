// The whole of Arduino.h that src/tv5725/ actually needs.
//
// VideoPath.h includes <Arduino.h> for `boolean` alone, and that single include
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

// Storage qualifier and its reader, not a dependency to move out: the OLED
// menu's images and strings are declared PROGMEM at file scope.
#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))

inline void delayMicroseconds(unsigned int) {}

// Admitted on the same terms: Clock::ClockGen waits out the Si5351's preload
// step, which is the part's settling time rather than a dependency to move out.
//
// The total is ACCUMULATED because a probe's cost is part of its contract and
// an injected clock cannot see it: a blind settle in front of a poll spends
// real milliseconds while millis() in the test is driven by the poll alone.
inline unsigned long &settledMs()
{
    static unsigned long ms = 0;
    return ms;
}

inline void delay(unsigned long ms) { settledMs() += ms; }

// F() and PSTR() are placement directives rather than dependencies: on the ESP
// they keep a literal in flash, where .rodata would otherwise cost RAM this
// fork has little of. A firmware file that drops them to stay host-compilable
// pays that cost silently, so the seam supplies them instead.
#define F(string) (string)
#define PSTR(string) (string)

#endif  // FAKE_ARDUINO_H_
