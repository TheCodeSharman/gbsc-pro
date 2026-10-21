// Host-compiled unit tests for Tv5725::SyncMeasurement -- `make -C test sync-measurement`.
//
// The chip cannot report whether the source sends composite or separate sync:
// STATUS_SYNC_PROC_VSACT reports the path already configured, so a unit that
// lands on csync stays there. hasOwnVsync() moves the path and watches for V
// instead. docs/sync-type-selection.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <Arduino.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "RegistersWritten.h"

void tv5725Log(const char *) {}

using namespace Tv5725;

static int probeCalls = 0;
static SyncMeasurement::Answer probeAnswer = SyncMeasurement::NoOwnVsync;

static SyncMeasurement::Answer countingProbe()
{
    ++probeCalls;
    return probeAnswer;
}

// STATUS_SYNC_PROC_HSACT, which is 1 on every source the probe can be asked
// about -- 1 on the bench RISC PC's separate sync and 1 on the Wii's sync on
// green, where VSACT is 0 -- and 0 when nothing is arriving at all.
static void givenHsync(bool arriving)
{
    const uint8_t bit = 1;   // STATUS_SYNC_PROC_HSACT, s0 0x16[1]
    uint8_t raw = Wire.bank[0][0x16];
    raw = (uint8_t)((raw & ~(1u << bit)) | ((arriving ? 1u : 0u) << bit));
    Wire.bank[0][0x16] = raw;
}

static void given(bool sourceHasOwnVsync)
{
    SyncMeasurement::forget();
    SyncMeasurement::set(false);
    probeCalls = 0;
    probeAnswer = sourceHasOwnVsync ? SyncMeasurement::OwnVsync
                                    : SyncMeasurement::NoOwnVsync;
    Wire.reset();
    givenHsync(true);
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

TEST_CASE("a probe taken while no sync is arriving is not latched")
{
    // A SOURCE THAT IS ABSENT ANSWERS THE PROBE EXACTLY AS A COMPOSITE ONE
    // DOES, because both fail to produce a V sync line and the probe concludes
    // by absence. Latching that holds a separate-sync source on the csync path
    // for the life of the selection: the separator stays in circuit, hsync is
    // sought on green where the RISC PC puts none, and nothing re-probes.
    //
    // Measured across a Ctrl-Break reboot of the bench RISC PC on separate
    // sync: the probe ran 10 ms after the source left, read no V sync in
    // 1001 ms, and the engine hunted a reference clock until /sc?~ forgot the
    // answer, with PAD_SYNC_OUT_ENZ left 1 so nothing was emitted at all.
    given(false);
    probeAnswer = SyncMeasurement::NoSource;

    SyncMeasurement::syncType(countingProbe);

    CHECK_FALSE(SyncMeasurement::isSet());
}

TEST_CASE("an unlatched probe is taken again on the next pass")
{
    given(false);
    probeAnswer = SyncMeasurement::NoSource;
    SyncMeasurement::syncType(countingProbe);

    probeAnswer = SyncMeasurement::NoOwnVsync;
    SyncMeasurement::syncType(countingProbe);

    CHECK(probeCalls == 2);
    CHECK(SyncMeasurement::isSet());
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

    probeAnswer = SyncMeasurement::OwnVsync;
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

// What the separator extracts once the probe puts it in, read back through
// STATUS_SYNC_PROC_VTOTAL. A plausible frame is a composite source; a count no
// source runs is one that is simply not there yet.
static void separatorFinds(uint16_t lines)
{
    const uint16_t raw = lines == 0 ? 0 : (uint16_t)(lines - 1);
    Wire.bank[0][0x1B] = (uint8_t)(raw & 0xFF);
    Wire.bank[0][0x1C] = (uint8_t)((Wire.bank[0][0x1C] & 0xF8) | (raw >> 8));
}

TEST_CASE("a source the separator finds no frame in is not called composite")
{
    // THE REBOOT CASE. A source between modes drives H with no V, exactly as a
    // composite source does, so concluding by absence holds a separate-sync
    // source on the csync path until something forgets the answer. Measured
    // across a Ctrl-Break of the bench RISC PC: the probe fired 40 ms after the
    // source left and the counts it was offered were 313 at 0 Hz, 938, 617 --
    // none of them a frame.
    vsyncArrivesAt(Never);
    separatorFinds(938);
    Wire.drift(0, 0x1B);   // a count that is never twice the same

    CHECK(SyncMeasurement::hasOwnVsync(testClock) == SyncMeasurement::NoSource);
}

TEST_CASE("a source with its own V sync answers without waiting out a settle")
{
    // **THE SETTLE IS PURE COST IN THE COMMON CASE.** The probe used to delay
    // OwnVsyncSettleMs blind before looking, so a source whose V sync is
    // already there paid the whole of it. Measured on the bench on `vga`,
    // VSACT timed from the instant applyForSyncType() moves the path: high at
    // 0 ms on three of four probes, and 294 ms on the first after a detection
    // cycle. The poll's own budget covers that tail, so nothing needs waiting
    // out in front of it.
    vsyncArrivesAt(0);
    settledMs() = 0;

    REQUIRE(SyncMeasurement::hasOwnVsync(testClock) == SyncMeasurement::OwnVsync);

    CHECK(settledMs() <= 20);
}

TEST_CASE("a V sync that arrives late is still the source's own")
{
    // THE TAIL IS THE SYNC PROCESSOR REACQUIRING, NOT THE SOURCE. Timed from
    // the moment the path moves, VSACT is high at 0 ms on three probes of four
    // and 294 ms on the fourth -- the first after a detection cycle. The
    // earlier distribution, taken 240 ms into a settle, reaches 443 ms by the
    // same clock. OwnVsyncWindowMs covers that.
    vsyncArrivesAt(400);
    CHECK(SyncMeasurement::hasOwnVsync(testClock) == SyncMeasurement::OwnVsync);
}

TEST_CASE("a separate-sync source whose V misses the window is not called composite")
{
    // **WHAT MAKES THE WINDOW SAFE TO BOUND.** A premature no used to latch
    // composite separation onto a separate-sync source and collapse
    // SP_VTOTAL. It cannot now: the negative branch puts the separator in and
    // asks for a frame, and a separate-sync source yields 97 rather than a
    // count any source runs -- so the answer is NoSource, which latches
    // nothing and is probed again on the next pass.
    vsyncArrivesAt(Never);
    separatorFinds(97);

    CHECK(SyncMeasurement::hasOwnVsync(testClock) == SyncMeasurement::NoSource);
}

TEST_CASE("a source with no V sync of its own is not given one")
{
    // The other half, and what stops the window above from becoming "always
    // yes": on composite sync no V arrives on its own pin, and the separator
    // put in afterwards extracts the frame that says so.
    vsyncArrivesAt(Never);
    separatorFinds(312);

    CHECK(SyncMeasurement::hasOwnVsync(testClock) == SyncMeasurement::NoOwnVsync);
}

static const uint8_t SOG_MODE_BIT = 0x01;       // s5_56[0]

// Whether the separator was ever in the path on a pass of the poll. Sampled
// from the clock because that is the only thing that runs WHILE the probe is
// reading, and what the probe leaves behind afterwards is a different question.
static bool g_separatorSeenIn = false;
static uint32_t watchingClock()
{
    if (Wire.bank[5][0x56] & SOG_MODE_BIT)
        g_separatorSeenIn = true;
    return testClock();
}

TEST_CASE("the probe takes the separator out of the path before it reads")
{
    // **STATUS_SYNC_PROC_VSACT REPORTS THE PATH IN FORCE, NOT THE SOURCE.** With
    // the separator in circuit it reads 0 whatever the source drives, so a probe
    // that clears SP_EXT_SYNC_SEL alone can only ever answer "no" -- and that
    // answer holds a separate-sync source as composite, which latches.
    //
    // Measured on the bench, the RiscPC at 320x256@50 with SYNC 0 and confirmed
    // emitting, after a session on a component input: `own V sync: no` on every
    // attempt with SP_SOG_MODE 1, where the same source and mode answered
    // `yes after 2ms` from a boot on which the separator had never been
    // enabled. docs/known-issues.md
    vsyncArrivesAt(10);
    Wire.bank[5][0x56] |= SOG_MODE_BIT;
    g_separatorSeenIn = false;

    SyncMeasurement::hasOwnVsync(watchingClock);

    CHECK_FALSE(g_separatorSeenIn);
}

TEST_CASE("the separator the probe took out goes back")
{
    // A source genuinely on composite separation is left off it otherwise, and
    // the probe runs on a source that is working. The arrangement HELD is what
    // it goes back to: the held answer is the authority on which path is in
    // force, not the register the probe happened to find.
    vsyncArrivesAt(10);
    SyncMeasurement::set(true);

    SyncMeasurement::hasOwnVsync(testClock);

    CHECK((Wire.bank[5][0x56] & SOG_MODE_BIT) == SOG_MODE_BIT);
}

TEST_CASE("the sync path the probe borrowed goes back")
{
    // The probe measures in the separate-sync arrangement, so a source already
    // on composite separation is left off it if this does not restore. The
    // arrangement HELD is what it goes back to, not the register value found:
    // the held answer is the authority on which path is in force.
    vsyncArrivesAt(10);
    SyncMeasurement::set(true);
    SyncMeasurement::hasOwnVsync(testClock);
    CHECK((Wire.bank[5][0x20] & EXT_SYNC_SEL_BIT) == EXT_SYNC_SEL_BIT);
}


TEST_CASE("the probe answers for the disturbance its own write made")
{
    // Moving SP_EXT_SYNC_SEL takes the sync separator in and out of the path,
    // and the chip latches that as a source disturbance exactly as it latches
    // the source's own. Left set it arms a re-measure once the change it ran
    // for has solved, on a count that solve has just measured -- measured on
    // the bench leaving composite sync, a second solve 0.3 s after the first.
    // docs/investigations/the-reference-clock-is-applied-to-a-working-picture.md
    vsyncArrivesAt(10);
    SyncMeasurement::hasOwnVsync(testClock);
    CHECK(Wire.touched[0][0x58]);
}

TEST_CASE("forgetting the sync type forgets the answer too, not just the arming")
{
    // **THE VALUE OUTLIVED THE VERDICT.** forget() cleared only the arming, so
    // isCsync() went on returning the last answer to every reader that does not
    // go through syncType() -- applyPresets() among them, which writes the whole
    // sync path from it. Two resets papered over that by calling set(false)
    // beside forget(), which is a reset guessing at a measurement and made each
    // of them a second owner of the held answer.
    SyncMeasurement::forget();
    SyncMeasurement::set(true);
    REQUIRE(SyncMeasurement::isCsync());

    SyncMeasurement::forget();

    CHECK_FALSE(SyncMeasurement::isCsync());
    CHECK_FALSE(SyncMeasurement::isSet());
}
