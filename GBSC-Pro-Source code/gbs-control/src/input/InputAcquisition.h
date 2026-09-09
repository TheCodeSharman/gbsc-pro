#ifndef INPUT_ACQUISITION_H_
#define INPUT_ACQUISITION_H_

// Deciding where video comes from, and keeping it coming.
//
// It sits above Tv5725:: and owns the tick: the escalation, the input policy
// and the no-signal report are its, and the scaler's share is the engine's.
// docs/input-acquisition.md

#include <stdint.h>

#include "../tv5725/VideoPath.h"

class InputAcquisition {
public:
    explicit InputAcquisition(Tv5725::VideoPath &videoPath);

    // One tick for the whole acquisition path, taken from loop(). True on the
    // pass that completes a mode change.
    bool poll(uint32_t nowMs);

private:
    Tv5725::VideoPath &videoPath_;
};

#endif  // INPUT_ACQUISITION_H_
