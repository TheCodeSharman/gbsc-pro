// Host-compiled unit tests for Tv5725::FramingSaveTimer --
// `make -C test framing-save-timer`.
//
// The framing table saves itself once the framing has held still, so a caller
// that moves the framing and walks away persists it. The inhibit is what lets a
// test disturb the framing without leaving it on flash as the source's
// remembered framing.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/FramingSaveTimer.h"

using namespace Tv5725;

static const uint32_t Quiet = 15000;

TEST_CASE("a framing that is still moving is not due")
{
    FramingSaveTimer timer;

    CHECK_FALSE(timer.due(1, 1000, Quiet));
    CHECK_FALSE(timer.due(2, 2000, Quiet));
    CHECK_FALSE(timer.due(3, 3000, Quiet));
}

TEST_CASE("a framing that has held still for the quiet period is due")
{
    FramingSaveTimer timer;

    CHECK_FALSE(timer.due(1, 1000, Quiet));
    CHECK_FALSE(timer.due(1, 1000 + Quiet - 1, Quiet));
    CHECK(timer.due(1, 1000 + Quiet, Quiet));
}

TEST_CASE("a revision already saved is not due again")
{
    FramingSaveTimer timer;

    CHECK_FALSE(timer.due(1, 1000, Quiet));
    CHECK(timer.due(1, 1000 + Quiet, Quiet));

    timer.markSaved(1);
    CHECK_FALSE(timer.due(1, 1000 + 2 * Quiet, Quiet));
}

TEST_CASE("a move after a save becomes due again")
{
    FramingSaveTimer timer;

    timer.markSaved(1);
    CHECK_FALSE(timer.due(2, 1000, Quiet));
    CHECK(timer.due(2, 1000 + Quiet, Quiet));
}

TEST_CASE("nothing is due while the save is inhibited")
{
    FramingSaveTimer timer;

    timer.inhibit(true);
    CHECK(timer.inhibited());

    CHECK_FALSE(timer.due(1, 1000, Quiet));
    CHECK_FALSE(timer.due(1, 1000 + Quiet, Quiet));
    CHECK_FALSE(timer.due(1, 1000 + 100 * Quiet, Quiet));
}

TEST_CASE("nothing that moved under the inhibit reaches flash afterwards")
{
    FramingSaveTimer timer;

    timer.inhibit(true);
    CHECK_FALSE(timer.due(7, 1000, Quiet));
    CHECK_FALSE(timer.due(7, 1000 + Quiet, Quiet));

    // Lifting the inhibit must not write the framing it was protecting against.
    // Delaying that write would only postpone it: whatever disturbed the
    // framing has walked away, so it will hold still for any quiet period.
    timer.inhibit(false);
    CHECK_FALSE(timer.due(7, 1000 + 2 * Quiet, Quiet));
    CHECK_FALSE(timer.due(7, 1000 + 9 * Quiet, Quiet));
}

TEST_CASE("a framing changed after the inhibit lifted is saved as usual")
{
    FramingSaveTimer timer;

    timer.inhibit(true);
    CHECK_FALSE(timer.due(7, 1000, Quiet));
    timer.inhibit(false);

    CHECK_FALSE(timer.due(8, 2000, Quiet));
    CHECK(timer.due(8, 2000 + Quiet, Quiet));
}
