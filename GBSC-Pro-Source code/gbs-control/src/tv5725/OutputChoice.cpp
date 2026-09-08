#include "OutputChoice.h"

namespace Tv5725 {

OutputChoice::OutputChoice() : preference_(OutputCustomized) {}

OutputChoice::OutputChoice(PresetPreference preference)
    : preference_(preference) {}

const OutputMode *OutputChoice::resolve() const
{
    return OutputMode::forPreference(preference_);
}

}  // namespace Tv5725
