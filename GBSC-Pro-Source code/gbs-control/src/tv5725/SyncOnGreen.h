#ifndef TV5725_SYNC_ON_GREEN_H_
#define TV5725_SYNC_ON_GREEN_H_

#include "Tv5725.h"

namespace Tv5725 {

// The slicer that recovers sync from the green channel, and the one owner of
// its level.
//
// **THE LEVEL IS HELD, NOT READ BACK.** Every ratchet that walks it takes the
// level it last set as its starting point; deriving the next one from the
// register asks the chip what it was told, and a read taken while the source is
// unlocked walks it somewhere nobody chose. docs/retiring-the-sync-watcher.md
class SyncOnGreen {
public:
    typedef UReg<0x05, 0x02, 1, 5> ADC_SOGCTRL;                       // When = 1, ADC enable SOG mode SOG control signal ADC

    // The widest slice the field carries.
    static const uint8_t LevelMax = 31;

    // The level to run at, without touching the slicer. Several sites choose
    // one for a source the ADC has not been brought up for yet, and the
    // bring-up applies it.
    //
    // A level past the field is refused rather than truncated: masking put 32
    // in as 0, which is the slicer fully open and the one value no ratchet can
    // climb back out of.
    static void choose(uint8_t level);

    // Choose it and put it in force.
    static void apply(uint8_t level);

    // Put the chosen level in force, for a caller that did not choose it.
    static void apply();

    static uint8_t level();

private:
    static uint8_t level_;
};

}  // namespace Tv5725

#endif  // TV5725_SYNC_ON_GREEN_H_
