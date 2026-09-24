#ifndef TV5725_DEBUG_PIN_H_
#define TV5725_DEBUG_PIN_H_

// The ESP's half of everything timed off DEBUG_IN_PIN, defined by the sketch
// the way tv5725Log() is. The timestamps come from two ICACHE_RAM_ATTR edge
// ISRs reading the CPU's cycle counter, and the wait around them turns WiFi's
// sleep mode off and feeds the watchdog -- none of which this layer can reach
// or host-compile.
//
// WHAT the pin carries is the caller's: Tv5725::TestBus selects it, and nothing
// here depends on the choice.

#include <stdint.h>

// One period: the cycle counts of two consecutive rising edges. False where no
// second edge arrived inside the timeout, which is what an unlocked source and
// a dead measurement path both look like.
bool debugPinPulseEdges(uint32_t *start, uint32_t *stop);

// The same period as a length, which is all most callers want. 0 ticks is what
// no pulse looks like, and is also the no-lock answer.
uint32_t debugPinPulseTicks();

// What one tick is worth, in ticks per second.
uint32_t debugPinTicksPerSecond();

// Poll the pin across the selectors this firmware uses and report whether it
// moves at all. Only meaningful after a sample has already failed, and silent
// in a build without diagnostics: every register that gates the pin can read
// correct while no edge arrives, and configuration cannot say which side is at
// fault.
void debugPinProbe();

#endif  // TV5725_DEBUG_PIN_H_
