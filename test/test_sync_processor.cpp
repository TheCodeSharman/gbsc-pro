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

// The divider the pulse ignore was measured through, on PAL 576i.
static const uint16_t MeasuredCsyncDivider = 2200;

// And the one the pass-through channel runs the Wii's 480p line at.
static const uint16_t ChannelDivider = 2038;

template <typename Field>
static uint32_t applied(bool csync)
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyForSyncType(csync, MeasuredCsyncDivider);
    return Field::read();
}

template <typename Field>
static bool wasWritten(bool csync)
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        SyncProcessor::applyForSyncType(csync, MeasuredCsyncDivider);
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
    CHECK(applied<SyncProcessor::SP_SYNC_BYPS>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_HS_POL_ATO>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_VS_POL_ATO>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_HS_LOOP_SEL>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_H_PROTECT>(csync) == 0);
}

TEST_CASE("csync coasts before the vertical interval and protects the line")
{
    const bool csync = true;

    CHECK(applied<SyncProcessor::SP_SOG_SRC_SEL>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_EXT_SYNC_SEL>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_SOG_MODE>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_NO_COAST_REG>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_PRE_COAST>(csync) == 7);
    CHECK(applied<SyncProcessor::SP_POST_COAST>(csync) == 6);
    CHECK(applied<SyncProcessor::SP_SYNC_BYPS>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_HS_LOOP_SEL>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_H_PROTECT>(csync) == 1);
}

TEST_CASE("the polarity auto-corrects stay the separate-sync branch's")
{
    // Both belong to a source with its own H and V, and pass-through leaves
    // them off deliberately.
    CHECK_FALSE(wasWritten<SyncProcessor::SP_HS_POL_ATO>(true));
    CHECK_FALSE(wasWritten<SyncProcessor::SP_VS_POL_ATO>(true));

    CHECK(wasWritten<SyncProcessor::SP_HS_POL_ATO>(false));
    CHECK(wasWritten<SyncProcessor::SP_VS_POL_ATO>(false));
}

TEST_CASE("the separation threshold follows the line in force on both arrangements")
{
    // THE CSYNC BRANCH LEFT IT ON THE PREVIOUS SOURCE'S VALUE. Measured on the
    // bench, RISC PC on vga then the Wii selected on ypbpr: the arrangement
    // applied the csync coast 7/6 and SP_DLT_REG 192 while SP_H_PULSE_IGNOR
    // stayed at 0xff, which is what the separate-sync branch writes. The
    // recovery ladder repaired it about fifteen seconds later, so the
    // configuration a selection should have made was a rung's side effect.
    //
    // AND A SELECTION CANNOT KEY IT ON SERRATION, because the serration it can
    // read is the source being LEFT. One fraction of the line serves every
    // composite source instead -- 107 through the 2200-sample line it was
    // measured at -- and on the RISC PC's unserrated composite sync 2 and 107
    // are indistinguishable, frozen, with controls, at 15 kHz and at 37.9 kHz.
    // ../docs/investigations/the-risc-pc-composite-sync-is-not-serrated.md
    //
    // The same fraction reaches a separate-sync source, which is indifferent to
    // the whole range -- so the arrangement decides the coast and the
    // separator, and not this.
    CHECK(wasWritten<SyncProcessor::SP_H_PULSE_IGNOR>(true));
    CHECK(wasWritten<SyncProcessor::SP_H_PULSE_IGNOR>(false));

    CHECK(applied<SyncProcessor::SP_H_PULSE_IGNOR>(true) == 0x6b);
    CHECK(applied<SyncProcessor::SP_H_PULSE_IGNOR>(false) == 0x6b);
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
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);
    const uint32_t pre = SyncProcessor::SP_PRE_COAST::read();
    const uint32_t post = SyncProcessor::SP_POST_COAST::read();

    SyncProcessor::applyDefaultCoastWindow();

    CHECK(SyncProcessor::SP_PRE_COAST::read() == pre);
    CHECK(SyncProcessor::SP_POST_COAST::read() == post);
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

TEST_CASE("the window is placed from the line rate rather than from a status bit")
{
    // **STATUS_SYNC_PROC_HSACT RAILS IN BOTH DIRECTIONS AND CANNOT GATE THIS.**
    // Measured on the bench the same day: 0 across a 450 ms window on a source
    // that then acquired, and 1 at every one of twenty separator levels while
    // the sync processor counted nothing at all.
    //
    // Refusing on 0 is the damaging rail, because it leaves the coast and clamp
    // windows unwritten -- and an unconfigured sync processor cannot count, so
    // the bit stays 0 and nothing ever places them.
    //
    // Whether a source is worth writing for is the CALLER's question and it
    // already asks it: VideoSourceAcquisition::mayWriteForSource() is
    // Chip::hasPower() && !sourceIsSearching(), which is a count in range and a
    // steadiness run rather than one bit.
    // docs/known-issues.md, "STATUS_SYNC_PROC_HSACT saturates in BOTH directions"
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HSACT::write(0);

    CHECK(SyncProcessor::acquireCoastWindow(false, BenchLineRateHz));
    CHECK(Wire.touched[0x05][0x4D]);
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
        false, false, BenchLineRateHz, 2250, 0));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 23);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 27);
}

TEST_CASE("a composite source is measured in its own units")
{
    // HPERIOD_IF counts against the chip's 27 MHz rather than the ADC clock, so
    // the same window is a different fraction of a different number.
    steadyLine(431);

    CHECK(SyncProcessor::acquireClampWindow(
        true, false, BenchLineRateHz, 2250, 0));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 14);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 18);
}

TEST_CASE("a component source clamps later, and holds for the same span")
{
    steadyLine(431);

    CHECK(SyncProcessor::acquireClampWindow(
        false, true, BenchLineRateHz, 2250, 0));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 73);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 77);
}

TEST_CASE("both clamp writers mean the same line on sync on green")
{
    // SP_CS_CLP_ST counts the 27 MHz reference -- clampFromReferenceClock()
    // chooses it -- so a caller handing over a line measured in ADC samples
    // puts the window a long way down the line. Measured on the Wii at 480p
    // into 1920x1080, divider 1212: a start of 108 against the 19 the
    // acquisition layer writes, 41 source pixels inside the picture, which is a
    // black bar on every line and the whole frame clamped 35% dark.
    steadyLine(431);
    const uint32_t lineRateHz = 31468;
    const uint16_t divider = 1212;
    const HsyncPulse pulse(88.0f / 1212.0f);
    SyncProcessor::applyForSyncType(true, divider);

    SyncProcessor::placeClampFor(true, true, lineRateHz, divider, pulse);
    const uint16_t installed = SyncProcessor::SP_CS_CLP_ST::read();

    SyncProcessor::acquireClampWindow(true, true, lineRateHz, divider,
                                      SyncProcessor::clampOffsetForPulse(pulse, lineRateHz));

    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == installed);
}

TEST_CASE("an offset moves the whole window later")
{
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HTOTAL::write(2250);

    CHECK(SyncProcessor::acquireClampWindow(
        false, true, BenchLineRateHz, 2250, 0x60));
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
        false, false, BenchLineRateHz, 2250, 0));
    CHECK(SyncProcessor::SP_CS_CLP_ST::read() == 24);
    CHECK(SyncProcessor::SP_CS_CLP_SP::read() == 28);
}

TEST_CASE("a line length that will not hold still leaves the clamp alone")
{
    steadyLine(431);
    GBS::STATUS_SYNC_PROC_HTOTAL::write(2250);
    Wire.drift(0x00, 0x18);

    CHECK_FALSE(SyncProcessor::acquireClampWindow(false, false, 0, 0, 0));
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
    CHECK(SyncProcessor::SP_DLT_REG::read() == 0x00);
}

TEST_CASE("an unmeasured interval keeps the hold")
{
    // The hold is the default, because a source denied one it needs emits
    // nothing. Coasted 4 lines before the vertical interval and 7
    // after, a 625-line source's equalisation pulses fall inside the count and
    // it measures 622. Coasted 7 and 3 the same source measures 314.
    // docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md
    Wire.reset();
    Wire.poison(Poisons[0]);

    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 7);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 6);
}

TEST_CASE("a composite source is coasted before its vertical interval")
{
    Wire.reset();
    SyncProcessor::applySeparationThresholds(true);

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 7);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 6);
    CHECK(SyncProcessor::SP_DLT_REG::read() >= 0x70);
}

TEST_CASE("an interval needing no hold is coasted for less than one that does")
{
    // One value sits on the edge of both measured bands at once: the Wii's sync
    // on green drops to absent at a post coast of 0 and is indistinguishable at
    // 4 and 6, where the RISC PC's composite is clean from 0 to 4 and displaces
    // 0.7% of frames at 5.
    // docs/investigations/the-composite-roll-is-lost-horizontal-pulses-in-the-doubled-scan.md
    SyncProcessor::useLongPostCoast(true);

    Wire.reset();
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);
    const uint32_t serrated = SyncProcessor::SP_POST_COAST::read();

    SyncProcessor::useLongPostCoast(false);

    Wire.reset();
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);

    CHECK(SyncProcessor::SP_POST_COAST::read() < serrated);

    SyncProcessor::useLongPostCoast(true);
}

TEST_CASE("an overridden coast is what the sync type applies")
{
    // The pair cannot be held against the engine from outside: it re-applies on
    // every solve, and holding it with a freeze stops the re-solve that makes
    // the count consequence visible. An override is what makes a coast
    // measurable on a running engine.
    SyncProcessor::overrideCoast(12, 9);

    Wire.reset();
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 12);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 9);

    SyncProcessor::forgetCoastOverride();
}

TEST_CASE("an overridden coast reaches the separation thresholds too")
{
    // Two functions write the pair, so an override reaching one of them would
    // be undone by whichever ran last.
    SyncProcessor::overrideCoast(12, 9);

    Wire.reset();
    SyncProcessor::applySeparationThresholds(true);

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 12);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 9);

    SyncProcessor::forgetCoastOverride();
}

TEST_CASE("forgetting the override returns the constants")
{
    SyncProcessor::overrideCoast(12, 9);
    SyncProcessor::forgetCoastOverride();

    Wire.reset();
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 7);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 6);
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

// How short a horizontal pulse must be to be ignored. Every pulse where the
// source carries its own vertical sync; where it is composite, a fraction of
// the line in ADC samples until the scan is known, and a noise floor once the
// scan is known to be progressive.
// docs/investigations/the-pulse-ignore-value-is-measured-not-chosen.md
// docs/investigations/the-pulse-ignore-is-a-fraction-of-the-line.md
// docs/investigations/the-risc-pc-composite-sync-is-not-serrated.md

TEST_CASE("the pulse ignore does not key on the sync type")
{
    // ONE VALUE FOR EVERY SOURCE, the way SP_DLT_REG beside it is. A
    // separate-sync source is indifferent across the whole range: measured on
    // `vga`, the RISC PC at 800x600@60 with separate sync into 1080p, frozen,
    // the register written by name -- 255, 16 and 65 each left
    // STATUS_SYNC_PROC_VTOTAL 627 in every one of 311 samples at 25 ms, with
    // STATUS_SYNC_PROC_HTOTAL 1342, the measured sync width 164 and the emitted
    // frame 1439x1080 at the same margins under all three.
    //
    // So the sync type must not decide it. A type that has been forgotten
    // cannot be told from one measured as separate, and reading that as
    // separate wrote 255 onto a sync-on-green source and left it uncountable.
    // ../docs/investigations/the-pulse-ignore-is-a-fraction-of-the-line.md
    Wire.reset();
    SyncProcessor::applyPulseIgnore(MeasuredCsyncDivider, true);

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x6b);
}

TEST_CASE("a source with no serrations takes the lower fraction")
{
    // The two bounds on this field do not overlap across the bench's sources.
    // The serrated one needs 3.68% of the line or more -- below that the
    // separator counts the vertical interval and reads 319 against a true 310
    // -- and the unserrated ones hold their count only at 2.81% or less, the
    // 4.9% above landing inside the dither on 640x512@50 composite.
    //
    // So the fraction follows a MEASUREMENT of the source rather than a
    // constant. SerrationMeasurement is what takes it.
    // ../docs/investigations/the-composite-count-dithers-where-the-ignore-nears-the-pulse.md
    Wire.reset();
    SyncProcessor::applyPulseIgnore(MeasuredCsyncDivider, false);

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 61);
}

TEST_CASE("a composite source's pulse ignore is a fraction of the line in ADC samples")
{
    // The pulse widths it is compared against are counted in ADC samples, so
    // one byte is a different fraction of every line. 107 was measured right on
    // PAL 576i through a 2200-sample line -- every smaller value counted the
    // serrations as lines. Through the 1446-sample line a 1080p raster affords
    // the Wii at 480p, the same 107 sits above its 106-sample sync pulse and
    // the separator ignores the source; lowered by hand, it measured within
    // half a second. docs/investigations/the-pulse-ignore-is-a-fraction-of-the-line.md
    Wire.reset();
    SyncProcessor::applyPulseIgnore(MeasuredCsyncDivider, true);
    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x6b);

    SUBCASE("a shorter line in samples takes a threshold under its sync pulse") {
        SyncProcessor::applyPulseIgnore(1446, true);
        CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 70);
    }

    SUBCASE("and the reference line likewise") {
        SyncProcessor::applyPulseIgnore(2040, true);
        CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 99);
    }
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
    // The line rate is what the window is derived from, so a rate no source
    // runs is what makes it underivable -- not a status bit.
    steadyLine(431);
    SyncProcessor::forgetPositions();

    REQUIRE_FALSE(SyncProcessor::acquireCoastWindow(false, 1000000));

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
// has already found. The search runs when the count is not the source's, so it
// asks for the settings most likely to see a pulse at all.

static SyncProcessor::Dynamic settled()
{
    SyncProcessor::Dynamic source;
    source.searching = false;
    source.present = true;
    source.csync = false;
    source.pathSource = false;
    source.divider = MeasuredCsyncDivider;
    source.latched = true;
    // Serrated, which is what every path writes before the probe has run.
    source.serrated = true;
    return source;
}

// A source being hunted for: nothing present, and the line the block is
// counting is what the threshold keys on.
static SyncProcessor::Dynamic hunting(bool csync, bool latched)
{
    SyncProcessor::Dynamic source = settled();
    source.searching = true;
    source.present = false;
    source.csync = csync;
    source.latched = latched;
    source.divider = ChannelDivider;
    return source;
}

TEST_CASE("the search admits the sync pulse at whatever clock the block is counting at")
{
    // WITH THE DIVIDER UNLATCHED the block counts at the chip's 27 MHz
    // reference or half of it, and a 7.3% pulse is 63 or 31 samples there --
    // under any threshold sized for a divider. Measured on a ypbpr selection
    // from vga at 1920x1080: the retry cycled for 45 s with the divider's
    // fraction in force, and 16 written by hand measured the source within half
    // a second. docs/investigations/the-pulse-ignore-is-a-fraction-of-the-line.md
    //
    // The value does not outlive the search -- the dynamic pass below writes
    // the divider's fraction the moment the count is in range -- because a
    // search value that did was measured reading 271 lines against a source's
    // 260 for thirteen seconds.
    Wire.reset();
    SyncProcessor::applyForSearch(hunting(true, false));

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 16);

    SUBCASE("and whatever the sync type, which a search has not established") {
        // A forgotten sync type cannot be told from one measured as separate,
        // and the separate-sync value admits no pulse at all: measured on
        // `ypbpr`, the Wii at 480p into 1920x1080, SP_H_PULSE_IGNOR 255 held
        // the engine at state: absent with the sync processor counting the 27
        // MHz reference rather than the divider, and /sc?~ did not clear it.
        // ../docs/known-issues.md
        SyncProcessor::applyForSearch(hunting(false, false));
        CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 16);
    }
}

TEST_CASE("the search separates on the threshold every source is read with")
{
    Wire.reset();
    SyncProcessor::applyPulseWidthDifference();
    const uint32_t settled = SyncProcessor::SP_DLT_REG::read();

    Wire.reset();
    SyncProcessor::applyForSearch(hunting(false, false));

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

    SyncProcessor::applyForSearch(hunting(false, false));

    CHECK(SyncProcessor::SP_H_CST_ST::read() == start);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == stop);
    CHECK(SyncProcessor::SP_H_COAST::read() == 0);
}

TEST_CASE("the search coasts the way every composite source is read")
{
    // THE INVERTED COAST GATES THE ACTIVE LINE INSTEAD OF THE VERTICAL
    // INTERVAL, so the block counts a serrated source's serration pulses as
    // lines and the hsync reaching the ADC PLL is broken every line. Measured on
    // a ypbpr selection from a 37.9 kHz predecessor: the search's 1 went in at
    // 2.3 s, the count read 263..268 against the source's 259/260 and
    // STATUS_SYNC_PROC_HTOTAL 2230..2392 against a 2200 divider for the next
    // 21 s, and clearing the one field brought the count, the total and
    // STATUS_MISC_PLLAD_LOCK right on the following sample.
    //
    // It latches: the wrong count is what keeps the source searched for, and a
    // search is the one state that writes the field, so nothing reaches the
    // clear. The search therefore coasts the way the sync type says, which is
    // what the threshold and the pulse width difference below already do.
    uint32_t under[2];
    for (bool csync : {false, true}) {
        for (int i = 0; i < 2; ++i) {
            Wire.reset();
            Wire.poison(Poisons[i]);
            SyncProcessor::applyForSearch(hunting(csync, false));
            under[i] = SyncProcessor::SP_COAST_INV_REG::read();
        }

        CHECK(under[0] != under[1]);
    }
}

TEST_CASE("the search forgets where the windows were placed")
{
    steadyLine(431);
    SyncProcessor::forgetPositions();
    REQUIRE(SyncProcessor::acquireCoastWindow(false, BenchLineRateHz));
    REQUIRE(SyncProcessor::coastPlaced());

    SyncProcessor::applyForSearch(hunting(false, false));

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
    // SP_RT_HS_SP follows the source's measured sync width: one quantity in
    // three registers, all three written by SourceMeasurement off one held value. A second writer
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

template <typename Field>
static uint32_t dynamicValue(const SyncProcessor::Dynamic &source)
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyDynamic(source);
    return Field::read();
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

// A SOURCE THAT CANNOT BE COUNTED IS BEING SEARCHED FOR, AND THERE IS NO SECOND
// ANSWER. The caller used to choose between the search configuration and the
// pulse width alone, and nothing it knows could decide which: a source with no
// measured line length wants the windows the search places, whatever the reason
// it has none. Measured on the Wii in 480i over ypbpr, where the selection
// asked for the lesser answer: STATUS_SYNC_PROC_VTOTAL sat at 97 for 13.65 s
// until a recovery wrote the search configuration, after which the source
// acquired in 1.6 s.
TEST_CASE("a source that cannot be counted gets the search configuration")
{
    SyncProcessor::Dynamic source = settled();
    source.searching = true;
    source.present = false;

    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyDynamic(source);

    // settled() is latched, so the threshold is the line's own fraction.
    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x6b);
    CHECK(SyncProcessor::SP_H_TIMER_VAL::read() == 0x3a);
    CHECK(SyncProcessor::SP_H_COAST::read() == 0);
    CHECK(SyncProcessor::SP_H_CST_ST::read() == 0x10);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 0x100);
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
    CHECK(SyncProcessor::SP_POST_COAST::read() == 6);
    CHECK(SyncProcessor::SP_DLT_REG::read() == 0xC0);
}

TEST_CASE("a settled source on the scaling path is read, not separated for")
{
    // The coast lengths are applyForSyncType()'s, and a second writer of them
    // closes a loop: the coast changes the measured line count, a changed count
    // arms a solve, and a solve applies the sync type.
    SyncProcessor::Dynamic source = settled();
    source.csync = true;

    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyDynamic(source);

    CHECK(SyncProcessor::SP_DLT_REG::read() == 0xC0);
    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x6b);
    CHECK_FALSE(dynamicWrites<SyncProcessor::SP_PRE_COAST>(source));
}

// The fraction skips the equalising pulses an interlaced vertical interval
// carries, and a progressive source has none to skip -- but a value BELOW the
// fraction is what the separator makes of an unserrated vertical interval, and
// one that low cannot count the source at all.
//
// Measured on `vga`, the RISC PC at 800x600@60 with `SYNC 1` through the
// 2038-sample line the engine installs, frozen with the divider latched and the
// separator re-locked on each value by a source mode round trip: 2, 14, 50 and
// 70 count 1226, 213, 554 and 426 lines at 36.99 Hz, where 86 and the
// fraction's 99 count 622 at 60.32 in every reading, across four interleaved
// controls. 36.99 Hz is the engine's own wrong rate on this source, so the
// threshold is what produces it.
//
// Below that floor the whole plateau up to the source's own 246-sample sync
// pulse reads it: 86..236 at 100%, ascending and descending alike, at separator
// levels 9 and 12.
// docs/investigations/the-engine-makes-the-composite-count-unmeasurable.md
TEST_CASE("a present composite source takes the fraction of its own line")
{
    SyncProcessor::Dynamic source = settled();
    source.csync = true;
    source.divider = ChannelDivider;

    CHECK(dynamicValue<SyncProcessor::SP_H_PULSE_IGNOR>(source) == 99);
}

// AND ON A PATH SOURCE TOO, which is the route with nothing else to re-place
// it: the channel solves nothing, so what the arrangement wrote stands for the
// life of the route.
TEST_CASE("a present source measured unserrated takes the lower fraction")
{
    // 640x512@50 on composite sync is the case: 4.9% of its line lands inside
    // the count's dither, and the band around 2.8% holds it. Measured frozen
    // on the solve, 48 reads 530 in 23 of 25 samples where the shipped 85
    // reads 527..530 with no dominant value.
    // ../docs/investigations/the-composite-count-dithers-where-the-ignore-nears-the-pulse.md
    SyncProcessor::Dynamic source = settled();
    source.csync = true;
    source.serrated = false;
    source.divider = ChannelDivider;

    CHECK(dynamicValue<SyncProcessor::SP_H_PULSE_IGNOR>(source) == 57);
}

TEST_CASE("a path source takes the same fraction")
{
    SyncProcessor::Dynamic source = settled();
    source.pathSource = true;
    source.csync = true;
    source.divider = ChannelDivider;

    CHECK(dynamicValue<SyncProcessor::SP_H_PULSE_IGNOR>(source) == 99);
}

TEST_CASE("a source neither searched for nor acquired keeps the search's threshold")
{
    // Between the two: a live count in range with the run not earned back. On a
    // sync-on-green source the count is in range through a reference clock the
    // ADC PLL has not latched, so the line the block counts is not the
    // divider's yet -- measured at 1187 samples against a 2040 reference, where
    // the divider's fraction, 99, sits above the 87-sample pulse and the solve
    // then reads 271 or 431 lines. The fraction arrives with the solve's own
    // install and with the source present, both of them latched states.
    SyncProcessor::Dynamic source = settled();
    source.present = false;
    source.csync = true;
    source.divider = 1446;
    source.latched = false;

    CHECK_FALSE(dynamicWrites<SyncProcessor::SP_DLT_REG>(source));
    CHECK_FALSE(dynamicWrites<SyncProcessor::SP_H_PULSE_IGNOR>(source));
}

// THE SEARCH IS ENTERED FROM A LATCHED STATE AS WELL AS FROM A COLD ONE, and
// the search's threshold is sized for the chip's reference clock: through the
// divider's own line it sits under the floor an unserrated vertical interval
// imposes, and the count it then takes is not the source's -- which is what
// keeps the source searched for.
//
// Measured on `vga`, the RISC PC at 800x600@60 with `SYNC 1` passed through to
// 1920x1080: one reading of 171 lines armed the search 54 s into a window the
// source had been held in, SP_H_PULSE_IGNOR went 99 to 16 with PLLAD_MD 2038
// against STATUS_SYNC_PROC_HTOTAL 2038, and the counts from there were 512,
// 558, 559, 824, 1069 and 1185 against the source's 622 for the remaining 35 s.
// docs/investigations/the-engine-makes-the-composite-count-unmeasurable.md
TEST_CASE("a search through the divider's own line takes that line's fraction")
{
    SyncProcessor::Dynamic source = settled();
    source.searching = true;
    source.present = false;
    source.csync = true;
    source.divider = ChannelDivider;

    CHECK(dynamicValue<SyncProcessor::SP_H_PULSE_IGNOR>(source) == 99);
}

// AND THE REFERENCE CLOCK KEEPS IT, because there the block counts a line the
// divider does not describe: measured at 860 or 430 samples against either
// reference divider, where the Wii's 480p pulse reads 63 or 31 samples and the
// divider's fraction is above it.
TEST_CASE("a search through a line the divider does not describe keeps the search's threshold")
{
    SyncProcessor::Dynamic source = settled();
    source.searching = true;
    source.present = false;
    source.csync = true;
    source.divider = ChannelDivider;
    source.latched = false;

    CHECK(dynamicValue<SyncProcessor::SP_H_PULSE_IGNOR>(source) == 16);
}

TEST_CASE("the coast inversion follows whether the source can be counted yet")
{
    // IT COMPENSATES FOR A COAST WINDOW STILL AT ITS DEFAULT, which is what a
    // source with no measured line length has. Measured on a ypbpr selection
    // from a 37.9 kHz predecessor: uninverted from the start the block counts
    // nothing at all -- 97, the value it holds when it is not following the
    // source -- and inverted after a count arrives it counts the serrations as
    // lines, 263..268 against 259/260, with STATUS_SYNC_PROC_HTOTAL 2230..2392
    // against a 2200 divider and the ADC PLL unlocked for as long as it stands.
    //
    // So it is one value per state and this is the one owner, which is why the
    // write sits AHEAD of a branch that returns.
    // ../../docs/investigations/the-search-coast-inversion-outlived-the-search.md
    SyncProcessor::Dynamic hunting = settled();
    hunting.csync = true;
    hunting.searching = true;
    hunting.present = false;

    CHECK(dynamicValue<SyncProcessor::SP_COAST_INV_REG>(hunting) == 1);

    SyncProcessor::Dynamic countable = settled();
    countable.csync = true;

    CHECK(dynamicValue<SyncProcessor::SP_COAST_INV_REG>(countable) == 0);
}

TEST_CASE("a source with its own sync has no coast to invert")
{
    for (bool searching : {false, true}) {
        SyncProcessor::Dynamic source = settled();
        source.searching = searching;
        source.present = !searching;

        CHECK_FALSE(dynamicWrites<SyncProcessor::SP_COAST_INV_REG>(source));
    }
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

// THE SEPARATOR'S OUTPUT IS REFERENCED TO THE LEADING EDGE OF THE PULSE, AND
// SO IS THE ADC PLL. The chip's default is the rising edge, which on a
// negative-going composite sync is the TRAILING edge of every pulse -- the one
// that moves through the vertical interval -- and the PLL then loses three or
// four horizontal pulses a field on the line-doubled scan. Measured on `vga` at
// 320x256@50 on composite sync into 1080p, frozen: 19% to 24% of frames
// displaced by whole lines with the default edge, 0.0% to 0.3% with both
// references on the leading edge, and neither bit alone closes it.
// ../docs/investigations/the-composite-roll-is-lost-horizontal-pulses-in-the-doubled-scan.md
TEST_CASE("composite sync references the leading edge of its pulse")
{
    CHECK(applied<SyncProcessor::SP_CS_P_SWAP>(true) == 1u);
    CHECK(applied<SyncProcessor::SP_HS2PLL_INV_REG>(true) == 1u);
    CHECK(SyncProcessor::referencesLeadingEdge());
}

// A source on its own hsync pin keeps the chip's default edge: the capture
// origin was calibrated there, and the inversion into the PLL displaces the
// picture 94 output px on that path.
// ../docs/investigations/leaving-bypass-leaves-the-sync-path-behind.md
TEST_CASE("separate sync references the chip's default edge")
{
    CHECK(applied<SyncProcessor::SP_CS_P_SWAP>(false) == 0u);
    CHECK(applied<SyncProcessor::SP_HS2PLL_INV_REG>(false) == 0u);
    CHECK_FALSE(SyncProcessor::referencesLeadingEdge());
}

// The polarity normalisation runs AFTER the sync type is put in force, on every
// rate measurement. The PLL's edge is the sync type's, so the normalisation
// may not touch it or the composite path loses the edge it was just given.
TEST_CASE("normalising the polarity leaves the ADC PLL's edge to the sync type")
{
    Wire.reset();
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);

    SyncProcessor::normaliseHsyncPolarity(true, false);

    CHECK(SyncProcessor::SP_HS2PLL_INV_REG::read() == 1u);
}

TEST_CASE("entering pass-through returns to the chip's default edge")
{
    Wire.reset();
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);

    SyncProcessor::applyForPassThrough(false);

    CHECK(SyncProcessor::SP_CS_P_SWAP::read() == 0u);
    CHECK_FALSE(SyncProcessor::referencesLeadingEdge());
}

// The static setup runs AFTER the sync type on the re-detection route, as it
// does for SP_SOG_MODE, so the edge is not among the static registers: written
// there it put a composite source back on the default edge every `/sc?~`.
// docs/investigations/sp-sog-mode-had-two-owners.md
TEST_CASE("the static setup leaves the edge the sync type chose")
{
    Wire.reset();
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);

    SyncProcessor::init();

    CHECK(SyncProcessor::SP_CS_P_SWAP::read() == 1u);
    CHECK(SyncProcessor::SP_HS2PLL_INV_REG::read() == 1u);
    CHECK(SyncProcessor::referencesLeadingEdge());
}

// THE CLAMP COUNTS FROM THE SAME REFERENCE, so it moves by one pulse with it or
// it lands on the sync tip. Measured on the Wii at 480p on `ypbpr` into
// 1920x1080, divider 1212, frozen: with the leading edge referenced and the
// clamp left at 19..23 the frame lifts from mean luma 143 to 162 and the
// source's own black bars read grey; moved by the 62 counts the 88-sample
// pulse is at 27 MHz, the bars are black again.
TEST_CASE("the clamp follows the leading edge by one pulse")
{
    steadyLine(431);
    const uint32_t lineRateHz = 31468;
    const uint16_t divider = 1212;
    const HsyncPulse pulse(88.0f / 1212.0f);

    SyncProcessor::applyForSyncType(false, divider);
    SyncProcessor::placeClampFor(true, true, lineRateHz, divider, pulse);
    const uint16_t trailing = SyncProcessor::SP_CS_CLP_ST::read();

    SyncProcessor::applyForSyncType(true, divider);
    SyncProcessor::placeClampFor(true, true, lineRateHz, divider, pulse);
    const uint16_t leading = SyncProcessor::SP_CS_CLP_ST::read();

    CHECK(leading - trailing == SyncProcessor::clampOffsetForPulse(pulse, lineRateHz));
    CHECK(SyncProcessor::clampOffsetForPulse(pulse, lineRateHz) == 62u);
}

// THE SEPARATOR'S REGENERATED PULSE ENDS WHERE THE SOURCE'S DOES. Referenced
// to the leading edge, the separator's output would otherwise end there too,
// and the capture counter that zeroes on it moves a whole pulse earlier: the
// picture lands one pulse right with its far edge off the window. The stop is
// in ADC samples -- measured against a pan of known units inside one
// acquisition, 1.00 samples a count on 320x256@50 and 1.03 on 800x600@60 --
// and the pulse put back on the end puts the picture where separate sync
// frames it, to within a few columns on the bench mode. On the default edge
// the chip's own 0 is the calibrated state.
// ../docs/investigations/the-composite-roll-is-lost-horizontal-pulses-in-the-doubled-scan.md
TEST_CASE("the separator's pulse ends where the source's does on the leading edge")
{
    const HsyncPulse pulse(153.0f / 2200.0f);

    Wire.reset();
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);
    SyncProcessor::placeSeparatorPulseEnd(2200, pulse);
    CHECK(SyncProcessor::SP_CS_HS_SP::read() == 153u);

    SyncProcessor::applyForSyncType(false, MeasuredCsyncDivider);
    SyncProcessor::placeSeparatorPulseEnd(2200, pulse);
    CHECK(SyncProcessor::SP_CS_HS_SP::read() == 0u);
}

TEST_CASE("the per-load setup leaves the separator's pulse end where the install put it")
{
    Wire.reset();
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);
    SyncProcessor::placeSeparatorPulseEnd(2200, HsyncPulse(153.0f / 2200.0f));

    SyncProcessor::prepare(MeasuredCsyncDivider);

    CHECK(SyncProcessor::SP_CS_HS_SP::read() == 153u);
}

// The per-load setup runs after applyForSyncType() on every preset load, and
// SP_SOG_MODE follows the sync type. Written from both, a separate-sync source
// is left configured for sync-on-green: STATUS_SYNC_PROC_VTOTAL then reads 0,
// detection's line-count wait times out, and the input is never taken -- so no
// output mode is set and the engine's own re-arm cannot fire either.
// Measured on the bench at 800x600@60: SOG mode 1 gives VTOTAL 0, and clearing
// it by hand gives 627 and a full acquisition within 3 s.
// docs/investigations/sp-sog-mode-had-two-owners.md

TEST_CASE("entering pass-through leaves the SOG polarity auto-correct off")
{
    // The polarity is normalised ONCE, by normaliseHsyncPolarity(), so that
    // nothing downstream takes it as an input. The chip's own auto-correct is a
    // second owner of that fact, and measured it displaces where the sync
    // processor starts counting by a whole pulse.
    //
    // Measured in pass-through at 800x600@60, divider 2038, high-active:
    // with it SET the low-active retime stop of 1858 leaves a 150 column black
    // bar down the left; CLEARED, the same 1858 frames the card's green border
    // on all four edges. It is also what a pass-through round trip left behind
    // to displace the scaled picture 310 columns.
    // docs/investigations/the-sog-polarity-auto-correct-displaced-pass-through.md
    Wire.reset();
    SyncProcessor::SP_SOG_P_ATO::write(1);

    SyncProcessor::applyForPassThrough(false);

    CHECK(SyncProcessor::SP_SOG_P_ATO::read() == 0u);
}

// SYNC ON GREEN WANTS THE SOG REFERENCE INVERTED IN PASS-THROUGH, AND THE SYNC
// TYPE CANNOT BE THE CONDITION. SP_SOG_MODE is raised by applyForSyncType()'s
// csync branch, so composite sync on a dedicated pin and sync on green are the
// same bit -- keying on it inverts the VGA input on SYNC 1 as well, where
// pass-through is already measured correct. A component source carries its
// sync on the SOG/Y pin by construction, which is what separates the two.
//
// Measured on the Wii at 480p into 1920x1080 in pass-through: un-inverted, the
// source's left border never reaches the frame and 56 of 1920 columns are
// absent; inverted, the frame carries the whole line at left 0 and right 0.
// It is a compensating correction and not a correct polarity -- un-inverted the
// counter reads the true pulse, 152 of 2038 against the mode's 62 of 858, and
// inverted it reads the complement.
// ../docs/investigations/the-sog-reference-is-inverted-for-pass-through.md
TEST_CASE("sync on green inverts the SOG reference in pass-through")
{
    Wire.reset();
    SyncProcessor::SP_SOG_P_INV::write(0);

    SyncProcessor::applyForPassThrough(true);

    CHECK(SyncProcessor::SP_SOG_P_INV::read() == 1u);
}

TEST_CASE("a source whose sync is not on green keeps the SOG reference")
{
    Wire.reset();
    SyncProcessor::SP_SOG_P_INV::write(1);

    SyncProcessor::applyForPassThrough(false);

    CHECK(SyncProcessor::SP_SOG_P_INV::read() == 0u);
}

// The inversion moves where the processor starts counting by a whole pulse, so
// the clamp has to follow by the same distance. SP_CS_CLP_ST counts the 27 MHz
// reference, which is the line coastLineFor() already states, so the offset is
// the pulse expressed in those counts rather than a fraction refitted for the
// inverted path.
//
// Measured on the Wii at 480p: a 152 sample pulse of a 2038 sample line is 64
// of the line's 858 counts, and the clamp frames the picture anywhere between
// 60 and 120 where the un-inverted reference wants 19. Below that the window
// lands on picture -- at 40 the first 120 columns come through as a grey band
// at level 100 and the black level lifts the frame to mean luma 220 -- and
// above it the window clamps on sync and crushes the frame to mean 131.
// So 19 + 64 sits near the middle of the band, where the bare 64 sits one step
// inside its lower edge.
TEST_CASE("the clamp follows an inverted SOG reference by one pulse")
{
    const HsyncPulse pulse(152.0f / 2038.0f);

    CHECK(SyncProcessor::clampOffsetForPulse(pulse, 31468) == 64u);
}

// AND THE SCALING PATH HAS TO WRITE IT TOO, because nothing on the way out of
// pass-through calls applyForPassThrough(). Left behind, the inversion reaches
// a route whose capture origin consumes it: SP_RT_HS_SP is derived from the
// pulse hsyncPulseSamples() folds, so it lands in the same place inverted or
// not while the retimed edge has moved by a whole pulse.
//
// Measured on the Wii at 480p scaling into 1920x1080, divider 1212: inverted,
// the picture moves from a left margin of 38 to 209 of 1920 and clips at the
// right. It is the same round-trip hazard SP_SOG_P_ATO had, which cost 310
// columns.
// docs/investigations/the-sog-polarity-auto-correct-displaced-pass-through.md
TEST_CASE("the scaling path never leaves the SOG reference inverted")
{
    Wire.reset();
    SyncProcessor::SP_SOG_P_INV::write(1);

    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);

    CHECK(SyncProcessor::SP_SOG_P_INV::read() == 0u);
}

// AND THE HELD FACT IS PART OF THE WRITE. init() cleared SP_SOG_P_INV on its
// own, leaving sogReferenceInverted() answering for an inversion no longer in
// circuit -- a second owner of the field the header names one owner for.
//
// The clamp is what consumes it: the acquisition adds
// clampOffsetForPulse() whenever the fact says inverted, so a fact left
// true behind a cleared register puts the clamp a whole pulse late, onto
// active picture, where the black level comes off the scene and takes the
// whole frame's colour with it.
//
// Measured on the Wii at 480p on `ypbpr` into 1920x1080, reached by a
// pass-through round trip: SP_CS_CLP_ST 78 and SP_CS_CLP_SP 82 against the 19
// and 23 the csync component path solves at that line rate, with SP_SOG_P_INV
// reading 0 beside them. docs/known-issues.md
TEST_CASE("the static setup clears the held SOG inversion with the register")
{
    Wire.reset();
    SyncProcessor::applyForPassThrough(true);
    REQUIRE(SyncProcessor::sogReferenceInverted());

    SyncProcessor::init();

    CHECK(SyncProcessor::SP_SOG_P_INV::read() == 0u);
    CHECK_FALSE(SyncProcessor::sogReferenceInverted());
}

// The sub coast is applyForSyncType()'s, and entering pass-through wrote it a
// second time -- unconditionally off, which is right for separate sync and
// wrong for every source carrying serrations. A separate-sync source wants it
// off either way, so the second write agreed with the first and the defect was
// invisible on `vga`; on the Wii's sync on green it is the whole of why
// pass-through showed no signal.
//
// Measured on the bench, Wii 480p on `ypbpr` in pass-through: the coast pair,
// SP_NO_COAST_REG, SP_SOG_MODE and SP_H_PROTECT all still carry the csync
// branch's values, and SP_DIS_SUB_COAST alone reads 1.
TEST_CASE("entering pass-through leaves the sub coast the sync type chose")
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);

    SyncProcessor::applyForPassThrough(true);

    CHECK(SyncProcessor::SP_DIS_SUB_COAST::read() == 0u);
}

TEST_CASE("the per-load setup leaves the sync mode the sync type chose")
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyForSyncType(false, MeasuredCsyncDivider);

    SyncProcessor::prepare(MeasuredCsyncDivider);

    CHECK(SyncProcessor::SP_SOG_MODE::read() == 0u);
}

TEST_CASE("the per-load setup leaves the coast enable the sync type chose")
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyForSyncType(false, MeasuredCsyncDivider);

    SyncProcessor::prepare(MeasuredCsyncDivider);

    CHECK(SyncProcessor::SP_NO_COAST_REG::read() == 1u);
}

// The H counter's overflow protect follows the sync type, and nothing on the
// board measures whether it is helping -- so the recovery ladder's toggle is
// the only thing entitled to move it afterwards. Measured on the bench at
// 320x256@50 and 640x480@60, both on composite sync, with automation frozen
// and the bit alternated: margins identical to the pixel, spread within 0.1,
// STATUS_SYNC_PROC_HTOTAL at the divider with sd 0.00 either way. A setting
// with no measurable effect is one that must have a single owner, because
// nothing can tell which writer won.
TEST_CASE("the per-load setup leaves the overflow protect the sync type chose")
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyForSyncType(false, MeasuredCsyncDivider);

    SyncProcessor::prepare(MeasuredCsyncDivider);

    CHECK(SyncProcessor::SP_H_PROTECT::read() == 0u);
}

// The sub coast is the coast WITHIN a line: SP_H_CST_ST/SP_H_CST_SP mask a
// window of the retiming so a serration cannot be taken for the line's hsync.
// A source with its own hsync needs none, and the default window masks exactly
// where its edge arrives.
//
// Measured on the bench, 320x256@50 on `vga`, returning from SYNC 1 to SYNC 0:
// left enabled with the window back at its default 16..256,
// STATUS_SYNC_PROC_HTOTAL wandered 3218..3251 against a PLLAD_MD of 2200, so
// every duty reading was refused as UNLOCKED, the solve never completed and the
// sync pad was never driven back -- a correctly measured source and a black
// output, for as long as it was left. Writing this one bit restored HTOTAL to
// 2200 and the acquisition within seconds. docs/known-issues.md
TEST_CASE("a source with its own hsync is left with no sub coast")
{
    CHECK(wasWritten<SyncProcessor::SP_DIS_SUB_COAST>(false));
    CHECK(applied<SyncProcessor::SP_DIS_SUB_COAST>(false) == 1u);
}

TEST_CASE("the sync type writes the pulse width difference with the coast")
{
    // The coast lengths and the pulse width difference are ONE separation
    // configuration, and applyForSyncType() used to write only the coast.
    // Measured on the bench across a vga -> ypbpr change that stalled: the
    // composite coast pair against SP_DLT_REG 0 -- the composite coast carrying
    // the separate-sync pulse width left behind by the source before it. The sync processor then counted 97 lines, the ADC PLL never
    // locked, and the dynamic writes that would repair it are withheld from a
    // source the engine cannot count. docs/known-issues.md
    CHECK(wasWritten<SyncProcessor::SP_DLT_REG>(true));
    CHECK(applied<SyncProcessor::SP_DLT_REG>(true) >= 0x70);
}

// The clamp and the coast window are readied for DETECTION, which is the only
// path that measures a source through them with nothing writing them
// afterwards. The per-load setup writes neither: the solve places both.
// ../docs/investigations/the-detection-defaults-are-not-the-preset-paths.md

template <typename Field>
static bool prepareWrote()
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        SyncProcessor::prepare(MeasuredCsyncDivider);
        under[i] = Field::read();
    }
    return under[0] == under[1];
}

TEST_CASE("readying for detection places a coast window to count within")
{
    Wire.reset();

    SyncProcessor::prepareForDetection();

    CHECK(SyncProcessor::SP_H_CST_ST::read() == 0x10u);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 0x100u);
}

TEST_CASE("readying for detection holds the clamp off the reference clock")
{
    Wire.reset();
    Wire.poison(Poisons[0]);

    SyncProcessor::prepareForDetection();

    CHECK(SyncProcessor::SP_CLP_SRC_SEL::read() == 0u);
    CHECK(SyncProcessor::SP_NO_CLAMP_REG::read() == 1u);
    CHECK(SyncProcessor::SP_CLAMP_MANUAL::read() == 0u);
    CHECK(SyncProcessor::SP_HCST_AUTO_EN::read() == 0u);
}

// s5_55 carries the auto-coast enable beside the two polarity auto-corrects,
// which only the separate-sync branch writes -- so a byte write here would put
// the chip's own polarity correct in force on a csync source.
TEST_CASE("readying for detection leaves the polarity auto-correct alone")
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        SyncProcessor::prepareForDetection();
        under[i] = SyncProcessor::SP_HS_POL_ATO::read() * 2
                   + SyncProcessor::SP_VS_POL_ATO::read();
    }

    CHECK(under[0] != under[1]);
}

TEST_CASE("the per-load setup leaves the coast window to the solve")
{
    CHECK_FALSE(prepareWrote<SyncProcessor::SP_H_CST_ST>());
    CHECK_FALSE(prepareWrote<SyncProcessor::SP_H_CST_SP>());
    CHECK_FALSE(prepareWrote<SyncProcessor::SP_HCST_AUTO_EN>());
}

TEST_CASE("the per-load setup leaves the clamp to the window that places it")
{
    CHECK_FALSE(prepareWrote<SyncProcessor::SP_CLAMP_MANUAL>());
    CHECK_FALSE(prepareWrote<SyncProcessor::SP_CLP_SRC_SEL>());
    CHECK_FALSE(prepareWrote<SyncProcessor::SP_NO_CLAMP_REG>());
}

TEST_CASE("the clamp is released only once it has been placed")
{
    steadyLine(431);

    SUBCASE("a placed clamp is let go") {
        SyncProcessor::forgetPositions();
        REQUIRE(SyncProcessor::acquireClampWindow(
            false, false, BenchLineRateHz, 2250, 0));
        SyncProcessor::holdClamp();

        SyncProcessor::releaseClampIfPlaced();

        CHECK(SyncProcessor::SP_NO_CLAMP_REG::read() == 0);
    }

    SUBCASE("an unplaced clamp stays held, because nothing has measured it") {
        SyncProcessor::forgetPositions();
        SyncProcessor::holdClamp();

        SyncProcessor::releaseClampIfPlaced();

        CHECK(SyncProcessor::SP_NO_CLAMP_REG::read() == 1);
    }
}

TEST_CASE("the separation thresholds leave the pulse ignore alone")
{
    // ONE OWNER PER FIELD, and the pulse ignore's is applyDynamic(): it keys on
    // the measured scan where these key on the sync type, so this pass is handed
    // neither the scan nor the divider it would take to derive one. A second
    // write from here overwrote the value the selection had just chosen.
    //
    // Measured on the bench over three ypbpr selections from the RISC PC at
    // 320x256@50: the arrangement wrote 0x6b at 1.6 s on all three, and on one
    // of them a 0x02 landed at 3.3 s, after which STATUS_MISC_PLLAD_LOCK read 0
    // and the sync processor counted 270/271 against a 2200 divider for the
    // rest of the window. The other two held 0x6b and acquired at 3.3 s. That
    // is the intermittency, and it is a second writer rather than a flaky part.
    Wire.reset();
    SyncProcessor::applyForSyncType(true, MeasuredCsyncDivider);
    REQUIRE(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x6b);

    SyncProcessor::applySeparationThresholds(true);

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == 0x6b);
}
