#include "SyncOnGreen.h"

#include <Arduino.h>   // delay(), a hardware settling time

#include "Adc.h"
#include "Interrupts.h"
#include "SyncType.h"

namespace Tv5725 {

namespace {

// How long the sync processor has to hold HSACT before the sync separator's own output
// is worth reading, and how long that output then has to stay up. Both are runs
// of consecutive samples inside a window in milliseconds, so the two are
// coupled: the run only completes where a register read is much faster than a
// millisecond, which on this bus it is.
const uint16_t EdgeRun = 60;
const uint16_t HoldRun = 50;
const uint16_t EdgeWindowMs = 60;

// The sync processor's own test bus, which is where the sync separator's output
// appears. Selecting it is what makes it readable, and the previous selection
// is put back: the console, the auto-gain routine and getSyncPresent() drive
// the same two registers for other things.
struct SeparatorBus {
    uint8_t sel, spSel;

    SeparatorBus()
        : sel(Tv5725::TEST_BUS_SEL::read()), spSel(Tv5725::TEST_BUS_SP_SEL::read())
    {
        if (sel != 0xa) {
            Tv5725::TEST_BUS_SEL::write(0xa);
            delay(1);
        }
        if (spSel != 0x0f) {
            Tv5725::TEST_BUS_SP_SEL::write(0x0f);
            delay(1);
        }
        Tv5725::TEST_BUS_EN::write(1);
    }

    ~SeparatorBus()
    {
        if (sel != 0xa)
            Tv5725::TEST_BUS_SEL::write(sel);
        if (spSel != 0x0f)
            Tv5725::TEST_BUS_SP_SEL::write(spSel);
    }

    uint8_t read() const { return Tv5725::TEST_BUS_2F::read(); }
};

bool edgesHeld(uint32_t (*nowMs)())
{
    uint16_t run = 0;
    uint32_t started = nowMs();
    while (nowMs() - started < EdgeWindowMs) {
        if (Tv5725::STATUS_SYNC_PROC_HSACT::read() == 1) {
            if (++run >= EdgeRun)
                return true;
        } else if (run >= 4) {
            run -= 3;
        }
    }
    return run >= EdgeRun;
}

// Both bits, which is what the coarse pass judges on: it has no run of HSACT
// beside it, so it asks more of the one reading it takes.
const uint8_t SeparatingCleanly = 0x05;

// One tuning pass's worth of evidence, and how long each sample waits for the
// measured line length to move before giving up on it.
const uint8_t SamplesPerPass = 16;
const uint8_t LineLengthReads = 20;

// How many reads the separator's output gets to move in before it is called
// frozen, and a reading no measured line length matches -- which is how a
// separator behind an ADC PLL still in reset is called live without being
// judged, nothing it reports meaning anything there.
const uint8_t LivenessReads = 128;
const uint16_t PllInResetReading = 777;

// How long a new level takes to reach the sync separator, the level below which
// there is no room to step, and the level below which the walk owns the margin
// rather than a single trim.
const uint16_t StepSettleMs = 30;
const uint8_t LowestSteppable = 2;
const uint8_t LowestTrimmable = 8;

// Whether the sync processor is reporting trouble at all. Either the sync
// separator raised its own interrupt or hsync stopped being active, and both
// are worth a closer look at the line length.
bool separatorReportsTrouble()
{
    return Interrupts::STATUS_INT_SOG_BAD::read() == 1
        || Tv5725::STATUS_SYNC_PROC_HSACT::read() == 0;
}

// A line length that will not hold still is the sync processor retiming against
// edges the sync separator is inventing or missing.
bool lineLengthMoves()
{
    const uint16_t settled = Tv5725::STATUS_SYNC_PROC_HLOW_LEN::read();
    for (uint8_t a = 0; a < LineLengthReads; ++a)
        if (Tv5725::STATUS_SYNC_PROC_HLOW_LEN::read() != settled)
            return true;
    return false;
}

uint16_t countBadSamples(bool sourceClassified)
{
    uint16_t counted = 0;
    for (uint8_t i = 0; i < SamplesPerPass; ++i) {
        if (separatorReportsTrouble()) {
            Interrupts::acknowledgeSogBad();

            // With no standard detected there is no settled line length to
            // compare against, so trouble counts on its own rather than waiting
            // for evidence that cannot arrive.
            if (!sourceClassified || lineLengthMoves())
                ++counted;
        }
        delay((i % 3) == 0 ? 1 : 0);
    }
    return counted;
}

bool separatorHolds(const SeparatorBus &bus)
{
    if (bus.read() == 0)
        return false;

    delay(20);
    for (uint16_t a = 0; a < HoldRun; ++a)
        if (Tv5725::STATUS_SYNC_PROC_HSACT::read() == 0 || bus.read() == 0)
            return false;
    return true;
}

}  // namespace

uint8_t SyncOnGreen::level_ = 0;
uint32_t SyncOnGreen::windowStart_ = 0;
uint16_t SyncOnGreen::badSamples_ = 0;
bool SyncOnGreen::steppedInWindow_ = false;

const uint8_t SyncOnGreen::DefaultLevel;
const uint8_t SyncOnGreen::FrozenLevel;
const uint8_t SyncOnGreen::LevelMax;
const uint16_t SyncOnGreen::WindowMs;
const uint16_t SyncOnGreen::StepThreshold;
const uint16_t SyncOnGreen::HandoverThreshold;

void SyncOnGreen::choose(uint8_t level)
{
    if (level > LevelMax)
        return;

    level_ = level;
}

void SyncOnGreen::apply(uint8_t level)
{
    choose(level);
    apply();
}

void SyncOnGreen::apply() { ADC_SOGCTRL::write(level_); }

uint8_t SyncOnGreen::level() { return level_; }

bool SyncOnGreen::inSyncPath() { return SyncType::isCsync(); }

void SyncOnGreen::acquire(uint32_t (*nowMs)(), void (*putInForce)())
{
    if (!inSyncPath()) {
        choose(DefaultLevel);
        return;
    }

    putInForce();

    SeparatorBus bus;
    delay(100);

    while (true) {
        if (edgesHeld(nowMs) && separatorHolds(bus))
            return;

        const bool exhausted = level_ < 2;
        choose(exhausted ? DefaultLevel : (uint8_t)(level_ - 1));
        putInForce();
        delay(8);

        if (exhausted)
            return;
    }
}

void SyncOnGreen::liftOffFloor(void (*putInForce)())
{
    if (!inSyncPath() || level_ >= LowestSteppable)
        return;

    choose((uint8_t)(level_ + 1));
    putInForce();
    delay(StepSettleMs);
}

void SyncOnGreen::reacquire(void (*walk)(), void (*putInForce)(), bool reopen)
{
    if (!inSyncPath())
        return;

    const uint16_t first = Adc::PLLAD_VCORST::read() == 1
                               ? PllInResetReading
                               : (uint16_t)Tv5725::STATUS_SYNC_PROC_HLOW_LEN::read();

    for (uint8_t read = 0; read < LivenessReads; ++read) {
        if (Tv5725::STATUS_SYNC_PROC_HLOW_LEN::read() != first) {
            if (reopen) {
                choose(0);
                putInForce();
            } else {
                walk();
            }
            return;
        }
        delay(0);
    }

    choose(FrozenLevel);
    putInForce();
}

void SyncOnGreen::acquireCoarse(void (*putInForce)())
{
    if (!inSyncPath())
        return;

    SeparatorBus bus;

    while ((bus.read() & SeparatingCleanly) != SeparatingCleanly) {
        const bool exhausted = level_ < 4;
        choose(exhausted ? DefaultLevel : (uint8_t)(level_ - 2));
        putInForce();
        delay(exhausted ? 40 : 28);

        if (exhausted)
            return;
    }
}

void SyncOnGreen::forgetWindow(uint32_t nowMs)
{
    badSamples_ = 0;
    windowStart_ = nowMs;
}

// How long the level takes to reach the sync separator, and the lowest level
// worth taking a further step off once a window closes -- below it the walk
// owns the level, because only the walk can reach a floor and put the default
// back.
void SyncOnGreen::step(uint8_t to, void (*putInForce)())
{
    choose(to);
    putInForce();
    delay(StepSettleMs);
    badSamples_ = 0;
}

// A step taken inside the window that has just closed was evidence the level
// was too high for this source, so it gives up the margin that turned out to be
// needed. Once, and only from a level the walk does not own.
bool SyncOnGreen::trimEarnedMargin(void (*putInForce)())
{
    if (!steppedInWindow_)
        return false;

    steppedInWindow_ = false;
    if (level_ < LowestTrimmable)
        return false;

    step(level_ - 1, putInForce);
    return true;
}

// Enough bad samples inside one window to say the level is too high: one step
// down while there is room, and the walk from the default once there is not.
bool SyncOnGreen::stepOnEvidence(void (*putInForce)(), void (*escalate)())
{
    bool moved = false;
    if (level_ >= LowestSteppable) {
        step(level_ - 1, putInForce);
        moved = true;
    } else if (badSamples_ > HandoverThreshold) {
        // Nowhere left to step, so the walk takes over -- and it is the
        // caller's, because it knows when not to run at all.
        escalate();
        badSamples_ = 0;
        moved = true;
    }

    if (moved)
        steppedInWindow_ = true;
    return moved;
}

SyncOnGreen::Tuning SyncOnGreen::tune(bool sourceDisturbed, bool sourceClassified,
                                      uint32_t (*nowMs)(), void (*putInForce)(),
                                      void (*escalate)())
{
    Tuning outcome = {false, false, false};

    if (!inSyncPath())
        return outcome;

    if (sourceDisturbed || Interrupts::STATUS_INT_SOG_BAD::read() == 1) {
        if (nowMs() - windowStart_ > WindowMs)
            forgetWindow(nowMs());
        outcome.sourceUnsettled = true;
    }

    if (nowMs() - windowStart_ >= WindowMs) {
        outcome.levelMoved = outcome.phaseStale = trimEarnedMargin(putInForce);
        return outcome;
    }

    const uint16_t counted = countBadSamples(sourceClassified);
    badSamples_ += counted;
    if (counted != 0)
        outcome.sourceUnsettled = true;
    if (badSamples_ >= StepThreshold) {
        outcome.levelMoved = stepOnEvidence(putInForce, escalate);
        windowStart_ = nowMs();
    }
    return outcome;
}

}  // namespace Tv5725
