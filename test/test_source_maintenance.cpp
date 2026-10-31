// Host-compiled unit tests for src/videosource/SourceMaintenance.cpp
// -- `make -C test source-maintenance`.
//
// The cadence alone, so no fake Wire and no sketch symbols.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/videosource/SourceMaintenance.h"

namespace {

// One acquired pass with the phase already found and no run of failures behind
// it, which is what every case that is not about those varies from.
SourceMaintenance::Due settled(SourceMaintenance &maintenance, uint16_t acquiredPasses)
{
    SourceMaintenance::Source source;
    source.acquiredPasses = acquiredPasses;
    source.arrivedPasses = acquiredPasses;
    source.unmeasuredPasses = 0;
    source.samplingPhaseFound = true;
    return maintenance.dueAt(source);
}

}  // namespace

TEST_CASE("the write FIFO is released on the first two acquired passes")
{
    SourceMaintenance maintenance;

    CHECK(settled(maintenance, 1).holdCapture);
    CHECK(settled(maintenance, 2).holdCapture);
    CHECK_FALSE(settled(maintenance, 3).holdCapture);
}

TEST_CASE("the dynamic sync-processor settings are re-applied on a cadence")
{
    SourceMaintenance maintenance;

    CHECK(settled(maintenance, 2).syncProcessorDynamic);
    CHECK(settled(maintenance, 6).syncProcessorDynamic);
    CHECK(settled(maintenance, 31).syncProcessorDynamic);
    CHECK(settled(maintenance, 62).syncProcessorDynamic);

    CHECK_FALSE(settled(maintenance, 5).syncProcessorDynamic);
    CHECK_FALSE(settled(maintenance, 30).syncProcessorDynamic);
}

TEST_CASE("the sampling phase is tried every tenth pass until it is found")
{
    // A source that sat through no transition, so the two runs agree.
    SourceMaintenance maintenance;
    SourceMaintenance::Source source;
    source.unmeasuredPasses = 0;
    source.samplingPhaseFound = false;
    source.samplingClockSuspect = false;

    source.acquiredPasses = source.arrivedPasses = 60;
    CHECK(maintenance.dueAt(source).samplingPhase);

    source.acquiredPasses = source.arrivedPasses = 65;
    CHECK_FALSE(maintenance.dueAt(source).samplingPhase);

    source.acquiredPasses = source.arrivedPasses = 120;
    CHECK(maintenance.dueAt(source).samplingPhase);

    // Past the window, so a phase never found is left alone rather than
    // retried for the life of the lock.
    source.acquiredPasses = source.arrivedPasses = 130;
    CHECK_FALSE(maintenance.dueAt(source).samplingPhase);
}

TEST_CASE("the phase window follows the arriving source, not the acquired run")
{
    // THE ACQUIRED RUN DOES NOT RESET ON A TRANSITION THE SOURCE SITS THROUGH.
    // An output change re-latches the sampling clock without the source ever
    // reading absent, so a window bounded by the acquired run has long closed
    // by the time the arriving source wants its phase searched -- and the
    // provisional answer the transition's own search left then stands for the
    // life of the lock.
    SourceMaintenance maintenance;
    SourceMaintenance::Source source;
    source.acquiredPasses = 200;
    source.unmeasuredPasses = 0;
    source.samplingPhaseFound = false;
    source.samplingClockSuspect = false;

    source.arrivedPasses = 60;
    CHECK(maintenance.dueAt(source).samplingPhase);

    source.arrivedPasses = 65;
    CHECK_FALSE(maintenance.dueAt(source).samplingPhase);

    // The give-up is per arrival too: a phase never found for THIS source is
    // what stops being worth walking the field for.
    source.arrivedPasses = 130;
    CHECK_FALSE(maintenance.dueAt(source).samplingPhase);
}

TEST_CASE("the sampling phase search waits out the sampling path's settling")
{
    // THE SEARCH READS THE WRONG BAND IF IT RUNS TOO SOON AFTER AN INPUT-CHANGE
    // SETUP, and what it waits for is a settling time rather than a walk.
    // Measured on vga at 1280x720@60 pass-through, the walk suppressed through
    // the arrival with a phase override so nothing disturbs the settling and
    // nothing has walked before the one probe, timed from `sync pad: driven`
    // and swept in both directions:
    //
    //   walk began  0.36  0.42 | 0.61  0.63  0.70  0.72  0.93  0.93
    //   centroid   22.41 22.43 | 15.20 15.40 14.84 15.11 14.75 15.29
    //
    // The band is at 6..22 and a centroid near 14 reports it; near 22 is the
    // fault, and the pick then lands at 6, inside the band, where the picture
    // is visibly worse. A detection pass is 20 ms, so pass 10 began the walk at
    // 0.27..0.44 s and pass 20 at about 0.5 s -- on the threshold, which is why
    // 20 measured no better, four of four.
    //
    // 60 passes is 1.2 s, about twice the boundary. A mode change needs none of
    // it and is correct at pass 10, so the wait is paid on every arrival to
    // cover the one kind that needs it.
    // ../docs/investigations/the-arrivals-own-walk-mismeasures-the-phase-band.md
    SourceMaintenance maintenance;
    SourceMaintenance::Source source;
    source.acquiredPasses = 200;
    source.unmeasuredPasses = 0;
    source.samplingPhaseFound = false;
    source.samplingClockSuspect = false;

    source.arrivedPasses = 10;
    CHECK_FALSE(maintenance.dueAt(source).samplingPhase);

    source.arrivedPasses = 20;
    CHECK_FALSE(maintenance.dueAt(source).samplingPhase);

    source.arrivedPasses = 60;
    CHECK(maintenance.dueAt(source).samplingPhase);
}

TEST_CASE("a suspect sampling clock is retried for as long as it is suspect")
{
    // A RECOVERY THAT STOPS IS NOT A RECOVERY. The phase window closes because
    // a phase never found is not worth walking the field for again -- but the
    // same refusal carrying the CLOCK is the engine's only route to repairing
    // it, and a source that falls out of lock after the window would never be
    // offered one.
    SourceMaintenance maintenance;
    SourceMaintenance::Source source;
    source.unmeasuredPasses = 0;
    source.samplingPhaseFound = false;
    source.samplingClockSuspect = true;

    source.acquiredPasses = source.arrivedPasses = 70;
    CHECK(maintenance.dueAt(source).samplingPhase);

    source.acquiredPasses = source.arrivedPasses = 600;
    CHECK(maintenance.dueAt(source).samplingPhase);

    // Still on the cadence, not on every pass.
    source.acquiredPasses = source.arrivedPasses = 605;
    CHECK_FALSE(maintenance.dueAt(source).samplingPhase);
}

TEST_CASE("a phase already found is not searched for again")
{
    SourceMaintenance maintenance;

    CHECK_FALSE(settled(maintenance, 10).samplingPhase);
}

TEST_CASE("the coast and clamp windows re-place once the source has held")
{
    SourceMaintenance maintenance;

    CHECK(settled(maintenance, 45).forgetPositions);
    CHECK_FALSE(settled(maintenance, 44).forgetPositions);
}

TEST_CASE("the separator's latched complaint is cleared once the source has held")
{
    SourceMaintenance maintenance;

    CHECK(settled(maintenance, 160).acknowledgeSogBad);
    CHECK_FALSE(settled(maintenance, 159).acknowledgeSogBad);
}

TEST_CASE("the deinterlacer is steered from the third pass with nothing outstanding")
{
    SourceMaintenance maintenance;
    SourceMaintenance::Source source;
    source.acquiredPasses = 3;
    source.unmeasuredPasses = 0;
    source.samplingPhaseFound = true;

    CHECK(maintenance.dueAt(source).steerDeinterlacer);

    source.acquiredPasses = 2;
    CHECK_FALSE(maintenance.dueAt(source).steerDeinterlacer);
}

TEST_CASE("a long run of failed passes arms a restore, and the restore runs once")
{
    SourceMaintenance maintenance;
    SourceMaintenance::Source source;
    source.acquiredPasses = 0;
    source.unmeasuredPasses = 150;
    source.samplingPhaseFound = true;

    const SourceMaintenance::Due armed = maintenance.dueAt(source);
    CHECK(armed.restoreAfterLongAbsence);
    CHECK(armed.forgetPositions);

    // The capture hold is what the restore replaces on the first pass: the
    // frame held would be one from before the absence.
    CHECK_FALSE(settled(maintenance, 1).holdCapture);

    const SourceMaintenance::Due restoring = settled(maintenance, 2);
    CHECK(restoring.sogLevel);
    CHECK(restoring.holdCapture);

    // Disarmed, so the next source to reach two passes is not restored on the
    // strength of an absence it never had.
    CHECK_FALSE(settled(maintenance, 2).sogLevel);
}

TEST_CASE("a short run of failed passes arms nothing")
{
    SourceMaintenance maintenance;
    SourceMaintenance::Source source;
    source.acquiredPasses = 0;
    source.unmeasuredPasses = 149;
    source.samplingPhaseFound = true;

    CHECK_FALSE(maintenance.dueAt(source).restoreAfterLongAbsence);
    CHECK(settled(maintenance, 1).holdCapture);
}

// --- how long an acquired source must hold before a window is worth placing --
//
// A THRESHOLD RATHER THAN A CADENCE, which is why it is not in Due: these stay
// true once reached, and what stops them running twice is the placement's own
// register state rather than the pass they fell due on.

namespace {

SourceMaintenance::Ready ready(SourceMaintenance &maintenance, uint16_t acquiredPasses)
{
    SourceMaintenance::Source source;
    source.acquiredPasses = acquiredPasses;
    source.unmeasuredPasses = 0;
    source.samplingPhaseFound = true;
    return maintenance.readyAt(source);
}

}  // namespace

TEST_CASE("the clamp window waits for fewer passes than the coast window")
{
    SourceMaintenance maintenance;

    CHECK_FALSE(ready(maintenance, 3).clampWindow);
    CHECK(ready(maintenance, 4).clampWindow);
    CHECK(ready(maintenance, 40).clampWindow);

    CHECK_FALSE(ready(maintenance, 6).coastWindow);
    CHECK(ready(maintenance, 7).coastWindow);
    CHECK(ready(maintenance, 40).coastWindow);
}

TEST_CASE("auto gain waits for a long settled run")
{
    SourceMaintenance maintenance;

    CHECK_FALSE(ready(maintenance, 90).autoGain);
    CHECK(ready(maintenance, 91).autoGain);
}
