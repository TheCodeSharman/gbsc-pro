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

// The rule sample() applies, exposed so a caller comparing a fresh count
// against a settled one applies the same one rather than ==.
TEST_CASE("two counts agree when they are equal or one apart")
{
    CHECK(SteadyRun::agree(311, 311));
    CHECK(SteadyRun::agree(311, 312));
    CHECK(SteadyRun::agree(312, 311));

    CHECK_FALSE(SteadyRun::agree(311, 313));
    CHECK_FALSE(SteadyRun::agree(311, 524));
    CHECK_FALSE(SteadyRun::agree(311, 97));
}

TEST_CASE("agreement at zero does not wrap")
{
    // A count of 0 is what an unmeasured source reads, and it must not agree
    // with 65535 through an underflow.
    CHECK_FALSE(SteadyRun::agree(0, 65535));
    CHECK(SteadyRun::agree(0, 1));
    CHECK(SteadyRun::agree(0, 0));
}

// An interlaced field carries a half line, so its count alternates and the pair
// widens to hold both values. A source that STOPS alternating has to collapse
// it again, or `alternated()` is true for the life of the run and
// `measureScanType()` answers ScanInterlaced for ever -- which leaves motion
// adapt engaged on a progressive source, weaving a picture that is not
// interlaced. An ordinary flash arms it: the count wobbles once through
// re-acquisition and never alternates again.
// docs/known-issues.md
TEST_CASE("a source that stops alternating stops reporting an alternating pair")
{
    SteadyRun run(Samples);

    for (uint8_t i = 0; i < 2 * SteadyRun::CrossingsForInterlace; ++i)
        run.sample(i % 2 ? 628 : 627);
    REQUIRE(run.settled());
    REQUIRE(run.alternated());

    SUBCASE("a run of one value collapses the pair onto it") {
        for (uint8_t i = 0; i < SteadyRun::CollapseSamples; ++i)
            run.sample(627);
        CHECK_FALSE(run.alternated());
        CHECK(run.value() == 627);
        CHECK(run.settled());
    }

    SUBCASE("and one sample short of it does not") {
        for (uint8_t i = 0; i < SteadyRun::CollapseSamples - 1; ++i)
            run.sample(627);
        CHECK(run.alternated());
    }

    SUBCASE("and collapses onto the value that ran, not onto the lower end") {
        // The mirror of the case above. Collapsing onto whichever end happens
        // to be held reports a count a line out from the one the source is
        // actually running, and that count is what the solve is sized from.
        for (uint8_t i = 0; i < SteadyRun::CollapseSamples; ++i)
            run.sample(628);
        CHECK_FALSE(run.alternated());
        CHECK(run.value() == 628);
    }

    SUBCASE("a run broken by the other value starts the count again") {
        for (uint8_t i = 0; i < SteadyRun::CollapseSamples - 1; ++i)
            run.sample(627);
        run.sample(628);
        for (uint8_t i = 0; i < SteadyRun::CollapseSamples - 1; ++i)
            run.sample(627);
        CHECK(run.alternated());
    }
}

// The collapse must clear the runs a GENUINELY interlaced source shows, or it
// drops motion adapt mid-picture. Measured on the RISC PC at 800x600@60 with
// ModeServ's INTERLACE ON, sampled at the engine's own 20 ms detection
// interval: 1873 samples, 953 of 628 against 920 of 627, and the longest run of
// either value is FIVE. A second window at 25 ms agrees -- 1129 samples, same
// longest run. CollapseSamples carries three times that.
TEST_CASE("a genuinely interlaced count is not collapsed by the runs it shows")
{
    REQUIRE(SteadyRun::CollapseSamples > 3 * 5);

    SteadyRun run(Samples);

    // The measured run-length histogram, worst case first and repeated: no
    // arrangement of runs this short may collapse the pair.
    const uint8_t lengths[] = {5, 4, 3, 2, 1, 2, 3, 2, 1, 2, 4, 2, 1, 3, 5, 2};
    uint16_t value = 627;
    for (int pass = 0; pass < 8; ++pass) {
        for (uint8_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
            for (uint8_t n = 0; n < lengths[i]; ++n)
                run.sample(value);
            value = value == 627 ? 628 : 627;
        }
        REQUIRE(run.alternated());
    }
}

// THE BOOT FAULT. A source wobbles by one as it is acquired, which widens the
// pair -- and a pair that has been widened once reported alternating for every
// sample until the collapse, sixteen samples later. Deinterlacer::FilteredPasses
// is TWO, so motion adapt engaged fourteen samples before the collapse could
// say the source was progressive, and the picture came up green and comb-torn
// for the life of the boot. Measured on every ESP reset.
// ../docs/known-issues.md
TEST_CASE("one excursion through acquisition is not an alternating count")
{
    SteadyRun run(Samples);
    for (uint8_t i = 0; i < Samples; ++i)
        run.sample(627);
    REQUIRE(run.settled());
    REQUIRE_FALSE(run.alternated());

    run.sample(628);
    CHECK_FALSE(run.alternated());

    for (uint8_t i = 0; i < SteadyRun::CollapseSamples; ++i) {
        CAPTURE(i);
        run.sample(627);
        CHECK_FALSE(run.alternated());
    }
}

// Excursions far enough apart are the same evidence as one of them, so they may
// not add up to an alternating count. A run longer than any interlaced source
// shows is what forgets them.
TEST_CASE("excursions spread out do not accumulate into an alternation")
{
    SteadyRun run(Samples);
    for (uint8_t i = 0; i < Samples; ++i)
        run.sample(627);
    REQUIRE(run.settled());

    for (uint8_t excursion = 0; excursion < 6; ++excursion) {
        CAPTURE(excursion);
        run.sample(628);
        CHECK_FALSE(run.alternated());
        for (uint8_t i = 0; i < SteadyRun::AlternationStaleRun; ++i)
            run.sample(627);
        CHECK_FALSE(run.alternated());
    }
}

// The other direction, and the one that must not regress: a count that really
// does alternate has to be reported quickly, because motion adapt is what makes
// an interlaced picture legible.
TEST_CASE("a count that keeps crossing is reported alternating within a few samples")
{
    SteadyRun run(Samples);
    for (uint8_t i = 0; i < 2 * SteadyRun::CrossingsForInterlace; ++i)
        run.sample(i % 2 ? 628 : 627);

    CHECK(run.settled());
    CHECK(run.alternated());
}

// A count that moves by one and STAYS there is a new count, not the second half
// of a pair. Nothing else adopts it: the collapse narrows a widened pair, and a
// pair that never widened has nothing to narrow, so without this the run reports
// the value the source has left for as long as it runs.
TEST_CASE("a count that moves by one and holds is adopted")
{
    SteadyRun run(Samples);
    for (uint8_t i = 0; i < Samples; ++i)
        run.sample(627);
    REQUIRE(run.value() == 627);

    for (uint8_t i = 0; i < SteadyRun::CollapseSamples; ++i)
        run.sample(628);

    CHECK(run.value() == 628);
    CHECK(run.settled());
    CHECK_FALSE(run.alternated());
}
