#include "SyncType.h"

namespace Tv5725 {

bool SyncType::csync_ = false;
bool SyncType::set_ = false;

bool SyncType::isCsync() { return csync_; }

bool SyncType::isSet() { return set_; }

void SyncType::set(bool csync) { csync_ = csync; }

void SyncType::forget() { set_ = false; }

bool SyncType::probe(bool (*sourceHasOwnVsync)())
{
    csync_ = !sourceHasOwnVsync();
    set_ = true;
    return csync_;
}

bool SyncType::probeOnce(bool (*sourceHasOwnVsync)())
{
    if (!set_)
        return probe(sourceHasOwnVsync);
    return csync_;
}

}  // namespace Tv5725
