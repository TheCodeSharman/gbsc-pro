#include "FramingSaveTimer.h"

namespace Tv5725 {

void FramingSaveTimer::inhibit(bool on)
{
    // Lifting it adopts whatever is live as already saved, so the framing the
    // inhibit was protecting against never reaches flash. Merely restarting the
    // quiet period would postpone that write, not prevent it.
    if (inhibited_ && !on)
        saved_ = seen_;
    inhibited_ = on;
}

bool FramingSaveTimer::inhibited() const { return inhibited_; }

void FramingSaveTimer::markSaved(uint16_t revision) { saved_ = revision; }

bool FramingSaveTimer::due(uint16_t revision, uint32_t now, uint32_t quietMs)
{
    if (revision != seen_) {
        seen_ = revision;
        movedAt_ = now;
        return false;
    }

    // Checked after the move is recorded, so seen_ tracks the live framing and
    // inhibit(false) can adopt it.
    if (inhibited_)
        return false;

    if (revision == saved_ || movedAt_ == 0)
        return false;

    return now - movedAt_ >= quietMs;
}

}  // namespace Tv5725
