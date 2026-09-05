#include "SyncOnGreen.h"

#include <Arduino.h>   // delay(), a hardware settling time

#include "SyncType.h"

namespace Tv5725 {

namespace {

// How long the sync processor has to hold HSACT before the slicer's own output
// is worth reading, and how long that output then has to stay up. Both are runs
// of consecutive samples inside a window in milliseconds, so the two are
// coupled: the run only completes where a register read is much faster than a
// millisecond, which on this bus it is.
const uint16_t EdgeRun = 60;
const uint16_t HoldRun = 50;
const uint16_t EdgeWindowMs = 60;

// The sync processor's own test bus, which is where the slicer's output
// appears. Selecting it is what makes it readable, and the previous selection
// is put back: the console, the auto-gain routine and getSyncPresent() drive
// the same two registers for other things.
struct SliceBus {
    uint8_t sel, spSel;

    SliceBus()
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

    ~SliceBus()
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

bool sliceHolds(const SliceBus &bus)
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

const uint8_t SyncOnGreen::DefaultLevel;
const uint8_t SyncOnGreen::LevelMax;

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

    SliceBus bus;
    delay(100);

    while (true) {
        if (edgesHeld(nowMs) && sliceHolds(bus))
            return;

        const bool exhausted = level_ < 2;
        choose(exhausted ? DefaultLevel : (uint8_t)(level_ - 1));
        putInForce();
        delay(8);

        if (exhausted)
            return;
    }
}

}  // namespace Tv5725
