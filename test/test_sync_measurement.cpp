// Host-compiled unit tests for Tv5725::SyncMeasurement -- `make -C test sync-measurement`.
//
// The chip cannot report whether the source sends composite or separate sync:
// STATUS_SYNC_PROC_VSACT reports the path already configured, so a unit that
// lands on csync stays there. hasOwnVsync() moves the path and watches for V
// instead. docs/sync-type-selection.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncMeasurement.h"

void tv5725Log(const char *) {}

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
    SyncMeasurement::forget();
    SyncMeasurement::set(false);
    probeCalls = 0;
    probeAnswer = sourceHasOwnVsync;
}

TEST_CASE("a source with its own vsync is not composite sync")
{
    given(true);
    SyncMeasurement::syncType(countingProbe);
    CHECK_FALSE(SyncMeasurement::isCsync());
}

TEST_CASE("a source without its own vsync is composite sync")
{
    given(false);
    SyncMeasurement::syncType(countingProbe);
    CHECK(SyncMeasurement::isCsync());
}

TEST_CASE("the probe runs once per source, not once per mode change")
{
    given(false);

    SyncMeasurement::syncType(countingProbe);
    SyncMeasurement::syncType(countingProbe);
    SyncMeasurement::syncType(countingProbe);

    CHECK(probeCalls == 1);
}

TEST_CASE("forgetting re-arms the probe")
{
    given(false);
    SyncMeasurement::syncType(countingProbe);
    CHECK(probeCalls == 1);

    SyncMeasurement::forget();
    SyncMeasurement::syncType(countingProbe);
    CHECK(probeCalls == 2);
}

TEST_CASE("a forced probe runs whether or not the answer is already held")
{
    given(false);
    SyncMeasurement::syncType(countingProbe);
    SyncMeasurement::probe(countingProbe);

    CHECK(probeCalls == 2);
}

TEST_CASE("a forced probe leaves the answer held, so the next mode change pays nothing")
{
    given(false);
    SyncMeasurement::probe(countingProbe);

    CHECK(SyncMeasurement::isSet());
    SyncMeasurement::syncType(countingProbe);
    CHECK(probeCalls == 1);
}

TEST_CASE("setting the type by hand does not claim the source was probed")
{
    given(false);

    // The YPbPr fallback and the two temporary flips during detection assert a
    // type without measuring one. Marking that as probed would suppress the
    // real probe for the rest of the source.
    SyncMeasurement::set(true);

    CHECK(SyncMeasurement::isCsync());
    CHECK_FALSE(SyncMeasurement::isSet());
}

TEST_CASE("a set value survives until something probes or sets again")
{
    given(false);
    SyncMeasurement::set(true);
    CHECK(SyncMeasurement::isCsync());

    probeAnswer = true;  // the source does have its own vsync
    SyncMeasurement::syncType(countingProbe);
    CHECK_FALSE(SyncMeasurement::isCsync());
}

// --- the own-V-sync probe ---------------------------------------------------
//
// A clock the test owns, advancing the way the probe's delay(2) does, and
// raising VSACT once it reaches the arrival time. The bit has to change WHILE
// the probe is polling, which a static fake register cannot express.

static const uint8_t VSACT_BIT = 0x08;      // s0_16[3]
static const uint8_t EXT_SYNC_SEL_BIT = 0x08;   // s5_20[3]

static uint32_t g_nowMs = 0;
static uint32_t g_vsyncArrivesAtMs = 0;

static uint32_t testClock()
{
    g_nowMs += 2;
    if (g_nowMs >= g_vsyncArrivesAtMs) {
        Wire.bank[0][0x16] |= VSACT_BIT;
    }
    return g_nowMs;
}

static void vsyncArrivesAt(uint32_t ms)
{
    Wire.reset();
    g_nowMs = 0;
    g_vsyncArrivesAtMs = ms;
}

// Larger than any arrival, so the bit never comes up.
static const uint32_t Never = 0xFFFFFFFFu;

TEST_CASE("a V sync that arrives late is still the source's own")
{
    // The tail of the reacquisition time reaches 242 ms where the typical case
    // is 2-3. A probe that stops looking inside that tail calls a separate-sync
    // source composite, which latches: SP_VTOTAL collapses to 97 and the picture
    // goes. docs/investigations/own-vsync-probe-window.md
    vsyncArrivesAt(400);
    CHECK(SyncMeasurement::hasOwnVsync(testClock) == true);
}

TEST_CASE("a source with no V sync of its own is not given one")
{
    // The other half, and what stops the window above from becoming "always
    // yes": on composite sync the timeout IS the correct answer.
    vsyncArrivesAt(Never);
    CHECK(SyncMeasurement::hasOwnVsync(testClock) == false);
}

TEST_CASE("the sync path the probe borrowed goes back")
{
    // The probe answers by MOVING SP_EXT_SYNC_SEL, so a source already on
    // composite separation is left off it if this does not restore.
    vsyncArrivesAt(10);
    Wire.bank[5][0x20] |= EXT_SYNC_SEL_BIT;
    SyncMeasurement::hasOwnVsync(testClock);
    CHECK((Wire.bank[5][0x20] & EXT_SYNC_SEL_BIT) == EXT_SYNC_SEL_BIT);
}

