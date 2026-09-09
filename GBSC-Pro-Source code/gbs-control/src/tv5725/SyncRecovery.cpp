#include "SyncRecovery.h"

namespace Tv5725 {

namespace {

struct Rung {
    uint16_t passes;
    SyncRecovery::Step step;
};

// Today's first-fire counts, in order. Only ReprobeSyncType moves: it shared
// 150 with FullReset, and one pass later -- about 20 ms -- buys one step per
// position and costs nothing measurable.
const Rung Ladder[] = {
    {2, SyncRecovery::LiftSogFloor},
    {8, SyncRecovery::CoastWindow},
    {27, SyncRecovery::SyncProcessorDynamic},
    {32, SyncRecovery::ReleaseCapture},
    {34, SyncRecovery::HoldClamp},
    {38, SyncRecovery::NudgeModeDetect},
    {63, SyncRecovery::HsyncOverflowProtect},
    {150, SyncRecovery::FullReset},
    {151, SyncRecovery::ReprobeSyncType},
    {413, SyncRecovery::ToggleInput},
};

const uint8_t RungCount = sizeof(Ladder) / sizeof(Ladder[0]);

}  // namespace

const uint16_t SyncRecovery::FirstEscalationPass;
const uint16_t SyncRecovery::CycleLength;

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

}  // namespace Tv5725
