// Host-compiled unit tests for src/videosource/SyncRecovery.cpp
// -- `make -C test sync-recovery`.
//
// Pure arithmetic, so no fake Wire and no sketch symbols.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/videosource/SyncRecovery.h"



// A mode change is 1.17 to 1.77 s to the picture being shown and a component
// selection 4.4 to 6.9 s, so every rung that used to fire inside the first three
// seconds fired into an acquisition that was working. Measured on a ypbpr leg
// that then failed: the engine read 263 lines at 15576 Hz and installed its
// divider at 2.16 s, and the coast-window rung reset that window 20 ms later,
// after which the count read 271 and STATUS_SYNC_PROC_HTOTAL 3268 against a
// 2200 divider.
// docs/investigations/the-recovery-ladder-fired-into-its-own-acquisition.md
TEST_CASE("an acquisition gets its whole budget before anything is recovered")
{
    CHECK(SyncRecovery::actAt(0) == SyncRecovery::None);
    CHECK(SyncRecovery::actAt(2000) == SyncRecovery::None);
    CHECK(SyncRecovery::actAt(6900) == SyncRecovery::None);
    CHECK(SyncRecovery::actAt(SyncRecovery::FirstActMs - 1) == SyncRecovery::None);
}

TEST_CASE("reconfiguring is the first act, because it disturbs nothing")
{
    CHECK(SyncRecovery::actAt(SyncRecovery::FirstActMs) == SyncRecovery::Reconfigure);
}

TEST_CASE("the blocks are reset only once a reconfigure has failed to take")
{
    CHECK(SyncRecovery::actAt(SyncRecovery::FirstActMs
                              + SyncRecovery::ActIntervalMs)
          == SyncRecovery::ResetBlocks);
}

TEST_CASE("moving the mux is the last act, and the rarest")
{
    CHECK(SyncRecovery::actAt(SyncRecovery::FirstActMs
                              + 2 * SyncRecovery::ActIntervalMs)
          == SyncRecovery::MoveInput);
}

TEST_CASE("each act holds for the whole interval it is given to work in")
{
    // An act is a selection's worth of work, so it gets a selection's worth of
    // time before the next one judges it. The caller fires on the edge, so what
    // this pins is that an act does not come round again inside its own window.
    for (uint32_t ms = SyncRecovery::FirstActMs;
         ms < SyncRecovery::FirstActMs + SyncRecovery::ActIntervalMs; ms += 20)
        CHECK(SyncRecovery::actAt(ms) == SyncRecovery::Reconfigure);
}

TEST_CASE("the acts cycle, because an unplugged source needs them to")
{
    // Stopping would leave a unit switched off and on again with no way back.
    const uint32_t cycle = 3 * SyncRecovery::ActIntervalMs;
    CHECK(SyncRecovery::actAt(SyncRecovery::FirstActMs + cycle)
          == SyncRecovery::Reconfigure);
    CHECK(SyncRecovery::actAt(SyncRecovery::FirstActMs + cycle
                              + SyncRecovery::ActIntervalMs)
          == SyncRecovery::ResetBlocks);
    CHECK(SyncRecovery::actAt(SyncRecovery::FirstActMs + 3 * cycle
                              + 2 * SyncRecovery::ActIntervalMs)
          == SyncRecovery::MoveInput);
}

TEST_CASE("the mux moves once a cycle and no oftener")
{
    // It is the guess of last resort, and the old ladder reached it every 9 s:
    // 413 passes of 20 ms, cycling at 451.
    uint16_t moves = 0;
    const uint32_t cycle = 3 * SyncRecovery::ActIntervalMs;
    SyncRecovery::Act last = SyncRecovery::None;
    for (uint32_t ms = 0; ms < SyncRecovery::FirstActMs + 3 * cycle; ms += 20) {
        const SyncRecovery::Act act = SyncRecovery::actAt(ms);
        if (act != last && act == SyncRecovery::MoveInput)
            ++moves;
        last = act;
    }
    CHECK(moves == 3);
}

TEST_CASE("every act is named, so a unit that has been hunting gives an account")
{
    CHECK(std::string(SyncRecovery::nameOf(SyncRecovery::Reconfigure))
          == "reconfigure");
    CHECK(std::string(SyncRecovery::nameOf(SyncRecovery::ResetBlocks))
          == "reset the blocks");
    CHECK(std::string(SyncRecovery::nameOf(SyncRecovery::MoveInput))
          == "move the input");
    CHECK(std::string(SyncRecovery::nameOf(SyncRecovery::None)) == "none");
}
