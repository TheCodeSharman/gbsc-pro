#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/AgreementRun.h"

using Tv5725::AgreementRun;

TEST_CASE("one reading is not a measurement")
{
    AgreementRun run(8);
    CHECK_FALSE(run.offer(false));
}

TEST_CASE("a pair that agrees is the measurement")
{
    AgreementRun run(8);
    REQUIRE_FALSE(run.offer(false));
    CHECK(run.offer(true));
}

TEST_CASE("the budget is spent by disagreements, and the next reading is taken")
{
    // The escape a reading that genuinely wanders needs: the capture is frozen
    // while a mode change is outstanding, so a source with no settled value
    // would sit on a frozen picture rather than a slightly wrong raster.
    AgreementRun run(8);
    for (uint8_t i = 0; i < 8; ++i) {
        CAPTURE(i);
        CHECK_FALSE(run.offer(false));
    }
    CHECK(run.offer(false));
}

TEST_CASE("an agreement gives the budget back")
{
    // A reading that REPEATED is not a disagreement, and counting it against
    // the budget leaves a source that settled and then started wandering taken
    // as it stands on its first disagreement -- with nothing held back at all
    // once the budget has been burnt by successes.
    AgreementRun run(8);
    REQUIRE_FALSE(run.offer(false));
    for (uint8_t i = 0; i < 6; ++i)
        REQUIRE(run.offer(true));

    for (uint8_t i = 0; i < 8; ++i) {
        CAPTURE(i);
        CHECK_FALSE(run.offer(false));
    }
    CHECK(run.offer(false));
}

TEST_CASE("a reading is owed while a pair has been seen and did not agree")
{
    AgreementRun run(8);
    CHECK_FALSE(run.awaiting());
    REQUIRE_FALSE(run.offer(false));
    CHECK(run.awaiting());
    REQUIRE(run.offer(true));
    CHECK_FALSE(run.awaiting());
}

TEST_CASE("reset forgets the budget as well as the offer")
{
    AgreementRun run(8);
    for (uint8_t i = 0; i < 4; ++i)
        REQUIRE_FALSE(run.offer(false));
    run.reset();
    CHECK_FALSE(run.awaiting());
    for (uint8_t i = 0; i < 8; ++i) {
        CAPTURE(i);
        CHECK_FALSE(run.offer(false));
    }
    CHECK(run.offer(false));
}
