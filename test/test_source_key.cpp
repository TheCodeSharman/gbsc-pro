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
float getSourceFieldRate(bool) { return 0.0f; }
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
