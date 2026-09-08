// Host-compiled unit tests for SyncProcessor::applyForSyncType() --
// `make -C test sync-processor`.
//
// Fields are read back through their own typedefs, never through a hand-written
// address: a wrong address does not error, it returns a plausible number.
//
// "Not written" is proved by running under two COMPLEMENTARY poisons and
// checking the two disagree. One poison cannot tell a field written 0 from a
// field left at a poison whose bit is already 0.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/gbs_types.h"

using namespace Tv5725;

static const uint8_t Poisons[2] = {0xA5, 0x5A};

template <typename Field>
static uint32_t applied(bool csync)
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyForSyncType(csync);
    return Field::read();
}

template <typename Field>
static bool wasWritten(bool csync)
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        SyncProcessor::applyForSyncType(csync);
        under[i] = Field::read();
    }
    return under[0] == under[1];
}

TEST_CASE("separate sync runs off the source's own H and V, uncoasted")
{
    const bool csync = false;

    CHECK(applied<SyncProcessor::SP_SOG_SRC_SEL>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_EXT_SYNC_SEL>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_SOG_MODE>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_NO_COAST_REG>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_PRE_COAST>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_POST_COAST>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_H_PULSE_IGNOR>(csync) == 0xff);
    CHECK(applied<SyncProcessor::SP_SYNC_BYPS>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_HS_POL_ATO>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_VS_POL_ATO>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_HS_LOOP_SEL>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_H_PROTECT>(csync) == 0);
}

TEST_CASE("csync coasts around the vertical interval and protects the line")
{
    const bool csync = true;

    CHECK(applied<SyncProcessor::SP_SOG_SRC_SEL>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_EXT_SYNC_SEL>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_SOG_MODE>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_NO_COAST_REG>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_PRE_COAST>(csync) == 4);
    CHECK(applied<SyncProcessor::SP_POST_COAST>(csync) == 7);
    CHECK(applied<SyncProcessor::SP_SYNC_BYPS>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_HS_LOOP_SEL>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_H_PROTECT>(csync) == 1);
}

TEST_CASE("the three fields csync never wrote are still not written")
{
    // Adding a write here would be a behaviour change wearing the clothes of a
    // move. The separate-sync branch owns all three.
    CHECK_FALSE(wasWritten<SyncProcessor::SP_HS_POL_ATO>(true));
    CHECK_FALSE(wasWritten<SyncProcessor::SP_VS_POL_ATO>(true));
    CHECK_FALSE(wasWritten<SyncProcessor::SP_H_PULSE_IGNOR>(true));

    CHECK(wasWritten<SyncProcessor::SP_HS_POL_ATO>(false));
    CHECK(wasWritten<SyncProcessor::SP_VS_POL_ATO>(false));
    CHECK(wasWritten<SyncProcessor::SP_H_PULSE_IGNOR>(false));
}

TEST_CASE("sync-on-green is enabled on both paths")
{
    // Tv5725::Adc's field, written by both copies this replaced, so it travels
    // with them rather than being left behind.
    CHECK(applied<Adc::ADC_SOGEN>(false) == 1);
    CHECK(applied<Adc::ADC_SOGEN>(true) == 1);
}

TEST_CASE("the SD vertical sync positions are one value each, not two halves")
{
    // SP_SDCS_VSST and SP_SDCS_VSSP are 11 bits split across a low byte and a
    // three-bit high field in a different register. Written as halves they
    // drift: setOutModeHdBypass() sets a start of 301, so the high field holds
    // 1, and a later path writing only the low byte with 2 leaves 258.
    Wire.reset();
    Wire.poison(0x00);

    SyncProcessor::writeSdVsyncStart(301);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 1);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 45);

    SUBCASE("a smaller value afterwards clears the high field") {
        SyncProcessor::writeSdVsyncStart(2);
        CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 0);
        CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 2);
    }

    SUBCASE("the stop position is the same shape") {
        SyncProcessor::writeSdVsyncStop(520);
        CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 2);
        CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 8);

        SyncProcessor::writeSdVsyncStop(0);
        CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
        CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 0);
    }
}

// SP_EXT_SYNC_SEL: 0 takes H and V from the dedicated pins, 1 leaves the sync
// processor on composite or sync-on-green. It travels with the input choice, so
// it is set from an InputSource row rather than from the sync type.

template <typename Field>
static uint32_t afterExternalSync(uint8_t sel)
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::selectExternalSync(sel);
    return Field::read();
}

template <typename Field>
static bool externalSyncWrote(uint8_t sel)
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        SyncProcessor::selectExternalSync(sel);
        under[i] = Field::read();
    }
    return under[0] == under[1];
}

TEST_CASE("the external sync select carries the input's choice")
{
    CHECK(afterExternalSync<SyncProcessor::SP_EXT_SYNC_SEL>(0) == 0);
    CHECK(afterExternalSync<SyncProcessor::SP_EXT_SYNC_SEL>(1) == 1);
}

TEST_CASE("choosing the external sync touches nothing the sync type owns")
{
    CHECK(externalSyncWrote<SyncProcessor::SP_EXT_SYNC_SEL>(1));

    CHECK_FALSE(externalSyncWrote<SyncProcessor::SP_SOG_MODE>(1));
    CHECK_FALSE(externalSyncWrote<SyncProcessor::SP_PRE_COAST>(1));
    CHECK_FALSE(externalSyncWrote<SyncProcessor::SP_POST_COAST>(1));
    CHECK_FALSE(externalSyncWrote<SyncProcessor::SP_SOG_SRC_SEL>(1));
}

// The coast window the sketch reaches for whenever it is starting over: on a
// preset load, on a new mode, and twice inside the no-sync escalation. It was
// written out by hand at every one of those, which is five copies of one fact.
TEST_CASE("the default coast window is one operation, not a pair of literals")
{
    Wire.reset();
    Wire.poison(Poisons[0]);

    SyncProcessor::applyDefaultCoastWindow();

    CHECK(SyncProcessor::SP_H_CST_ST::read() == 0x10);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 0x100);
}

TEST_CASE("the default coast window leaves the coast lengths alone")
{
    // It says where in the line to coast, not how long around the vertical
    // interval to do it -- those follow the sync type, and a caller starting
    // the window over must not silently undo them.
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyForSyncType(true);
    const uint32_t pre = SyncProcessor::SP_PRE_COAST::read();
    const uint32_t post = SyncProcessor::SP_POST_COAST::read();

    SyncProcessor::applyDefaultCoastWindow();

    CHECK(SyncProcessor::SP_PRE_COAST::read() == pre);
    CHECK(SyncProcessor::SP_POST_COAST::read() == post);
}

// Widening the coast, the escalation a source whose sync has gone reaches
// before anything is reset. Serrated sync puts equalisation pulses either side
// of the vertical interval, so the coast has to cover more lines and the
// separator has to ignore fewer short pulses to find the real ones.

TEST_CASE("widening the coast covers more lines either side of the interval")
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyForSyncType(true);

    SyncProcessor::widenCoastForSerration();

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 9);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 9);
}

TEST_CASE("a pulse-ignore wide enough to hide a real pulse is halved")
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::SP_H_PULSE_IGNOR::write(0x6b);

    SyncProcessor::widenCoastForSerration();

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x6b / 2);
}

TEST_CASE("a pulse-ignore already narrow is left where it is")
{
    // Halving it again reaches a width that lets ringing through as sync.
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::SP_H_PULSE_IGNOR::write(0x32);

    SyncProcessor::widenCoastForSerration();

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x32);
}

// Resetting the sync processor. A pulse, so the final state proves nothing and
// the fake's write trace is the assertion: a block never taken low was never
// reset.

static bool wasPulsedLow(uint8_t seg, uint8_t reg, uint8_t bit)
{
    bool wentLow = false;
    for (size_t i = 0; i < Wire.trace.size(); ++i) {
        const FakeTwoWire::Traced &t = Wire.trace[i];
        if (t.segment != seg || t.reg != reg)
            continue;
        if ((t.value & (1u << bit)) == 0)
            wentLow = true;
        else if (wentLow)
            return true;
    }
    return false;
}

TEST_CASE("resetting the sync processor takes the block low and brings it back")
{
    Wire.reset();
    Chip::SFTRST_SYNC_RSTZ::write(1);
    Wire.trace.clear();

    SyncProcessor::reset();

    CHECK(wasPulsedLow(0x00, 0x47, 2));
    CHECK(Chip::SFTRST_SYNC_RSTZ::read() == 1);
}

TEST_CASE("toggling the H counter's overflow protection flips it and flips back")
{
    // The ladder has nothing to measure it against, so it tries the other
    // setting periodically. That only works if the toggle is a toggle.
    Wire.reset();
    SyncProcessor::SP_H_PROTECT::write(0);

    SyncProcessor::toggleHsyncOverflowProtect();
    CHECK(SyncProcessor::SP_H_PROTECT::read() == 1);

    SyncProcessor::toggleHsyncOverflowProtect();
    CHECK(SyncProcessor::SP_H_PROTECT::read() == 0);
}

// The coast window: where in the line the sync processor stops counting, taken
// from the source's own line length. HPERIOD_IF counts against the chip's 27 MHz
// and the window is placed as a fraction of it.

static bool g_stable = true;
static unsigned g_stableCalls = 0;
static bool stableStub() { ++g_stableCalls; return g_stable; }

// A source holding a 15625 Hz line still. HPERIOD_IF 431 is what that mode reads
// when the register is behaving.
static void steadyLine(uint16_t hperiod)
{
    Wire.reset();
    g_stable = true;
    g_stableCalls = 0;
    GBS::HPERIOD_IF::write(hperiod);
}

TEST_CASE("the coast window is placed as a fraction of the source's own line")
{
    steadyLine(431);

    CHECK(SyncProcessor::acquireCoastWindow(false, stableStub));
    CHECK(SyncProcessor::SP_H_CST_ST::read() == 0x10);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 1668);
    CHECK(SyncProcessor::SP_HCST_AUTO_EN::read() == 0);
}

TEST_CASE("the automatic window brackets the sync rather than spanning the line")
{
    steadyLine(431);

    CHECK(SyncProcessor::acquireCoastWindow(true, stableStub));
    CHECK(SyncProcessor::SP_H_CST_ST::read() == 96);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 267);
    CHECK(SyncProcessor::SP_HCST_AUTO_EN::read() == 1);
}

TEST_CASE("a line length that will not hold still leaves the window alone")
{
    // HPERIOD_IF's high byte moving under the reader is the railing register:
    // every sample lands a long way from the last, and a window placed on the
    // mean of those is a window placed on nothing.
    steadyLine(431);
    Wire.drift(0x00, 0x07);

    CHECK_FALSE(SyncProcessor::acquireCoastWindow(false, stableStub));
    CHECK_FALSE(Wire.touched[0x05][0x4D]);
    CHECK_FALSE(Wire.touched[0x05][0x4F]);
}

TEST_CASE("an unstable sync processor leaves the window alone")
{
    steadyLine(431);
    g_stable = false;

    CHECK_FALSE(SyncProcessor::acquireCoastWindow(false, stableStub));
    CHECK_FALSE(Wire.touched[0x05][0x4D]);
    CHECK_FALSE(Wire.touched[0x05][0x4F]);
}

TEST_CASE("a line too short to place a window in writes nothing")
{
    // A collapsed horizontal measurement with no plausible vertical count
    // beside it. A window at a fraction of nothing is worse than the one
    // already in force.
    steadyLine(4);
    GBS::STATUS_SYNC_PROC_VTOTAL::write(400);

    CHECK_FALSE(SyncProcessor::acquireCoastWindow(false, stableStub));
    CHECK_FALSE(Wire.touched[0x05][0x4D]);
}

TEST_CASE("a collapsed line under a countable source is treated as a long one")
{
    // The horizontal measurement has gone while the vertical is still counting,
    // so the source is there and it is HPERIOD_IF that failed. A long window is
    // a better guess than one placed on the collapsed reading.
    steadyLine(4);
    GBS::STATUS_SYNC_PROC_VTOTAL::write(311);

    CHECK(SyncProcessor::acquireCoastWindow(false, stableStub));
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 1936);
}

// The clamp window: where in the line the black level is sampled. It has to
// land in the back porch -- after the sync pulse, before active video -- and
// the two sync paths measure the line in different units to find it.

TEST_CASE("the clamp sits in the back porch of a separate-sync line")
{
    // Separate sync counts in ADC samples, so STATUS_SYNC_PROC_HTOTAL is the
    // line and the fractions are of that.
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HTOTAL::write(2250);

    CHECK(SyncProcessor::acquireClampWindow(false, false, 0, stableStub));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 23);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 132);
}

TEST_CASE("a composite source is measured in its own units")
{
    // HPERIOD_IF counts against the chip's 27 MHz rather than the ADC clock, so
    // the same window is a different fraction of a different number.
    steadyLine(431);

    CHECK(SyncProcessor::acquireClampWindow(true, false, 0, stableStub));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 14);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 76);
}

TEST_CASE("a component source clamps later, and stops where the others do")
{
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HTOTAL::write(2250);

    CHECK(SyncProcessor::acquireClampWindow(false, true, 0, stableStub));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 73);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 132);
}

TEST_CASE("an offset moves the whole window later")
{
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HTOTAL::write(2250);

    CHECK(SyncProcessor::acquireClampWindow(false, true, 0x60, stableStub));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 73 + 0x60);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 132 + 0x60);
}

TEST_CASE("a window already within a unit of where it belongs is not rewritten")
{
    // One bus write per pass, for a window that has not moved, on a function
    // the watcher calls on a schedule.
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HTOTAL::write(2250);
    SyncProcessor::SP_CS_CLP_ST::write(24);
    SyncProcessor::SP_CS_CLP_SP::write(133);

    CHECK(SyncProcessor::acquireClampWindow(false, false, 0, stableStub));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 24);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 133);
}

TEST_CASE("a line length that will not hold still leaves the clamp alone")
{
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HTOTAL::write(2250);
    Wire.drift(0x00, 0x18);

    CHECK_FALSE(SyncProcessor::acquireClampWindow(false, false, 0, stableStub));
    CHECK_FALSE(Wire.touched[0x05][0x41]);
    CHECK_FALSE(Wire.touched[0x05][0x43]);
}

// The sync separation thresholds a source with no broadcast vertical interval
// wants: how different a pulse must be to read as vertical, and how short one
// must be to be ignored.

TEST_CASE("a source with its own H and V is coasted over nothing and ignores nothing")
{
    Wire.reset();
    SyncProcessor::applySeparationThresholds(false);

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 0x00);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 0x00);
    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0xff);
    CHECK(SyncProcessor::SP_DLT_REG::read() == 0x00);
}

TEST_CASE("a composite source is coasted over its vertical interval")
{
    Wire.reset();
    SyncProcessor::applySeparationThresholds(true);

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 0x04);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 0x07);
    CHECK(SyncProcessor::SP_DLT_REG::read() == 0x70);
    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x02);
}

// Putting the sync path back for a scaling RGBHV source, after a preset written
// for another standard has moved it.

TEST_CASE("a composite-sync scaling RGBHV source coasts on its own window")
{
    Wire.reset();
    Wire.poison(Poisons[0]);

    SyncProcessor::applyForScalingRgbhv(true);

    CHECK(SyncProcessor::SP_SOG_MODE::read() == 1);
    CHECK(SyncProcessor::SP_H_CST_ST::read() == 0x10);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 0x80);
    CHECK(SyncProcessor::SP_H_PROTECT::read() == 1);
}

TEST_CASE("that window is NARROWER than the default and does not substitute")
{
    // 0x80 against applyDefaultCoastWindow()'s 0x100. The two look alike and
    // are not the same operation: swapping one for the other moves where the
    // sync processor stops coasting by half a window.
    Wire.reset();
    SyncProcessor::applyForScalingRgbhv(true);
    const uint32_t scaling = SyncProcessor::SP_H_CST_SP::read();

    Wire.reset();
    SyncProcessor::applyDefaultCoastWindow();

    CHECK(scaling != SyncProcessor::SP_H_CST_SP::read());
}

TEST_CASE("a separate-sync scaling RGBHV source runs uncoasted and clamps by hand")
{
    Wire.reset();
    Wire.poison(Poisons[0]);

    SyncProcessor::applyForScalingRgbhv(false);

    CHECK(SyncProcessor::SP_SOG_MODE::read() == 0);
    CHECK(SyncProcessor::SP_CLAMP_MANUAL::read() == 1);
    CHECK(SyncProcessor::SP_NO_COAST_REG::read() == 1);
}

TEST_CASE("the separate-sync arm leaves the coast window where it was")
{
    Wire.reset();

    SyncProcessor::applyForScalingRgbhv(false);

    CHECK_FALSE(Wire.touched[0x05][0x4D]);
    CHECK_FALSE(Wire.touched[0x05][0x4F]);
}
