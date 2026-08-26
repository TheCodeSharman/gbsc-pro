// Host-compiled unit tests for Tv5725::SyncType -- `make -C test sync-type`.
//
// Whether a source brings its own H and V or carries composite sync cannot be
// read back: STATUS_SYNC_PROC_VSACT reports the path already configured, so a
// unit that lands on csync stays there. docs/sync-type-selection.md
//
// The probe that breaks that circularity costs ~500 ms, so it runs once per
// SOURCE. This holds that gate; the probe itself is the sketch's, because it is
// a 240 ms settle and a 250 ms poll.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncType.h"

using namespace Tv5725;

static int probeCalls = 0;
static bool probeAnswer = false;

static bool countingProbe()
{
    ++probeCalls;
    return probeAnswer;
}

static void given(bool sourceHasOwnVsync)
{
    SyncType::forget();
    SyncType::set(false);
    probeCalls = 0;
    probeAnswer = sourceHasOwnVsync;
}

TEST_CASE("a source with its own vsync is not composite sync")
{
    given(true);
    SyncType::probeOnce(countingProbe);
    CHECK_FALSE(SyncType::isCsync());
}

TEST_CASE("a source without its own vsync is composite sync")
{
    given(false);
    SyncType::probeOnce(countingProbe);
    CHECK(SyncType::isCsync());
}

TEST_CASE("the probe runs once per source, not once per mode change")
{
    given(false);

    SyncType::probeOnce(countingProbe);
    SyncType::probeOnce(countingProbe);
    SyncType::probeOnce(countingProbe);

    CHECK(probeCalls == 1);
}

TEST_CASE("forgetting re-arms the probe")
{
    given(false);
    SyncType::probeOnce(countingProbe);
    CHECK(probeCalls == 1);

    SyncType::forget();
    SyncType::probeOnce(countingProbe);
    CHECK(probeCalls == 2);
}

TEST_CASE("a forced probe runs whether or not the answer is already held")
{
    given(false);
    SyncType::probeOnce(countingProbe);
    SyncType::probe(countingProbe);

    CHECK(probeCalls == 2);
}

TEST_CASE("a forced probe leaves the answer held, so the next mode change pays nothing")
{
    given(false);
    SyncType::probe(countingProbe);

    CHECK(SyncType::isSet());
    SyncType::probeOnce(countingProbe);
    CHECK(probeCalls == 1);
}

TEST_CASE("setting the type by hand does not claim the source was probed")
{
    given(false);

    // The YPbPr fallback and the two temporary flips during detection assert a
    // type without measuring one. Marking that as probed would suppress the
    // real probe for the rest of the source.
    SyncType::set(true);

    CHECK(SyncType::isCsync());
    CHECK_FALSE(SyncType::isSet());
}

TEST_CASE("a set value survives until something probes or sets again")
{
    given(false);
    SyncType::set(true);
    CHECK(SyncType::isCsync());

    probeAnswer = true;  // the source does have its own vsync
    SyncType::probeOnce(countingProbe);
    CHECK_FALSE(SyncType::isCsync());
}
