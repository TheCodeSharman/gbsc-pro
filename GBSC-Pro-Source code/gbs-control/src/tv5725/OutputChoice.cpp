#include "OutputChoice.h"

namespace Tv5725 {

const PresetPreference OutputChoice::ScaledDefault;

OutputChoice::OutputChoice() : preference_(OutputCustomized) {}

OutputChoice::OutputChoice(PresetPreference preference)
    : preference_(preference) {}

const OutputMode *OutputChoice::resolve() const
{
    return OutputMode::forPreference(preference_);
}

PresetPreference OutputChoice::scaledOr(PresetPreference wanted)
{
    // Bypass resolves to a mode rather than to nothing, so naming a mode is not
    // the test -- naming a scaled one is.
    const OutputMode *mode = OutputMode::forPreference(wanted);
    if (mode == 0 || mode->isBypass())
        return ScaledDefault;
    return wanted;
}

}  // namespace Tv5725
