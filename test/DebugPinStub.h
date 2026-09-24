#ifndef TEST_DEBUG_PIN_STUB_H_
#define TEST_DEBUG_PIN_STUB_H_

// The ESP's half of Tv5725::TestBusRateMeasurement, as a host stub: a tick count
// standing in for the two edge ISRs. Each binary defines debugPinPulseTicks()
// from ticksForHz(), so a suite that drives the rate and one that pins it to a
// constant share the arithmetic.

#include <stdint.h>

// Arbitrary, as long as the ticks are derived from it.
static const uint32_t StubTicksPerSecond = 160000000u;

// Not inline: TestBusRateMeasurement.cpp is a separate translation unit, so this
// needs a real definition to link against. One include per binary, so one.
uint32_t debugPinTicksPerSecond() { return StubTicksPerSecond; }

// The binary's own, from ticksForHz() below.
uint32_t debugPinPulseTicks();

// The same pulse as two edge timestamps, which is the seam Tv5725::FrameSync
// measures phase from. Derived here so the binaries that only care about a
// rate keep supplying a period and nothing else; a suite that drives the PHASE
// defines this itself and does not include this header.
//
// A start of 1 rather than 0: the platform reads a cycle counter, and 0 is what
// no pulse looks like there.
bool debugPinPulseEdges(uint32_t *start, uint32_t *stop)
{
    uint32_t ticks = debugPinPulseTicks();
    if (ticks == 0)
        return false;
    *start = 1;
    *stop = 1 + ticks;
    return true;
}

// The failure diagnostic, which has nothing to report on the host.
void debugPinProbe() {}

// One pulse is one field. 0 ticks is what no pulse looks like, which is how a
// source with no lock reads.
inline uint32_t ticksForHz(float hz)
{
    return hz > 0.0f ? (uint32_t)((float)StubTicksPerSecond / hz) : 0;
}

#endif  // TEST_DEBUG_PIN_STUB_H_
