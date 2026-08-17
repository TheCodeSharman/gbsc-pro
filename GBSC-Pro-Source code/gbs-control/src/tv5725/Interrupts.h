#ifndef TV5725_INTERRUPTS_H
#define TV5725_INTERRUPTS_H

#include "Tv5725.h"

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
    typedef UReg<0x00, 0x58, 0, 1> INT_CONTROL_RST_SOGBAD;            // Interrupt bit0 reset control When = 1, interrupt bit0
                                                                      // status will be reset to zero

    typedef UReg<0x00, 0x58, 1, 1> INT_CONTROL_RST_SOGSWITCH;         // Interrupt bit1 reset control When = 1, interrupt bit1
                                                                      // status will be reset to zero

    typedef UReg<0x00, 0x58, 2, 1> INT_RST_2;                         // Interrupt bit2 reset control When = 1, interrupt bit2
                                                                      // status will be reset to zero

    typedef UReg<0x00, 0x58, 3, 1> INT_RST_3;                         // Interrupt bit3 reset control When = 1, interrupt bit3
                                                                      // status will be reset to zero

    typedef UReg<0x00, 0x58, 4, 1> INT_CONTROL_RST_NOHSYNC;           // Interrupt bit4 reset control When = 1, interrupt bit4
                                                                      // status will be reset to zero

    typedef UReg<0x00, 0x58, 5, 1> INT_RST_5;                         // Interrupt bit5 reset control When = 1, interrupt bit5
                                                                      // status will be reset to zero

    typedef UReg<0x00, 0x58, 6, 1> INT_RST_6;                         // Interrupt bit6 reset control When = 1, interrupt bit6
                                                                      // status will be reset to zero

    typedef UReg<0x00, 0x58, 7, 1> INT_RST_7;                         // Interrupt bit7 reset control When = 1, interrupt bit7
                                                                      // status will be reset to zero

    typedef UReg<0x00, 0x59, 0, 1> INT_ENABLE0;                       // Interrupt bit0 enable When = 1, enable interrupt bit0
                                                                      // generator

    typedef UReg<0x00, 0x59, 1, 1> INT_ENABLE1;                       // Interrupt bit1 enable When = 1, enable interrupt bit1
                                                                      // generator

    typedef UReg<0x00, 0x59, 2, 1> INT_ENABLE2;                       // Interrupt bit2 enable When = 1, enable interrupt bit2
                                                                      // generator

    typedef UReg<0x00, 0x59, 3, 1> INT_ENABLE3;                       // Interrupt bit3 enable When = 1, enable interrupt bit3
                                                                      // generator

    typedef UReg<0x00, 0x59, 4, 1> INT_ENABLE4;                       // Interrupt bit4 enable When = 1, enable interrupt bit4
                                                                      // generator

    typedef UReg<0x00, 0x59, 5, 1> INT_ENABLE5;                       // Interrupt bit5 enable When = 1, enable interrupt bit5
                                                                      // generator

    typedef UReg<0x00, 0x59, 6, 1> INT_ENABLE6;                       // Interrupt bit6 enable When = 1, enable interrupt bit6
                                                                      // generator

    typedef UReg<0x00, 0x59, 7, 1> INT_ENABLE7;                       // Interrupt bit7 enable When = 1, enable interrupt bit7
                                                                      // generator GISTERS

    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_INTERRUPTS_H
