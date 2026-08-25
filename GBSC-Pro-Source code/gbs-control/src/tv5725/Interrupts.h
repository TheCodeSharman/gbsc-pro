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
// INT_STATUS_, the byte the block latches into. RD-5725-1.1 documents it as one
// block at s0_0F rather than field by field.
    typedef UReg<0x00, 0x0F, 0, 1> STATUS_INT_SOG_BAD;                // Part of INT_STATUS_, which RD-5725-1.1 documents as one

    typedef UReg<0x00, 0x0F, 1, 1> STATUS_INT_SOG_SW;                 // Part of INT_STATUS_, which RD-5725-1.1 documents as one

    typedef UReg<0x00, 0x0F, 2, 1> STATUS_INT_SOG_OK;                 // When =1, means input SOG source is stable [datasheet:

    typedef UReg<0x00, 0x0F, 3, 1> STATUS_INT_INP_SW;                 // When =1, means input source switch the mode [datasheet:

    typedef UReg<0x00, 0x0F, 4, 1> STATUS_INT_INP_NO_SYNC;            // Part of INT_STATUS_, which RD-5725-1.1 documents as one

    typedef UReg<0x00, 0x0F, 5, 1> STATUS_INT_INP_HSYNC;              // When =1, means input H-sync status is changed between

    typedef UReg<0x00, 0x0F, 6, 1> STATUS_INT_INP_VSYNC;              // When =1, means input V-sync status is changed between

    typedef UReg<0x00, 0x0F, 7, 1> STATUS_INT_INP_CSYNC;              // When =1, means input H-sync status is changed between

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

    // Whether the source has disturbed since this was last asked, and claim it.
    //
    // Bit 1, SOG switch. Measured across a source mode change sampled from
    // loop() at 30 ms, bits 0 and 1 fire together at ~0.9-1.1 s in BOTH
    // directions, so bit 1 alone is signal enough; bit 3, the one the datasheet
    // calls "input source switch the mode", fired on one direction only and
    // 3.4 s in.
    //
    // **Bit 0 is deliberately not read.** Reading is claiming -- the bits are
    // latched, so one left unacknowledged reports the same disturbance on every
    // later poll -- and bit 0 already has an owner on every sync path, which
    // counts consecutive sets to decide a separate-sync source should move to
    // csync. Two claimants and the count never reaches its threshold.
    static bool takeSourceDisturbed();

    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_INTERRUPTS_H
