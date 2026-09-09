#include "SyncMeasurement.h"

#include <Arduino.h>
#include <stdio.h>

#include "Chip.h"
#include "SyncProcessor.h"
#include "../../gbs_types.h"
#include "Tv5725Log.h"

namespace Tv5725 {

const uint16_t SyncMeasurement::OwnVsyncSettleMs;
const uint16_t SyncMeasurement::OwnVsyncWindowMs;

bool SyncMeasurement::csync_ = false;
bool SyncMeasurement::set_ = false;

bool SyncMeasurement::isCsync() { return csync_; }

bool SyncMeasurement::isSet() { return set_; }

void SyncMeasurement::set(bool csync) { csync_ = csync; }

void SyncMeasurement::forget() { set_ = false; }

bool SyncMeasurement::probe(bool (*hasOwnVsyncNow)())
{
    csync_ = !hasOwnVsyncNow();
    set_ = true;
    return csync_;
}

bool SyncMeasurement::syncType(bool (*hasOwnVsyncNow)())
{
    if (!set_)
        return probe(hasOwnVsyncNow);
    return csync_;
}

bool SyncMeasurement::hasOwnVsync(uint32_t (*nowMs)())
{
    const uint8_t extSyncBackup = SyncProcessor::SP_EXT_SYNC_SEL::read();
    SyncProcessor::SP_EXT_SYNC_SEL::write(0);
    delay(OwnVsyncSettleMs);

    bool active = false;
    const uint32_t start = nowMs();
    while (!active && (nowMs() - start) < OwnVsyncWindowMs) {
        active = GBS::STATUS_SYNC_PROC_VSACT::read() == 1;
        delay(2);
    }
    const uint32_t rose = nowMs() - start;

    if (active) { // confirm it: the bit flickers while the processor settles
        delay(10);
        active = GBS::STATUS_SYNC_PROC_VSACT::read() == 1;
    }

    // How far into the window V arrived, because OwnVsyncWindowMs is sized from
    // that distribution and nothing else on the board reports it.
    char line[64];
    snprintf(line, sizeof(line), "own V sync: %s after %ums",
             active ? "yes" : "no", (unsigned)rose);
    tv5725Log(line);

    SyncProcessor::SP_EXT_SYNC_SEL::write(extSyncBackup);
    return active;
}

}  // namespace Tv5725
