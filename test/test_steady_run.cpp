// Host-compiled unit tests for Tv5725::SteadyRun -- `make -C test steady-run`.
//
// One definition of "the measurement has settled", because there are two runs
// over the same register read and they drifted apart: the count gate learned to
// accept an interlaced source's alternation and the source-moved gate did not,
// so an interlaced source still never acquired.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SteadyRun.h"

using Tv5725::SteadyRun;

static const uint8_t Samples = 4;

static bool feed(SteadyRun &run, const uint16_t *values, uint8_t count)
{
    bool settled = false;
    for (uint8_t i = 0; i < count; ++i)
        settled = run.sample(values[i]);
    return settled;
}

TEST_CASE("a repeated value settles once enough have agreed")
{
    SteadyRun run(Samples);

    for (uint8_t i = 1; i < Samples; ++i) {
        CAPTURE(i);
        CHECK_FALSE(run.sample(311));
    }
    CHECK(run.sample(311));
    CHECK(run.value() == 311);
    CHECK_FALSE(run.alternated());
}

TEST_CASE("a value that moves starts the run again")
{
    SteadyRun run(Samples);
    const uint16_t settling[] = {311, 311, 311, 311};
    REQUIRE(feed(run, settling, 4));

    CHECK_FALSE(run.sample(97));
}

// An interlaced field carries a half line, so its count cannot hold still. A run
// demanding identical samples never completes and the source never acquires.
TEST_CASE("a pair alternating by one settles too")
{
    SteadyRun run(Samples);
    const uint16_t interlaced[] = {311, 312, 311, 312, 311, 312};

    CHECK(feed(run, interlaced, 6));
    CHECK(run.alternated());
}

TEST_CASE("the higher of an alternating pair is the settled value")
{
    // Both undercount the true field -- 259.5 against 262.5 on a Wii at 480i --
    // so the higher is the closer of the two.
    SteadyRun run(Samples);
    const uint16_t interlaced[] = {259, 260, 259, 260, 259, 260};
    REQUIRE(feed(run, interlaced, 6));

    CHECK(run.value() == 260);
}

TEST_CASE("the pair widens once and no further")
{
    SteadyRun run(Samples);
    const uint16_t drifting[] = {311, 312, 313};

    CHECK_FALSE(feed(run, drifting, 3));
    CHECK_FALSE(run.alternated());
}

TEST_CASE("a jump of more than one is not a pair")
{
    SteadyRun run(Samples);
    const uint16_t jumping[] = {311, 313, 311, 313};

    CHECK_FALSE(feed(run, jumping, 4));
}

TEST_CASE("nothing has alternated before the run completes")
{
    SteadyRun run(Samples);
    run.sample(311);

    CHECK_FALSE(run.sample(312));
    CHECK_FALSE(run.alternated());
}

TEST_CASE("a value that cannot settle is still the one reported")
{
    // The caller rejects a count no source runs before offering it, and still
    // wants to report what arrived.
    SteadyRun run(Samples);
    const uint16_t settled[] = {311, 311, 311, 311};
    REQUIRE(feed(run, settled, 4));

    run.restart(97);

    CHECK(run.value() == 97);
    CHECK_FALSE(run.sample(97));
}

TEST_CASE("a reset forgets the run and the value")
{
    SteadyRun run(Samples);
    const uint16_t settled[] = {311, 311, 311, 311};
    REQUIRE(feed(run, settled, 4));

    run.reset();

    CHECK(run.value() == 0);
    CHECK_FALSE(run.sample(311));
}

// The solve gates on its own longer run over the same count, so the idle run
// starts satisfied rather than re-earning what has just been measured.
TEST_CASE("a run can be handed a value already settled")
{
    SteadyRun run(Samples);

    run.settle(311);

    CHECK(run.value() == 311);
    CHECK(run.sample(311));
    CHECK_FALSE(run.alternated());
}
