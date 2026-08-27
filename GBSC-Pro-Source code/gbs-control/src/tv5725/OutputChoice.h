#ifndef TV5725_OUTPUT_CHOICE_H_
#define TV5725_OUTPUT_CHOICE_H_

// The output resolution the user asked for, held until the source's field rate
// has been measured.
//
// A preference names a resolution and nothing else. matchPresetSource swaps
// within two pairs -- 960p against 1024p, 480p against 576p -- to whichever
// member matches the rate the source runs at, and that rate is not known until
// the engine has measured the new source. So the choice travels and resolve()
// happens on the far side of the measurement.
//
// See docs/firmware-geometry-engine.md

#include "OutputMode.h"

namespace Tv5725 {

class OutputChoice {
public:
    OutputChoice();

    // One resolution, matching nothing: what a direct command asks for.
    explicit OutputChoice(PresetPreference preference);

    // ntscDownshiftAllowed gates 1024p -> 960p alone, and is upstream's
    // asymmetry rather than a design: standard 8 and a scaling-RGBHV source are
    // excluded there while the PAL direction is unguarded.
    //
    // shownAtSixtyHz is PalForce60, which puts a 50 Hz source on a 60 Hz
    // output. The pairs are a property of the output, so they key to 60 whatever
    // was measured.
    OutputChoice(PresetPreference preference, bool matchSource,
                 bool ntscDownshiftAllowed, bool shownAtSixtyHz);

    // The mode this choice comes to at that rate, or 0 where it names no
    // resolution: bypass, a custom preset, or nothing chosen.
    const OutputMode *resolve(float measuredFieldRateHz) const;

private:
    PresetPreference preference_;
    bool matchSource_;
    bool ntscDownshiftAllowed_;
    bool shownAtSixtyHz_;
};

}  // namespace Tv5725

#endif  // TV5725_OUTPUT_CHOICE_H_
