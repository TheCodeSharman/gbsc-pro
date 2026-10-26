// Host-compiled unit tests for Tv5725::BusHistogram -- `make -C test bus-histogram`.
//
// What a test-bus pair reads has to be summarised on the device, because most
// of the sync processor's pairs are live wires: 37 of its 64 dither within one
// acquisition, so a single read samples the wire's state rather than anything
// about the source. A distribution taken from loop() is what can be compared
// between two states.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/BusHistogram.h"

using namespace Tv5725;

TEST_CASE("a value read over and over reads as one value at both extremes")
{
    BusHistogram seen;
    for (int i = 0; i < 50; ++i)
        seen.add(1856);

    CHECK(seen.count() == 50);
    CHECK(seen.lowest() == 1856);
    CHECK(seen.highest() == 1856);
    CHECK(seen.mean() == 1856);
    CHECK(seen.distinct() == 1);
    CHECK_FALSE(seen.overflowed());
}

TEST_CASE("a live wire's two levels read as two values and the extremes bracket them")
{
    BusHistogram seen;
    for (int i = 0; i < 30; ++i)
        seen.add(1856);
    for (int i = 0; i < 10; ++i)
        seen.add(16514);

    CHECK(seen.count() == 40);
    CHECK(seen.lowest() == 1856);
    CHECK(seen.highest() == 16514);
    CHECK(seen.distinct() == 2);
    CHECK(seen.countAt(0) == 30);
    CHECK(seen.countAt(1) == 10);
}

// A partial table that did not say so would read as the whole distribution,
// and the values kept are the FIRST seen rather than the most frequent -- so a
// reader comparing two states has to know the table is a sample.
TEST_CASE("more distinct values than the table holds says so")
{
    BusHistogram seen;
    for (uint16_t value = 0; value < BusHistogram::Slots + 4; ++value)
        seen.add((uint16_t)(100 + value));

    CHECK(seen.distinct() == BusHistogram::Slots);
    CHECK(seen.overflowed());
    CHECK(seen.count() == BusHistogram::Slots + 4);
    CHECK(seen.highest() == 100 + BusHistogram::Slots + 3);
}

// Nothing sampled is not a mean of zero, which a consumer would print as a
// reading. The count is what says whether there is one.
TEST_CASE("an empty histogram reports no count rather than a value")
{
    const BusHistogram seen;

    CHECK(seen.count() == 0);
    CHECK(seen.mean() == 0);
    CHECK(seen.distinct() == 0);
    CHECK_FALSE(seen.overflowed());
}

// The sum of a long run overflows 32 bits at about 65000 samples of a
// full-scale value, and a sampling run is meant to be long.
TEST_CASE("a long run of a wide value still means what it should")
{
    BusHistogram seen;
    for (uint32_t i = 0; i < 100000; ++i)
        seen.add(0xFFFF);

    CHECK(seen.count() == 100000);
    CHECK(seen.mean() == 0xFFFF);
}

TEST_CASE("clearing it forgets every reading")
{
    BusHistogram seen;
    seen.add(42);
    seen.add(99);
    seen.clear();

    CHECK(seen.count() == 0);
    CHECK(seen.distinct() == 0);
    seen.add(7);
    CHECK(seen.lowest() == 7);
    CHECK(seen.highest() == 7);
}
