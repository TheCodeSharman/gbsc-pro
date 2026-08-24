#include "SyncOutput.h"


namespace Tv5725 {

const uint16_t SyncOutput::MinimumBlankMs;

SyncOutput::SyncOutput() : blanked_(false), since_(0) {}

bool SyncOutput::blanked() const { return blanked_; }

void SyncOutput::write(bool blanked, unsigned long nowMs)
{
    blanked_ = blanked;
    since_ = nowMs;
    Chip::PAD_SYNC_OUT_ENZ::write(blanked ? 1 : 0);
}

void SyncOutput::blankNow(unsigned long nowMs)
{
    write(true, nowMs);
}

bool SyncOutput::poll(bool changing, unsigned long nowMs)
{
    if (changing) {
        // Asserted every pass rather than once, because the preset and reset
        // paths write this pad too and a blank they cleared half way through a
        // change would show the encoder the timing it is being moved off.
        // Re-entering also restarts the clock: what it has to lock to is the
        // settled timing, not the one being left.
        write(true, nowMs);
        return true;
    }

    if (blanked_ && (nowMs - since_) >= MinimumBlankMs) {
        write(false, nowMs);
        return true;
    }
    return false;
}

}  // namespace Tv5725
