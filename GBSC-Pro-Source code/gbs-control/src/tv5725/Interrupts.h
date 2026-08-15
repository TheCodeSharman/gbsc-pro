#ifndef TV5725_INTERRUPTS_H
#define TV5725_INTERRUPTS_H

namespace Tv5725 {

// The interrupt block: which conditions may report, and the per-source resets
// that clear them.
//
// INT_ENABLE0..7 are all 1 -- every source unmasked -- and the INT_RST_* bits
// are all 0, which is the resting state rather than a held reset. They are
// PULSES: the sketch raises one and drops it again to acknowledge a latched
// condition, so 0 here is "nothing currently being cleared".
//
// s0_58[0], [1] and [4] are absent on purpose: the three
// resetInterrupt*Bit() functions write 1 then 0 to acknowledge a latched
// condition, and a static write here would land mid-acknowledgement. Those are
// also the bits that once had two names and so two owners, with every by-name
// check passing -- ask in bits, not in names.
class Interrupts {
public:
    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_INTERRUPTS_H
