// Host-compiled unit tests for Tv5725::SourceKey -- `make -C test source-key`.
//
// What identifies a source, so a framing can be kept against it. The rules it
// has to satisfy are in docs/framing-presets.md, "The key".

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceKey.h"

// The two the sketch supplies. The key only reaches SourceMeasurement for the
// bounds on a count and a rate, so neither is ever called.
#include "DebugPinStub.h"
uint32_t debugPinPulseTicks() { return ticksForHz(0.0f); }
void tv5725Log(const char *) {}

using namespace Tv5725;

TEST_CASE("a source with no measurement behind it identifies nothing")
{
    // Two sources nobody has measured are not the same source, or a framing
    // would carry into whatever arrives next.
    CHECK_FALSE(SourceKey().valid());
    CHECK(SourceKey() != SourceKey());
}

TEST_CASE("the same source measured twice is the same key")
{
    CHECK(SourceKey(311, 50.08f) == SourceKey(311, 50.08f));
}

TEST_CASE("the rate is bucketed wider than it jitters")
{
    // The engine re-solves on the measured field rate and that reading wobbles,
    // so an exact match on a float misses its own entry.
    CHECK(SourceKey(311, 50.02f) == SourceKey(311, 50.13f));
}

TEST_CASE("readings either side of a whole hertz are still the same source")
{
    // The rounding puts these in different hertz -- 60 and 61 -- and the
    // tolerance is what keeps them one source, which is the reason it is wider
    // than the rounding. Measured at 800x600@60: one unchanged source settles
    // at 60.38 Hz after one mode change and 60.72 after the next. Were identity
    // decided by the rounded value, the stored framing would swap with it.
    CHECK(SourceKey(627, 60.38f) == SourceKey(627, 60.72f));
    CHECK(SourceKey(627, 60.38f).rateHz() != SourceKey(627, 60.72f).rateHz());
}

TEST_CASE("the key carries a whole number of hertz, not the reading")
{
    // The timings are generated FROM the key, so the key is what has to be
    // repeatable. A measurement carries a wandering fraction -- the bench reads
    // 800x600@60 at 60.38 and 60.72 across mode changes -- and a raster solved
    // from it moves by 11 px between two solves of one unchanged source.
    CHECK(SourceKey(311, 50.08f).rateHz() == 50.0f);
    CHECK(SourceKey(627, 60.38f).rateHz() == 60.0f);
    CHECK(SourceKey(524, 59.94f).rateHz() == 60.0f);
}

TEST_CASE("a rate change too small to be movement does not change identity")
{
    // ratesAgree() calls two rates within HeldRateTolerancePerMille the same
    // measurement, so a change inside it arms no mode change. If the key moved
    // there, solveForSource() would swap the stored framing with no re-solve
    // behind it -- a silent reframing on drift.
    //
    // 60.0 against 62.5 is 4.2%, inside the 5% that trigger allows.
    CHECK(SourceKey(627, 60.0f) == SourceKey(627, 62.5f));
}

TEST_CASE("adjacent standards stay apart")
{
    CHECK(SourceKey(311, 50.08f) != SourceKey(311, 60.05f));
    CHECK(SourceKey(311, 50.08f) != SourceKey(312, 50.08f));
}

TEST_CASE("a line count no source runs is not a key")
{
    // A settling source passes through counts inside no standard at all, and a
    // framing stored against one of those is stored against nothing.
    CHECK_FALSE(SourceKey(97, 50.08f).valid());
    CHECK_FALSE(SourceKey(311, 0.0f).valid());
}
