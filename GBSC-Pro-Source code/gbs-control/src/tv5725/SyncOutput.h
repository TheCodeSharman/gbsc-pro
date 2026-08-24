#ifndef TV5725_SYNC_OUTPUT_H_
#define TV5725_SYNC_OUTPUT_H_

// HSOUT/VSOUT across a mode change.
//
// The encoder samples the analog output and does not always notice the timing
// under it moved: it carries on transmitting the mode it locked to before, and
// the display reports that older rate and shows nothing. Taking sync away is
// what makes it re-acquire.
//
// **HELD AS STATE, NOT TOGGLED.** The output is blanked exactly while a mode
// change is outstanding, so a blank cannot outlive the change that caused it --
// which is the failure a drop-and-restore pair risks, and the reason the pair
// had to sit in one block. The blank starts when the change does, so the
// measuring and solving happen behind it and absorb the encoder's relock time
// rather than being followed by it.
//
// docs/investigations/encoder-stale-timing.md

#include <stdint.h>

#include "Chip.h"


namespace Tv5725 {

class SyncOutput {
public:
    // What the encoder needs with no sync before it will look again. Only a
    // change that finishes faster than this waits at all.
    static const uint16_t MinimumBlankMs = 250;

    SyncOutput();

    // `changing` is whether the engine still has a mode change outstanding.
    // Returns true when the pad state was written, so a caller can see that a
    // pass touched the bus rather than every pass writing it.
    bool poll(bool changing, unsigned long nowMs);

    // Blank straight away, before the engine has been told anything moved. A
    // preset command writes output registers on its way to telling it, and the
    // picture glitches if the blank waits for that. poll() still decides when
    // it ends, so this cannot leave the output dark.
    void blankNow(unsigned long nowMs);

    bool blanked() const;

private:
    void write(bool blanked, unsigned long nowMs);

    bool blanked_;
    unsigned long since_;
};

}  // namespace Tv5725

#endif  // TV5725_SYNC_OUTPUT_H_
