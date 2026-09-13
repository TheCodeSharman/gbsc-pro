#ifndef TV5725_OUTPUT_CHOICE_H_
#define TV5725_OUTPUT_CHOICE_H_

// The output resolution the user asked for.
//
// A preference names a resolution and NOTHING ELSE. It used to be swapped
// within two pairs on the source's field rate, which is the rate standing in
// for a line count and only holds for broadcast sources -- the same 256-line
// raster at 50 Hz and 60 Hz came out at two different output resolutions.
// docs/firmware-geometry-engine.md

#include "OutputMode.h"

namespace Tv5725 {

class OutputChoice {
public:
    OutputChoice();

    explicit OutputChoice(PresetPreference preference);

    // The mode this choice names, or 0 where it names no resolution: a custom
    // preset, or nothing chosen. Never pass-through -- that is not a resolution
    // and this cannot express it.
    const OutputMode *resolve() const;

    // The resolution a load runs at when the preference names none. Every other
    // site that has to substitute one uses this.
    static const PresetPreference ScaledDefault = Output1080P;

private:
    PresetPreference preference_;
};

}  // namespace Tv5725

#endif  // TV5725_OUTPUT_CHOICE_H_
