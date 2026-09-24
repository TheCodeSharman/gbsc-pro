// Host tests for FrameTimeLock -- `make -C test frame-time-lock`.
//
// The gate, not the correction: when the lock is allowed to run, which of its
// two corrections runs, and what it says it is doing. Every case drives a real
// VideoSourceAcquisition over the fake bus, because what the gate asks about
// the source is held there.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "SolvedEngine.h"
#include "MeasuredSource.h"

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/FrameSync.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputMode.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/videosource/FrameTimeLock.h"

using namespace Tv5725;

namespace {

// The gate answers NULL when nothing is blocking, which is not a string.
std::string reason(const char *blocked)
{
    return blocked != NULL ? std::string(blocked) : std::string("(nothing)");
}

// Everything open: the option on, a source present, the sync watcher running.
FrameTimeLock::Conditions allOpen()
{
    FrameTimeLock::Conditions conditions;
    conditions.optionEnabled = true;
    conditions.sourcePresent = true;
    conditions.syncWatcherEnabled = true;
    conditions.method = 0;
    return conditions;
}

// Hold the source until it has been acquired for long enough to correct
// against, and place the coast window -- which loop() does rather than the
// acquisition pass, and which the gate insists on before it arms.
void aSourceWorthLockingTo(VideoSourceAcquisition &acquisition, unsigned passes)
{
    for (unsigned i = 0; i < passes; ++i)
        pollOnce(acquisition);
    acquisition.placeCoastWindow(0);
    REQUIRE(Tv5725::SyncProcessor::coastPlaced());
}

}  // namespace

TEST_CASE("the option being off is the first thing reported")
{
    SolvedEngine unit;
    FrameSync lock(unit.clock);
    FrameTimeLock gate(lock, unit.acquisition, unit.engine, unit.sampling);

    FrameTimeLock::Conditions conditions = allOpen();
    conditions.optionEnabled = false;

    CHECK(reason(gate.blockedBy(conditions, 100000)) == std::string("the option is off"));
}

TEST_CASE("a source that is not there blocks the lock")
{
    SolvedEngine unit;
    FrameSync lock(unit.clock);
    FrameTimeLock gate(lock, unit.acquisition, unit.engine, unit.sampling);

    FrameTimeLock::Conditions conditions = allOpen();
    conditions.sourcePresent = false;

    CHECK(reason(gate.blockedBy(conditions, 100000)) == std::string("no source"));
}

TEST_CASE("the sync watcher being off blocks the lock")
{
    SolvedEngine unit;
    FrameSync lock(unit.clock);
    FrameTimeLock gate(lock, unit.acquisition, unit.engine, unit.sampling);

    FrameTimeLock::Conditions conditions = allOpen();
    conditions.syncWatcherEnabled = false;

    CHECK(reason(gate.blockedBy(conditions, 100000)) == std::string("the sync watcher is off"));
}

TEST_CASE("video that routes around the scaler blocks the lock")
{
    // Pass-through drives the encoder from the source's own timing, so there is
    // no output frame time here to steer.
    SolvedEngine unit;
    unit.engine.setOutputMode(&ModeBypass);

    FrameSync lock(unit.clock);
    FrameTimeLock gate(lock, unit.acquisition, unit.engine, unit.sampling);

    CHECK(reason(gate.blockedBy(allOpen(), 100000))
          == std::string("the video bypasses the scaler"));
}

TEST_CASE("a lock with no coast window never arms")
{
    // The sync processor has to be coasting over the source's vertical
    // interval before a vsync period measured off it means anything.
    SolvedEngine unit;
    for (unsigned i = 0; i < FrameTimeLock::ArmPasses + 1u; ++i)
        pollOnce(unit.acquisition);
    REQUIRE_FALSE(SyncProcessor::coastPlaced());

    FrameSync lock(unit.clock);
    FrameTimeLock gate(lock, unit.acquisition, unit.engine, unit.sampling);

    CHECK(reason(gate.blockedBy(allOpen(), 100000)) == std::string("not armed: no coast window"));
}

TEST_CASE("a source that has only just been acquired is not armed against")
{
    // A correction measured across a settling source steers the output towards
    // a rate the source is about to leave.
    SolvedEngine unit;
    unit.acquisition.resolveFromSource();

    FrameSync lock(unit.clock);
    FrameTimeLock gate(lock, unit.acquisition, unit.engine, unit.sampling);

    REQUIRE(unit.acquisition.acquiredPasses() < FrameTimeLock::ArmPasses);
    CHECK(reason(gate.blockedBy(allOpen(), 100000))
          == std::string("not armed: the source has not held long enough"));
}

TEST_CASE("a lock something keeps disturbing never arms")
{
    SolvedEngine unit;
    aSourceWorthLockingTo(unit.acquisition, FrameTimeLock::ArmPasses + 1);

    FrameSync lock(unit.clock);
    FrameTimeLock gate(lock, unit.acquisition, unit.engine, unit.sampling);

    lock.defer(100000);

    CHECK(reason(gate.blockedBy(allOpen(), 100000 + FrameTimeLock::ArmQuietMs / 2))
          == std::string("not armed: something keeps disturbing the lock"));

    SUBCASE("and does arm once it is left alone") {
        CHECK(reason(gate.blockedBy(allOpen(), 100000 + FrameTimeLock::ArmQuietMs + 1))
              != std::string("not armed: something keeps disturbing the lock"));
    }
}

TEST_CASE("an armed lock paces itself between corrections")
{
    // The lock defers itself after each correction, so this is the answer for
    // most of the passes between two of them -- and it is reported to nobody,
    // because a state that alternates with running every second is the console
    // telling itself the time.
    SolvedEngine unit;
    aSourceWorthLockingTo(unit.acquisition, FrameTimeLock::HeldPasses + 2);

    FrameSync lock(unit.clock);
    FrameTimeLock gate(lock, unit.acquisition, unit.engine, unit.sampling);

    gate.service(allOpen(), 100000);
    REQUIRE(lock.ready());

    g_logLines.clear();
    gate.service(allOpen(), 100000 + FrameSync::LockIntervalMs / 2);

    CHECK_FALSE(loggedContaining("frame time lock"));
}

TEST_CASE("an armed lock on a held source runs, and says so once")
{
    SolvedEngine unit;
    aSourceWorthLockingTo(unit.acquisition, FrameTimeLock::HeldPasses + 2);

    FrameSync lock(unit.clock);
    FrameTimeLock gate(lock, unit.acquisition, unit.engine, unit.sampling);

    uint32_t now = 100000;
    for (int i = 0; i < 4; ++i) {
        gate.service(allOpen(), now);
        now += FrameSync::LockIntervalMs + 1;
    }

    CHECK(loggedContaining("frame time lock: running"));

    SUBCASE("and does not repeat itself while nothing changes") {
        unsigned said = 0;
        for (size_t i = 0; i < g_logLines.size(); ++i)
            if (g_logLines[i] == "frame time lock: running")
                ++said;
        CHECK(said == 1u);
    }
}

TEST_CASE("a board with no generator stretches the raster instead of the clock")
{
    // The internal PLL's rate cannot be slewed, so the only correction left is
    // the raster one -- which is what moves VDS_VSYNC_RST.
    SolvedEngine unit;
    aSourceWorthLockingTo(unit.acquisition, FrameTimeLock::HeldPasses + 2);

    FrameSync lock(unit.clock);
    REQUIRE_FALSE(lock.canSteerRate());
    FrameTimeLock gate(lock, unit.acquisition, unit.engine, unit.sampling);

    const uint16_t before = GBS::VDS_VSYNC_RST::read();

    uint32_t now = 100000;
    for (int i = 0; i < 6; ++i) {
        gate.service(allOpen(), now);
        now += FrameSync::LockIntervalMs + 1;
    }

    CHECK(GBS::VDS_VSYNC_RST::read() != before);
    CHECK(lock.lastCorrection() == FrameSync::Correction);
}

TEST_CASE("method 1 corrects the raster and leaves the vsync pulse alone")
{
    SolvedEngine unit;
    aSourceWorthLockingTo(unit.acquisition, FrameTimeLock::HeldPasses + 2);

    FrameSync lock(unit.clock);
    FrameTimeLock gate(lock, unit.acquisition, unit.engine, unit.sampling);

    const uint16_t pulseBefore = GBS::VDS_VS_ST::read();

    FrameTimeLock::Conditions conditions = allOpen();
    conditions.method = 1;

    uint32_t now = 100000;
    for (int i = 0; i < 6; ++i) {
        gate.service(conditions, now);
        now += FrameSync::LockIntervalMs + 1;
    }

    REQUIRE(lock.lastCorrection() == FrameSync::Correction);
    CHECK(GBS::VDS_VS_ST::read() == pulseBefore);
}
