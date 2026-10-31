#include "SourceMaintenance.h"

namespace {

// The window the sampling phase is searched in, and how often inside it.
//
// IT OPENS LATE BECAUSE THE SAMPLING PATH SETTLES AFTER AN INPUT-CHANGE SETUP.
// A walk run before it has reports the dirty band eight to ten phases on, so
// the middle of the widest clean run lands inside the real band and the picture
// is worse for it. The boundary is between 0.42 s and 0.61 s past the pad being
// driven, and a detection pass is 20 ms, so 60 passes is about twice it. What
// it waits for is the settling and not a walk: with the search suppressed
// through the whole arrival, the first walk taken after it is correct.
// ../../../docs/investigations/the-arrivals-own-walk-mismeasures-the-phase-band.md
const uint16_t PhaseFirstPass = 60;
const uint16_t PhaseLastPass = 120;
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

SourceMaintenance::Source::Source()
    : acquiredPasses(0), arrivedPasses(0), unmeasuredPasses(0),
      samplingPhaseFound(false), samplingClockSuspect(false)
{
}

SourceMaintenance::SourceMaintenance() : restoreArmed_(false) {}

SourceMaintenance::Due SourceMaintenance::dueAt(const Source &source)
{
    Due due = {false, false, false, false, false, false, false, false};

    const uint16_t acquired = source.acquiredPasses;
    const uint16_t arrived = source.arrivedPasses;

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

    // In the ARRIVING source's run, because what the search answers for is the
    // sampling and a transition re-latches that without the source moving.
    //
    // The window closes on a phase never found, because walking the field again
    // buys nothing. It does NOT close while the refusal blamed the clock: that
    // refusal is the engine's only route to repairing one, so it stays due.
    if (!source.samplingPhaseFound && arrived >= PhaseFirstPass
        && (arrived <= PhaseLastPass || source.samplingClockSuspect)
        && (arrived % PhaseEveryPasses) == 0)
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
