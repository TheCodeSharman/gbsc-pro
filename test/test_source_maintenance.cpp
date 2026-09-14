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
    SourceMaintenance maintenance;
    SourceMaintenance::Source source;
    source.acquiredPasses = 10;
    source.unmeasuredPasses = 0;
    source.samplingPhaseFound = false;

    CHECK(maintenance.dueAt(source).samplingPhase);

    source.acquiredPasses = 15;
    CHECK_FALSE(maintenance.dueAt(source).samplingPhase);

    source.acquiredPasses = 60;
    CHECK(maintenance.dueAt(source).samplingPhase);

    // Past the window, so a phase never found is left alone rather than
    // retried for the life of the lock.
    source.acquiredPasses = 70;
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
