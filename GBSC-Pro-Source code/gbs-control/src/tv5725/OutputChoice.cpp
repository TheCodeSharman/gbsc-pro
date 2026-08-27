#include "OutputChoice.h"

namespace Tv5725 {

OutputChoice::OutputChoice()
    : preference_(OutputCustomized), matchSource_(false),
      ntscDownshiftAllowed_(false), shownAtSixtyHz_(false) {}

OutputChoice::OutputChoice(PresetPreference preference)
    : preference_(preference), matchSource_(false),
      ntscDownshiftAllowed_(false), shownAtSixtyHz_(false) {}

OutputChoice::OutputChoice(PresetPreference preference, bool matchSource,
                           bool ntscDownshiftAllowed, bool shownAtSixtyHz)
    : preference_(preference), matchSource_(matchSource),
      ntscDownshiftAllowed_(ntscDownshiftAllowed),
      shownAtSixtyHz_(shownAtSixtyHz) {}

const OutputMode *OutputChoice::resolve(float measuredFieldRateHz) const
{
    if (!matchSource_)
        return OutputMode::forPreference(preference_);

    const bool pal = !shownAtSixtyHz_
                     && measuredFieldRateHz < (float)OutputMode::PalNtscSplitHz;

    PresetPreference preference = preference_;
    if (pal && preference == Output960P)
        preference = Output1024P;
    else if (!pal && preference == Output1024P && ntscDownshiftAllowed_)
        preference = Output960P;

    if (pal && preference == Output480P)
        preference = Output576P;
    else if (!pal && preference == Output576P)
        preference = Output480P;

    return OutputMode::forPreference(preference);
}

}  // namespace Tv5725
