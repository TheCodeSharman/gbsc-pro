#ifndef TV5725_MEMORY_BUS_H
#define TV5725_MEMORY_BUS_H

#include <stdint.h>

namespace Tv5725 {

// The SDRAM bus itself: the clock it runs at, and the nanosecond trims that
// compensate this board's traces. Where MemoryMap says what lives at which
// address, this says how the wires under it are driven.
//
// The three delay lines are a property of the memory chip and the board traces
// rather than of the output resolution. Swept one at a time to both ends of
// range the picture is unchanged at every step, and a delay line's working
// region is contiguous, so none of the three tunes anything here. That sweep
// was at 129.6 MHz, a 7.72 ns clock period against 6.17 ns at 162 -- re-take it
// before relying on the margin at a higher clock.
//
// doPostPresetLoadSteps() re-initialises the part at whatever clock this
// selected, so no re-init is needed here.
class MemoryBus {
public:
    // Bring up the SDRAM bus: the clock, the part's mode register, its timing,
    // the address mapping, the arbitration and the board's delay trim.
    static void init();
};

}  // namespace Tv5725

#endif
