#include "SourceMaintenance.h"

namespace {

// The window the sampling phase is searched in, and how often inside it.
const uint16_t PhaseFirstPass = 10;
const uint16_t PhaseLastPass = 60;
const uint16_t PhaseEveryPasses = 10;

const uint16_t DynamicFirstPass = 2;
const uint16_t DynamicSecondPass = 6;
const uint16_t DynamicEveryPasses = 31;

// A window measured before the source has held describes a line it was not
// yet sending, and the clamp is the cheaper of the two to have wrong.
const uint16_t ClampReadyPasses = 4;
const uint16_t CoastReadyPasses = 7;

// Auto gain reads the picture rather than the sync, so it waits for a run long
// enough that the picture is the source's own.
const uint16_t AutoGainReadyPasses = 91;

const uint16_t ForgetPositionsPass = 45;
const uint16_t AcknowledgeSogBadPass = 160;
const uint16_t DeinterlacerFirstPass = 3;

}  // namespace

SourceMaintenance::SourceMaintenance() : restoreArmed_(false) {}

SourceMaintenance::Due SourceMaintenance::dueAt(const Source &source)
{
    Due due = {false, false, false, false, false, false, false, false};

    const uint16_t acquired = source.acquiredPasses;

    if (source.unmeasuredPasses >= LongAbsencePasses) {
        restoreArmed_ = true;
        due.restoreAfterLongAbsence = true;
        due.forgetPositions = true;
    }

    if (acquired == 1 && !restoreArmed_)
        due.holdCapture = true;

    if (acquired == DynamicFirstPass) {
        due.syncProcessorDynamic = true;
        due.holdCapture = true;
        if (restoreArmed_) {
            due.sogLevel = true;
            restoreArmed_ = false;
        }
    }

    if (acquired == DynamicSecondPass || (acquired % DynamicEveryPasses) == 0)
        due.syncProcessorDynamic = true;

    if (!source.samplingPhaseFound && acquired >= PhaseFirstPass
        && acquired <= PhaseLastPass && (acquired % PhaseEveryPasses) == 0)
        due.samplingPhase = true;

    if (acquired == ForgetPositionsPass)
        due.forgetPositions = true;

    if (acquired == AcknowledgeSogBadPass)
        due.acknowledgeSogBad = true;

    if (acquired >= DeinterlacerFirstPass && source.unmeasuredPasses == 0)
        due.steerDeinterlacer = true;

    return due;
}

SourceMaintenance::Ready SourceMaintenance::readyAt(const Source &source)
{
    Ready ready = {false, false, false};

    ready.clampWindow = source.acquiredPasses >= ClampReadyPasses;
    ready.coastWindow = source.acquiredPasses >= CoastReadyPasses;
    ready.autoGain = source.acquiredPasses >= AutoGainReadyPasses;

    return ready;
}
