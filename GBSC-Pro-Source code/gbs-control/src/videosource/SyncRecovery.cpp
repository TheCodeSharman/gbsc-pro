#include "SyncRecovery.h"

namespace {

struct Rung {
    uint16_t passes;
    SyncRecovery::Step step;
};

// Today's first-fire counts, in order.
//
// **ReprobeSyncType SITS BEFORE THE RUNGS THAT CANNOT MOVE A SYNC PATH.** A
// wrong sync path does not always break the line count: measured in
// pass-through on a composite source held as separate sync, the count is a
// steady and correct 524 and the divider is latched while the field rate reads
// 15.32 Hz where 60 is due. Nothing the count-based arms watch moves, so the
// re-probe is the only rung that can help -- and at 151 it arrived about 56 s
// into a source reported absent.
// ../../../../docs/investigations/a-sync-type-change-arms-no-probe.md
//
// No step may sit at 63. The board-power check rewrites the counter to it and
// the next pass increments past, so 63 is never a count a step is asked for.
//
// ReopenSogSeparator was not a rung at all. It was `% 450` handed to
// SyncOnGreen::reacquire() as its `reopen` flag -- every third pass of the
// reset block, meaning "this caller has run out of walks". That is an
// escalation, so it gets a position rather than hiding inside another step's
// arguments.
const Rung Ladder[] = {
    {2, SyncRecovery::LiftSogFloor},
    {8, SyncRecovery::CoastWindow},
    {27, SyncRecovery::SyncProcessorDynamic},
    {32, SyncRecovery::ReleaseCapture},
    {34, SyncRecovery::HoldClamp},
    {38, SyncRecovery::NudgeModeDetect},
    {44, SyncRecovery::ReprobeSyncType},
    {48, SyncRecovery::HsyncOverflowProtect},
    {60, SyncRecovery::RestartSamplingClock},
    {150, SyncRecovery::FullReset},
    {413, SyncRecovery::ToggleInput},
    {450, SyncRecovery::ReopenSogSeparator},
};

const uint8_t RungCount = sizeof(Ladder) / sizeof(Ladder[0]);

}  // namespace

const uint16_t SyncRecovery::FirstEscalationPass;
const uint16_t SyncRecovery::CycleLength;

const char *SyncRecovery::nameOf(Step step)
{
    switch (step) {
    case LiftSogFloor:          return "lift SOG floor";
    case CoastWindow:           return "coast window";
    case SyncProcessorDynamic:  return "sync processor dynamic";
    case ReleaseCapture:        return "release capture";
    case HoldClamp:             return "hold clamp";
    case NudgeModeDetect:       return "nudge mode detect";
    case HsyncOverflowProtect:  return "hsync overflow protect";
    case RestartSamplingClock:  return "restart sampling clock";
    case FullReset:             return "full reset";
    case ReprobeSyncType:       return "reprobe sync type";
    case ToggleInput:           return "toggle input";
    case ReopenSogSeparator:    return "reopen SOG separator";
    case None:                  break;
    }
    return "none";
}

SyncRecovery::Step SyncRecovery::stepAt(uint16_t passes)
{
    if (passes < FirstEscalationPass)
        return None;

    const uint16_t at = passes % CycleLength;
    for (uint8_t i = 0; i < RungCount; ++i) {
        if (Ladder[i].passes == at)
            return Ladder[i].step;
    }
    return None;
}

uint16_t SyncRecovery::positionOf(Step step)
{
    for (uint8_t i = 0; i < RungCount; ++i) {
        if (Ladder[i].step == step)
            return Ladder[i].passes;
    }
    return 0;
}

