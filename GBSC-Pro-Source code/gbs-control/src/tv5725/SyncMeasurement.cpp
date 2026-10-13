#include "SyncMeasurement.h"

#include <Arduino.h>
#include <stdio.h>

#include "Adc.h"
#include "Chip.h"
#include "Interrupts.h"
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

void SyncMeasurement::forget()
{
    set_ = false;
    csync_ = false;
}

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
    // **THE MEASURING CONFIGURATION IS THE SEPARATE-SYNC ONE, PUT IN FORCE AS A
    // DEFINED STATE RATHER THAN POKED FIELD BY FIELD.**
    // STATUS_SYNC_PROC_VSACT below reports the path the processor is CONFIGURED
    // for, not what the source drives -- so with the separator still in circuit
    // it reads 0 whatever arrives, and clearing SP_EXT_SYNC_SEL on its own
    // leaves SP_SOG_MODE set and the separator in. The answer is then always
    // "no", which holds a separate-sync source as composite, and that latches:
    // only a source change re-probes, and the count a wrong path produces is
    // outside the source bounds.
    //
    // One owner writes it, so the state the probe measures in is the same state
    // a separate-sync source runs in.
    // ../../../../docs/known-issues.md
    tv5725Log("measure: sync type, probing for the source's own V sync");

    SyncProcessor::applyForSyncType(false, Adc::dividerInForce());
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
    snprintf(line, sizeof(line), "measured: own V sync %s after %ums",
             active ? "YES, separate H/V" : "no, composite or SOG",
             (unsigned)rose);
    tv5725Log(line);

    // Back to the arrangement HELD, which is a defined state too. The caller
    // applies whatever this answer decides, so the only requirement here is to
    // leave nothing half configured behind.
    SyncProcessor::applyForSyncType(isCsync(), Adc::dividerInForce());

    // THE PROBE ANSWERS FOR ITS OWN DISTURBANCE. Both writes above take the
    // sync separator in and out of the path, which the chip latches exactly as
    // it latches the source's own -- and the measurement this probe runs for
    // answers it, so left set it arms a second one on a source that has not
    // moved. ../../../../docs/investigations/the-reference-clock-is-applied-to-a-working-picture.md
    Interrupts::acknowledgeSogSwitch();
    return active;
}

}  // namespace Tv5725
