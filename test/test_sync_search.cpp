// Host-compiled unit tests for src/input/SyncSearch.h -- `make -C test sync-search`.
//
// The question is only "which search does this saved source and this V-sync
// reading select", which is pure logic -- so the detection livelock is
// reproducible here in a millisecond rather than on a board that has to be
// power-cycled and hand-restored to try again.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/input/SyncSearch.h"

TEST_CASE("a source with V-sync takes the V-sync-present search")
{
    SUBCASE("saved as RGBs") {
        CHECK(SyncSearch::searchFor(SyncSearch::SourceRgbs, true) ==
              SyncSearch::VsyncPresent);
    }

    SUBCASE("saved as VGA") {
        CHECK(SyncSearch::searchFor(SyncSearch::SourceVga, true) ==
              SyncSearch::VsyncPresent);
    }
}

TEST_CASE("a source with no V-sync takes the V-sync-absent search")
{
    SUBCASE("saved as RGBs") {
        CHECK(SyncSearch::searchFor(SyncSearch::SourceRgbs, false) ==
              SyncSearch::VsyncAbsent);
    }

    SUBCASE("saved as VGA -- the livelock") {
        // Gating the V-sync-absent search on S_RGBs alone leaves a unit saved as
        // S_VGA reading VSACT 0 matching no search at all, and detection then
        // resets the sync processor, flips ADC_INPUT_SEL to the empty input and
        // reports nothing found, forever -- ADC_INPUT_SEL alternating on a ~1 s
        // beat. VSACT 0 is the NORMAL reading on this bench, so this is the
        // common case rather than a corner.
        CHECK(SyncSearch::searchFor(SyncSearch::SourceVga, false) ==
              SyncSearch::VsyncAbsent);
    }
}

TEST_CASE("sources this path does not own select no search")
{
    // YPbPr has its own branch on ADC_INPUT_SEL 0, and 0 means nothing
    // meaningful was ever saved. Neither should be dragged into an RGB search.
    SUBCASE("YPbPr, with V-sync") {
        CHECK(SyncSearch::searchFor(SyncSearch::SourceYuv, true) == SyncSearch::None);
    }

    SUBCASE("YPbPr, without V-sync") {
        CHECK(SyncSearch::searchFor(SyncSearch::SourceYuv, false) == SyncSearch::None);
    }

    SUBCASE("nothing saved") {
        CHECK(SyncSearch::searchFor(0, false) == SyncSearch::None);
    }
}

TEST_CASE("a source the sync processor is counting is not swept for sync")
{
    CHECK(SyncSearch::shouldSweepSyncProcessor(0, true) == false);
}

TEST_CASE("nothing counted and no mode named is swept")
{
    CHECK(SyncSearch::shouldSweepSyncProcessor(0, false) == true);
}

TEST_CASE("a named mode is never swept")
{
    CHECK(SyncSearch::shouldSweepSyncProcessor(15, false) == false);
}
