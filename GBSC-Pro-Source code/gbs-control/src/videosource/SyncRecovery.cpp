#include "SyncRecovery.h"

const uint32_t SyncRecovery::FirstActMs;
const uint32_t SyncRecovery::ActIntervalMs;

const char *SyncRecovery::nameOf(Act act)
{
    switch (act) {
    case Reconfigure:   return "reconfigure";
    case ResetBlocks:   return "reset the blocks";
    case MoveInput:     return "move the input";
    case None:          break;
    }
    return "none";
}

SyncRecovery::Act SyncRecovery::actAt(uint32_t unacquiredMs)
{
    if (unacquiredMs < FirstActMs)
        return None;

    switch (((unacquiredMs - FirstActMs) / ActIntervalMs) % 3) {
    case 0:     return Reconfigure;
    case 1:     return ResetBlocks;
    default:    return MoveInput;
    }
}
