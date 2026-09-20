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

// The clamp needs enough samples to establish a level and no more. That is a
// settling time, not a property of the line's geometry, so it does not scale
// with the divider: written as a fraction it grows toward active video exactly
// when there is no reason to. Measured on the bench, RiscPC 640x480@60 at
// divider 1566 -- a back porch of about 120 samples -- a window of 16..92 left
// the picture discoloured and 16..20 cleared it.
TEST_CASE("the clamp window is a settling time, not a fraction of the line")
{
    const uint16_t shortLine = 1566, longLine = 2506;

    const uint16_t narrow = (uint16_t)(SyncProcessor::clampStopFor(shortLine, false, false)
                                       - SyncProcessor::clampStartFor(shortLine, false, false));
    const uint16_t wide = (uint16_t)(SyncProcessor::clampStopFor(longLine, false, false)
                                     - SyncProcessor::clampStartFor(longLine, false, false));

    CHECK(narrow == wide);
    CHECK(narrow <= 8);

    SUBCASE("and it still starts where the line puts it, so the sync edge is cleared") {
        CHECK(SyncProcessor::clampStartFor(longLine, false, false)
              > SyncProcessor::clampStartFor(shortLine, false, false));
    }

    SUBCASE("composite sync starts later, where its own edge structure ends") {
        CHECK(SyncProcessor::clampStartFor(shortLine, true, false)
              > SyncProcessor::clampStartFor(shortLine, false, false));
        CHECK(SyncProcessor::clampStopFor(shortLine, true, false)
              - SyncProcessor::clampStartFor(shortLine, true, false) == narrow);
    }
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
    CHECK(applied<SyncProcessor::SP_PRE_COAST>(csync) == 7);
    CHECK(applied<SyncProcessor::SP_POST_COAST>(csync) == 3);
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
// it is set from an VideoSourceSelection row rather than from the sync type.

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

// A source holding a 15625 Hz line still, with the sync processor counting it.
// HPERIOD_IF 431 is what that mode reads when the register is behaving.
static void steadyLine(uint16_t hperiod)
{
    Wire.reset();
    GBS::STATUS_SYNC_PROC_HSACT::write(1);
    GBS::HPERIOD_IF::write(hperiod);
}

// THE LINE COMES FROM THE RATE THE ENGINE SOLVED, NOT FROM HPERIOD_IF. The
// register reports the same quantity -- the source's line in the chip's own
// 27 MHz counts -- and it rails, so a window placed from it is placed on
// nothing exactly when the source most needs coasting. The two agree where the
// register is healthy: 15625 Hz gives 1728, and a reading of 431 is
// (431 + 1) x 4 = 1728.
static const uint32_t BenchLineRateHz = 15625;

TEST_CASE("the coast window is placed as a fraction of the source's own line")
{
    steadyLine(431);

    CHECK(SyncProcessor::acquireCoastWindow(false, BenchLineRateHz));
    CHECK(SyncProcessor::SP_H_CST_ST::read() == 0x10);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 1672);
    CHECK(SyncProcessor::SP_HCST_AUTO_EN::read() == 0);
}

TEST_CASE("the automatic window brackets the sync rather than spanning the line")
{
    steadyLine(431);

    CHECK(SyncProcessor::acquireCoastWindow(true, BenchLineRateHz));
    CHECK(SyncProcessor::SP_H_CST_ST::read() == 97);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 267);
    CHECK(SyncProcessor::SP_HCST_AUTO_EN::read() == 1);
}

TEST_CASE("a source with no solved rate leaves the window alone")
{
    // Before the first solve there is no line to place a window on, and the one
    // already in force is a better guess than a fraction of nothing.
    steadyLine(431);

    CHECK_FALSE(SyncProcessor::acquireCoastWindow(false, 0));
    CHECK_FALSE(Wire.touched[0x05][0x4D]);
    CHECK_FALSE(Wire.touched[0x05][0x4F]);
}

TEST_CASE("an unstable sync processor leaves the window alone")
{
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HSACT::write(0);

    CHECK_FALSE(SyncProcessor::acquireCoastWindow(false, BenchLineRateHz));
    CHECK_FALSE(Wire.touched[0x05][0x4D]);
    CHECK_FALSE(Wire.touched[0x05][0x4F]);
}

TEST_CASE("a line too short to place a window in writes nothing")
{
    // A rate no source runs, which would put the whole window inside a handful
    // of counts.
    steadyLine(431);

    CHECK_FALSE(SyncProcessor::acquireCoastWindow(false, 1000000));
    CHECK_FALSE(Wire.touched[0x05][0x4D]);
}

// The clamp window: where in the line the black level is sampled. It has to
// land in the back porch -- after the sync pulse, before active video -- and
// the two sync paths measure the line in different units to find it.

TEST_CASE("the clamp sits in the back porch of a separate-sync line")
{
    // Separate sync counts in ADC samples, so STATUS_SYNC_PROC_HTOTAL is the
    // line and the fractions are of that.
    steadyLine(431);

    CHECK(SyncProcessor::acquireClampWindow(
        false, false, SyncProcessor::clampLineFor(false, BenchLineRateHz, 2250), 0));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 23);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 27);
}

TEST_CASE("a composite source is measured in its own units")
{
    // HPERIOD_IF counts against the chip's 27 MHz rather than the ADC clock, so
    // the same window is a different fraction of a different number.
    steadyLine(431);

    CHECK(SyncProcessor::acquireClampWindow(
        true, false, SyncProcessor::clampLineFor(true, BenchLineRateHz, 2250), 0));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 14);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 18);
}

TEST_CASE("a component source clamps later, and holds for the same span")
{
    steadyLine(431);

    CHECK(SyncProcessor::acquireClampWindow(
        false, true, SyncProcessor::clampLineFor(false, BenchLineRateHz, 2250), 0));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 73);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 77);
}

TEST_CASE("an offset moves the whole window later")
{
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HTOTAL::write(2250);

    CHECK(SyncProcessor::acquireClampWindow(
        false, true, SyncProcessor::clampLineFor(false, BenchLineRateHz, 2250), 0x60));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 73 + 0x60);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 77 + 0x60);
}

TEST_CASE("a window already within a unit of where it belongs is not rewritten")
{
    // One bus write per pass, for a window that has not moved, on a function
    // the watcher calls on a schedule.
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HTOTAL::write(2250);
    SyncProcessor::SP_CS_CLP_ST::write(24);
    SyncProcessor::SP_CS_CLP_SP::write(28);

    CHECK(SyncProcessor::acquireClampWindow(
        false, false, SyncProcessor::clampLineFor(false, BenchLineRateHz, 2250), 0));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 24);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 28);
}

TEST_CASE("a line length that will not hold still leaves the clamp alone")
{
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HTOTAL::write(2250);
    Wire.drift(0x00, 0x18);

    CHECK_FALSE(SyncProcessor::acquireClampWindow(false, false, 0, 0));
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

TEST_CASE("the composite coast pair counts a source's lines, not its serrations")
{
    // Coasted 4 lines before the vertical interval and 7 after, a 625-line
    // source's equalisation pulses fall inside the count and it measures 622.
    // Coasted 7 and 3 the same source measures 314.
    // docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md
    Wire.reset();
    Wire.poison(Poisons[0]);

    SyncProcessor::applyForSyncType(true);

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 7);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 3);
}

TEST_CASE("a composite source is coasted over its vertical interval")
{
    Wire.reset();
    SyncProcessor::applySeparationThresholds(true);

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 7);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 3);
    CHECK(SyncProcessor::SP_DLT_REG::read() >= 0x70);
    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() <= 0x0e);
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

TEST_CASE("coasting further for a serrated source leaves the pulse-ignore alone")
{
    // SP_H_PULSE_IGNOR has writers of its own. Widening the coast because a
    // source's serrations are being counted must not become another one.
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyForSyncType(true);
    SyncProcessor::SP_H_PULSE_IGNOR::write(107);

    SyncProcessor::widenCoast();

    const uint32_t pre = SyncProcessor::SerratedPreCoastLines;
    const uint32_t post = SyncProcessor::SerratedPostCoastLines;

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 107);
    CHECK(SyncProcessor::SP_PRE_COAST::read() > pre);
    CHECK(SyncProcessor::SP_POST_COAST::read() > post);
}

TEST_CASE("the pulse-width difference threshold clears the measured floor")
{
    // Below 0x70 a serrated source is miscounted -- 307 against 310 at 0x30,
    // and no lock at all at 0 -- while every value from 0x70 to 0xFF reads it
    // identically and a separate-sync source is indifferent across the whole
    // range. So one value serves both, with margin over the floor rather than
    // at it.
    // docs/investigations/the-pulse-ignore-value-is-measured-not-chosen.md
    Wire.reset();

    SyncProcessor::applyPulseWidthDifference();

    CHECK(SyncProcessor::SP_DLT_REG::read() >= 0x70);
}

// How short a horizontal pulse must be to be ignored. Three states, each
// measured, and no two of them interchangeable: the value that reads a serrated
// source stops a high-rate one locking at all.
// docs/investigations/the-pulse-ignore-value-is-measured-not-chosen.md

TEST_CASE("a source with its own vertical sync ignores every pulse")
{
    Wire.reset();
    SyncProcessor::applyPulseIgnore(false, false);

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0xff);
}

TEST_CASE("a serrated source needs the threshold above its equalisation pulses")
{
    // Measured on PAL 576i: 0x6B reads the source's 310 lines and every
    // smaller value tried reads 314 to 316, steadily and wrongly.
    Wire.reset();
    SyncProcessor::applyPulseIgnore(true, true);

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x6b);
}

TEST_CASE("a composite source without serrations needs a narrow threshold")
{
    // Measured at 40 kHz on composite sync: 0x02, 0x06 and 0x0E all read the
    // source, and 0x33 upwards does not lock at all. The serrated value is
    // among those that do not.
    Wire.reset();
    SyncProcessor::applyPulseIgnore(true, false);

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() <= 0x0e);
}

// Whether each window has been placed for the source in force. It is state, not
// a register: nothing on the chip says whether a window was measured or is left
// over from the source before.

TEST_CASE("placing the coast window records that it is placed")
{
    steadyLine(431);
    SyncProcessor::forgetPositions();
    REQUIRE_FALSE(SyncProcessor::coastPlaced());

    REQUIRE(SyncProcessor::acquireCoastWindow(false, BenchLineRateHz));

    CHECK(SyncProcessor::coastPlaced());
}

TEST_CASE("a window that could not be measured is not recorded as placed")
{
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HSACT::write(0);
    SyncProcessor::forgetPositions();

    REQUIRE_FALSE(SyncProcessor::acquireCoastWindow(false, BenchLineRateHz));

    CHECK_FALSE(SyncProcessor::coastPlaced());
}

TEST_CASE("a new source forgets both windows")
{
    steadyLine(431);
    SyncProcessor::forgetPositions();
    REQUIRE(SyncProcessor::acquireCoastWindow(false, BenchLineRateHz));
    REQUIRE(SyncProcessor::coastPlaced());

    SyncProcessor::forgetPositions();

    CHECK_FALSE(SyncProcessor::coastPlaced());
    CHECK_FALSE(SyncProcessor::clampPlaced());
}

// Configuring the separator to HUNT for a source, rather than to read one it
// has already found. The search runs when nothing is counting, so it asks for
// the settings most likely to see a pulse at all.

TEST_CASE("the search ignores only the shortest pulses")
{
    Wire.reset();

    SyncProcessor::applyForSearch(false);

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() <= 0x02);
}

TEST_CASE("the search separates on the threshold every source is read with")
{
    Wire.reset();
    SyncProcessor::applyPulseWidthDifference();
    const uint32_t settled = SyncProcessor::SP_DLT_REG::read();

    Wire.reset();
    SyncProcessor::applyForSearch(false);

    CHECK(SyncProcessor::SP_DLT_REG::read() == settled);
}

TEST_CASE("the search coasts on the default window and over no sub coast")
{
    Wire.reset();
    SyncProcessor::applyDefaultCoastWindow();
    const uint32_t start = SyncProcessor::SP_H_CST_ST::read();
    const uint32_t stop = SyncProcessor::SP_H_CST_SP::read();

    SyncProcessor::SP_H_CST_ST::write(0x77);
    SyncProcessor::SP_H_CST_SP::write(0x777);
    SyncProcessor::SP_H_COAST::write(1);

    SyncProcessor::applyForSearch(false);

    CHECK(SyncProcessor::SP_H_CST_ST::read() == start);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == stop);
    CHECK(SyncProcessor::SP_H_COAST::read() == 0);
}

TEST_CASE("a composite source coasts inverted while it is searched for")
{
    Wire.reset();
    SyncProcessor::setCoastInvert(false);

    SyncProcessor::applyForSearch(true);

    CHECK(SyncProcessor::SP_COAST_INV_REG::read() == 1);
}

TEST_CASE("a source with its own sync has its coast inversion left alone")
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        SyncProcessor::applyForSearch(false);
        under[i] = SyncProcessor::SP_COAST_INV_REG::read();
    }

    CHECK(under[0] != under[1]);
}

TEST_CASE("the search forgets where the windows were placed")
{
    steadyLine(431);
    SyncProcessor::forgetPositions();
    REQUIRE(SyncProcessor::acquireCoastWindow(false, BenchLineRateHz));
    REQUIRE(SyncProcessor::coastPlaced());

    SyncProcessor::applyForSearch(false);

    CHECK_FALSE(SyncProcessor::coastPlaced());
    CHECK_FALSE(SyncProcessor::clampPlaced());
}

TEST_CASE("the scaling RGBHV path takes the retiming module's auto polarity")
{
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        SyncProcessor::applyForScalingRgbhv(i == 0);

        CHECK(SyncProcessor::SP_SOG_P_ATO::read() == 1);
    }
}

TEST_CASE("the scaling RGBHV path puts the SD vertical sync at the top of the frame")
{
    Wire.reset();
    SyncProcessor::writeSdVsyncStart(301);
    SyncProcessor::writeSdVsyncStop(299);

    SyncProcessor::applyForScalingRgbhv(false);

    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 2);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
}

TEST_CASE("a scaling RGBHV source is a new source, so neither window is placed")
{
    steadyLine(431);
    SyncProcessor::forgetPositions();
    REQUIRE(SyncProcessor::acquireCoastWindow(false, BenchLineRateHz));
    REQUIRE(SyncProcessor::coastPlaced());

    SyncProcessor::applyForScalingRgbhv(false);

    CHECK_FALSE(SyncProcessor::coastPlaced());
    CHECK_FALSE(SyncProcessor::clampPlaced());
}

// Whether the bring-up writes a field at all. Two complementary poisons: one
// cannot tell a field written 0 from one left at a poison whose bits are
// already 0.
template <typename Field>
static bool bringUpWrites()
{
    uint32_t seen[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        SyncProcessor::init();
        seen[i] = Field::read();
    }
    return seen[0] == seen[1];
}

TEST_CASE("the bring-up leaves the retime stop to the class that holds the divider")
{
    // SP_RT_HS_SP is 93% of PLLAD_MD: one quantity in three registers, all
    // three written by SourceMeasurement off one held value. A second writer
    // here puts a window on the sync processor that the ADC clock does not
    // match, and the header above says what poisoning it costs.
    CHECK_FALSE(bringUpWrites<SyncProcessor::SP_RT_HS_SP>());
}

TEST_CASE("the bring-up establishes the rest of the block the sketch used to write")
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::init();

    CHECK(SyncProcessor::SP_H_TIMER_VAL::read() == 0x3A);
    CHECK(SyncProcessor::SYNC_PROC_5_23::read() == 0x00);
    CHECK(SyncProcessor::SYNC_PROC_5_5D::read() == 0x02);
}

TEST_CASE("the SD vertical sync position is one value for every source")
{
    // It is a vertical pan of the captured frame, one source line per count,
    // and it reaches the picture only where the sync processor separates V out
    // of composite sync. Nothing about a source's standard bears on it, and the
    // engine already owns vertical placement.
    // docs/investigations/the-sd-vsync-window-follows-the-sync-type.md
    Wire.reset();
    SyncProcessor::applySdVsyncPosition();

    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 14);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 11);
}

// The dynamic configuration: what the separator is set to pass by pass, as
// against the static half init() writes and the per-sync-type half
// applyForSyncType() writes. Every fact comes in, because where the source is
// selected and what the engine measured are not this block's to read.

static SyncProcessor::Dynamic settled()
{
    SyncProcessor::Dynamic source;
    source.searching = false;
    source.present = true;
    source.hunting = false;
    source.csync = false;
    source.pathSource = false;
    source.serrated = false;
    return source;
}

template <typename Field>
static bool dynamicWrites(const SyncProcessor::Dynamic &source)
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        SyncProcessor::applyDynamic(source);
        under[i] = Field::read();
    }
    return under[0] == under[1];
}

TEST_CASE("a source being hunted for gets the search configuration")
{
    SyncProcessor::Dynamic source = settled();
    source.searching = true;
    source.present = false;
    source.hunting = true;

    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyDynamic(source);

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x02);
    CHECK(SyncProcessor::SP_H_TIMER_VAL::read() == 0x3a);
    CHECK(SyncProcessor::SP_H_COAST::read() == 0);
    CHECK(SyncProcessor::SP_H_CST_ST::read() == 0x10);
}

TEST_CASE("a source being searched for without the hunt asked gets neither")
{
    // The caller that wants the hunt configuration says so. What is left is the
    // one value every source takes, so a separator mid-search is not left
    // configured for the source before it.
    SyncProcessor::Dynamic source = settled();
    source.searching = true;
    source.present = false;

    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyDynamic(source);

    CHECK(SyncProcessor::SP_DLT_REG::read() == 0xC0);
    CHECK_FALSE(dynamicWrites<SyncProcessor::SP_H_TIMER_VAL>(source));
}

TEST_CASE("a source whose sync carries no vertical interval gets the thresholds")
{
    SyncProcessor::Dynamic source = settled();
    source.pathSource = true;
    source.csync = true;

    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyDynamic(source);

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 7);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 3);
    CHECK(SyncProcessor::SP_DLT_REG::read() == 0xC0);
}

TEST_CASE("a settled source on the scaling path is read, not separated for")
{
    // The coast lengths are applyForSyncType()'s, and a second writer of them
    // closes a loop: the coast changes the measured line count, a changed count
    // arms a solve, and a solve applies the sync type.
    SyncProcessor::Dynamic source = settled();
    source.csync = true;
    source.serrated = true;

    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyDynamic(source);

    CHECK(SyncProcessor::SP_DLT_REG::read() == 0xC0);
    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x6b);
    CHECK_FALSE(dynamicWrites<SyncProcessor::SP_PRE_COAST>(source));
}

TEST_CASE("a source neither searched for nor acquired is left alone")
{
    // Between the two: a live count in range with the run not earned back. The
    // thresholds want a settled line length to be right about, so nothing is
    // written until the run says there is one.
    SyncProcessor::Dynamic source = settled();
    source.present = false;

    CHECK_FALSE(dynamicWrites<SyncProcessor::SP_DLT_REG>(source));
    CHECK_FALSE(dynamicWrites<SyncProcessor::SP_H_PULSE_IGNOR>(source));
}

TEST_CASE("the coast inversion is cleared for a settled composite-sync source")
{
    // applyForSearch() sets it, so a source that locks after a search has it
    // standing.
    SyncProcessor::Dynamic source = settled();
    source.csync = true;

    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyDynamic(source);

    CHECK(SyncProcessor::SP_COAST_INV_REG::read() == 0);
}

TEST_CASE("a separate-sync source's coast inversion is not touched")
{
    SyncProcessor::Dynamic source = settled();

    CHECK_FALSE(dynamicWrites<SyncProcessor::SP_COAST_INV_REG>(source));
}

// Whether the block is counting a horizontal sync. Its one register bit, and
// nothing else: the polarity beside it says which way the pulse goes, which is
// a property of the source rather than of whether it is being counted.

TEST_CASE("an active horizontal sync is reported however the pulse goes")
{
    Wire.reset();
    GBS::STATUS_SYNC_PROC_HSACT::write(1);

    GBS::STATUS_SYNC_PROC_HSPOL::write(0);
    CHECK(SyncProcessor::hsyncActive());

    GBS::STATUS_SYNC_PROC_HSPOL::write(1);
    CHECK(SyncProcessor::hsyncActive());
}

TEST_CASE("no horizontal sync is reported however the pulse goes")
{
    Wire.reset();
    GBS::STATUS_SYNC_PROC_HSACT::write(0);

    GBS::STATUS_SYNC_PROC_HSPOL::write(0);
    CHECK_FALSE(SyncProcessor::hsyncActive());

    GBS::STATUS_SYNC_PROC_HSPOL::write(1);
    CHECK_FALSE(SyncProcessor::hsyncActive());
}

// A field written only by the bypass switch has no owner on the scaling path,
// and this one displaces the picture by 94 output px when it is inherited.
// docs/investigations/leaving-bypass-leaves-the-sync-path-behind.md
TEST_CASE("normalising the polarity clears the inversion into the ADC PLL")
{
    Wire.reset();
    SyncProcessor::SP_HS2PLL_INV_REG::write(1);

    SyncProcessor::normaliseHsyncPolarity(true, false);

    CHECK(SyncProcessor::SP_HS2PLL_INV_REG::read() == 0u);
}
