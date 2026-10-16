// Host-compiled tests for Tv5725::AutoGain -- `make -C test auto-gain`.
//
// The loop reads the converter's own level off the test bus and backs the
// analog gain off while it saturates. What saturation IS carries the defect
// this class replaced, so the band is pinned here rather than left to the
// bench.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/AutoGain.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/TestBus.h"

void tv5725Log(const char *) {}

using namespace Tv5725;

namespace {

AutoGain::Levels levels(uint16_t red, uint16_t green, uint16_t blue)
{
    const uint8_t over = AutoGain::OverToStep;   // the whole pass, on any one channel
    AutoGain::Levels measured = {{red, over}, {green, over}, {blue, over}};
    return measured;
}

// A bus reporting one level on every read, which is what a flat field looks
// like to a pass.
void busReports(uint16_t level)
{
    Wire.bank[0][0x2E] = (uint8_t)(level & 0xFF);
    Wire.bank[0][0x2F] = (uint8_t)(level >> 8);
}

AutoGain::Levels clippedCounts(uint8_t red, uint8_t green, uint8_t blue)
{
    AutoGain::Levels measured = {{TestBus::LevelMax, red},
                                 {TestBus::LevelMax, green},
                                 {TestBus::LevelMax, blue}};
    return measured;
}

// A bus whose reading climbs and wraps, so a pass sees a chosen number of
// samples in the band and the rest well below it.
void busClimbsFrom(uint8_t low)
{
    Wire.bank[0][0x2F] = 0x03;
    Wire.bank[0][0x2E] = low;
    Wire.drift(0, 0x2E);
}

}  // namespace

TEST_CASE("a saturated channel asks for less gain")
{
    Wire.reset();

    CHECK(AutoGain::gainFor(0x7B, levels(TestBus::LevelMax, 0, 0))
          == 0x7B + AutoGain::Step);
}

TEST_CASE("a channel clipping on its own is enough")
{
    // On an RGB source the three channels carry independent pictures, so one
    // of them can clip while the other two are nowhere near the band.
    Wire.reset();

    AutoGain::Levels measured = {{0x200, 0}, {0x210, 0}, {0x3FC, AutoGain::OverToStep}};
    CHECK(AutoGain::gainFor(0x7B, measured) == 0x7B + AutoGain::Step);
}

TEST_CASE("a ceiling below full scale is still saturation")
{
    // THE DEFECT THIS CLASS REPLACED. The level a channel stops at is per
    // channel and below full scale -- 0x3fc, 0x3fd and 0x3fc measured on one
    // input against 0x3ff on another -- so an equality against the top of the
    // range is a condition that never holds and a loop keyed on one never
    // runs.
    Wire.reset();

    CHECK(AutoGain::gainFor(0x7B, levels(0, 0x3FC, 0)) == 0x7B + AutoGain::Step);
}

TEST_CASE("a measurement clear of the band leaves the gain alone")
{
    Wire.reset();

    AutoGain::Levels measured = {{0x3E4, 0}, {0x3D3, 0}, {0x3E2, 0}};
    CHECK(AutoGain::gainFor(0x7B, measured) == 0x7B);
}

TEST_CASE("the gain stops at the top of the register rather than wrapping")
{
    Wire.reset();

    CHECK(AutoGain::gainFor(0xFF, levels(TestBus::LevelMax, 0, 0)) == 0xFF);
    CHECK(AutoGain::gainFor(0xFE, levels(TestBus::LevelMax, 0, 0)) == 0xFF);
}

TEST_CASE("a pass reports the highest level a channel reached, not the last")
{
    // One read is one pixel at whatever moment the bus was read, so a pass
    // that reported its last read would answer with whatever was on screen
    // when it stopped looking.
    Wire.reset();
    Wire.bank[0][0x2F] = 0x03;
    Wire.bank[0][0x2E] = 0xF0;
    Wire.drift(0, 0x2E);

    const AutoGain::Levels peaks = AutoGain::peaks();

    CHECK(peaks.red.peak == TestBus::LevelMax);
}

TEST_CASE("a pass puts the test bus back")
{
    Wire.reset();
    TestBus::select(TestBus::SyncProcessor);

    AutoGain::peaks();

    CHECK(TestBus::selected() == (uint8_t)TestBus::SyncProcessor);
}

TEST_CASE("backing off moves every channel together")
{
    // The colour space set the three gains alike, and one channel moving on
    // its own is a tint no one asked for.
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x7B, 0x7B, 0x7B);
    busReports(TestBus::LevelMax);

    AutoGain::run(100000);
    CHECK(AutoGain::run(100000 + AutoGain::SettleMs));

    CHECK(Adc::ADC_RGCTRL::read() == 0x7B + AutoGain::Step);
    CHECK(Adc::ADC_GGCTRL::read() == 0x7B + AutoGain::Step);
    CHECK(Adc::ADC_BGCTRL::read() == 0x7B + AutoGain::Step);
}

TEST_CASE("a pass that measures no saturation leaves the gain where it was")
{
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x7B, 0x7B, 0x7B);
    busReports(0x300);

    AutoGain::run(200000);
    CHECK(!AutoGain::run(200000 + AutoGain::QuietMs));

    CHECK(Adc::ADC_GGCTRL::read() == 0x7B);
}

TEST_CASE("a pass steps from the gain that is held, not from the register")
{
    // The gain in force is state this class is handed, the way every other
    // solved value is. Reading it back asks the chip for a value the firmware
    // put there, and the colour space writes the same register.
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x40, 0x40, 0x40);
    Adc::ADC_RGCTRL::write(0xC0);
    busReports(TestBus::LevelMax);

    AutoGain::run(300000);
    AutoGain::run(300000 + AutoGain::SettleMs);

    CHECK(Adc::ADC_RGCTRL::read() == 0x40 + AutoGain::Step);
}

TEST_CASE("a pass counts the samples that reached the band")
{
    Wire.reset();
    busClimbsFrom(0xF8);   // eight of the channel's reads are in the band

    const AutoGain::Levels measured = AutoGain::peaks();

    CHECK(measured.red.over == 8);
}

TEST_CASE("the gain moves on the pass's total, not on one channel's")
{
    // A picture that clips shows it on every channel it clips, where the
    // all-bits read falls in whichever channel's turn it lands on.
    Wire.reset();

    const uint8_t under = AutoGain::OverToStep - 1;
    CHECK(AutoGain::gainFor(0x7B, clippedCounts(under, 0, 0)) == 0x7B);
    CHECK(AutoGain::gainFor(0x7B, clippedCounts(under - 2, 1, 1)) == 0x7B);
    CHECK(AutoGain::gainFor(0x7B, clippedCounts(under - 2, 2, 1))
          == 0x7B + AutoGain::Step);
}

TEST_CASE("a step waits for the picture to catch up")
{
    // The level is read after the frame buffer, so a gain change is a frame or
    // two away from the measurement that judges it. Stepping again before then
    // judges a gain already left behind, and the walk runs to the end of the
    // register -- measured, from the seed to 0xff with nothing to stop it.
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x7B, 0x7B, 0x7B);
    busReports(TestBus::LevelMax);

    AutoGain::run(600000);
    CHECK(AutoGain::run(600000 + AutoGain::SettleMs));
    CHECK(!AutoGain::run(600000 + 2 * AutoGain::SettleMs - 1));
    CHECK(AutoGain::run(600000 + 2 * AutoGain::SettleMs));

    CHECK(Adc::ADC_GGCTRL::read() == 0x7B + 2 * AutoGain::Step);
}

TEST_CASE("clipping has to hold across two passes before the gain moves")
{
    // A pass over the threshold on its own is as likely to be the all-bits
    // read as a clipped picture, and once a step is taken nothing takes it
    // back -- so a walk that trusted one pass crept past the knee, a step a
    // minute, on a picture that had stopped clipping.
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x7B, 0x7B, 0x7B);
    busReports(TestBus::LevelMax);

    CHECK(!AutoGain::run(800000));

    CHECK(Adc::ADC_GGCTRL::read() == 0x7B);
}

TEST_CASE("a source arriving starts the walk again")
{
    // Its gain has just been seeded afresh, so a pass left over from the
    // source before cannot be half the evidence for the first step.
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x7B, 0x7B, 0x7B);
    busReports(TestBus::LevelMax);

    AutoGain::run(900000);
    AutoGain::forget();

    CHECK(!AutoGain::run(900000 + AutoGain::SettleMs));
}

TEST_CASE("a walk with nothing to correct stops reading the bus every pass")
{
    // A pass is seventy-two reads of the bus and three settling waits, so
    // looking at the walk's own rate for ever spends a chunk of every loop on
    // a source that has nothing left to correct. A picture that starts
    // clipping is noticed a quiet interval later, which is what that buys.
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x7B, 0x7B, 0x7B);
    busReports(0x300);

    AutoGain::run(1000000);

    Wire.touched[0][0x4D] = false;
    AutoGain::run(1000000 + AutoGain::SettleMs);
    CHECK_FALSE(Wire.touched[0][0x4D]);

    Wire.touched[0][0x4D] = false;
    AutoGain::run(1000000 + AutoGain::QuietMs);
    CHECK(Wire.touched[0][0x4D]);
}

TEST_CASE("a walk that is still stepping keeps looking at its own rate")
{
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x7B, 0x7B, 0x7B);
    busReports(TestBus::LevelMax);

    AutoGain::run(1100000);

    Wire.touched[0][0x4D] = false;
    AutoGain::run(1100000 + AutoGain::SettleMs);
    CHECK(Wire.touched[0][0x4D]);
}

TEST_CASE("a dark picture never settles the walk")
{
    // A pass with nothing bright on it says only that nothing bright was on
    // screen, which is not the same as a gain that fits. Settling on it would
    // lock in the seed and clip everything brighter that followed.
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x7B, 0x7B, 0x7B);
    busReports(0x200);

    uint32_t now = 1200000;
    for (int i = 0; i < 40; ++i) {
        AutoGain::run(now);
        now += AutoGain::QuietMs;
    }

    Wire.touched[0][0x4D] = false;
    AutoGain::run(now + AutoGain::QuietMs);

    CHECK(Wire.touched[0][0x4D]);
}

TEST_CASE("near-white without clipping is what settles the walk")
{
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x7B, 0x7B, 0x7B);
    busReports(AutoGain::EvidenceLevel);

    uint32_t now = 1300000;
    for (int i = 0; i < 40; ++i) {
        AutoGain::run(now);
        now += AutoGain::QuietMs;
    }

    Wire.touched[0][0x4D] = false;
    AutoGain::run(now + AutoGain::QuietMs);
    CHECK_FALSE(Wire.touched[0][0x4D]);

    Wire.touched[0][0x4D] = false;
    AutoGain::run(now + AutoGain::SettledMs);
    CHECK(Wire.touched[0][0x4D]);
}

TEST_CASE("a settled walk still corrects a picture that starts clipping")
{
    // It looks rarely rather than not at all, so content brighter than
    // anything it saw while settling is answered rather than left clipped
    // until the source changes.
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x7B, 0x7B, 0x7B);
    busReports(AutoGain::EvidenceLevel);

    uint32_t now = 1500000;
    for (int i = 0; i < 40; ++i) {
        AutoGain::run(now);
        now += AutoGain::QuietMs;
    }

    busReports(TestBus::LevelMax);
    now += AutoGain::SettledMs;
    AutoGain::run(now);
    now += AutoGain::SettleMs;

    CHECK(AutoGain::run(now));
    CHECK(Adc::ADC_GGCTRL::read() == 0x7B + AutoGain::Step);
}

TEST_CASE("the walk keeps the picture's top below saturation, not at it")
{
    // Content a little brighter than anything the walk has seen has to have
    // somewhere to go, so the level a sample counts against sits below the
    // one a channel stops at.
    Wire.reset();
    busReports(AutoGain::BackOffLevel);

    const AutoGain::Levels measured = AutoGain::peaks();

    CHECK(AutoGain::BackOffLevel < AutoGain::Saturated);
    CHECK(measured.red.over >= AutoGain::OverToStep);
    CHECK(AutoGain::gainFor(0x7B, measured) == 0x7B + AutoGain::Step);
}

TEST_CASE("a source arriving starts a settled walk again")
{
    Wire.reset();
    Adc::forgetGain();
    AutoGain::forget();
    Adc::holdGain(0x7B, 0x7B, 0x7B);
    busReports(AutoGain::EvidenceLevel);

    uint32_t now = 1700000;
    for (int i = 0; i < 40; ++i) {
        AutoGain::run(now);
        now += AutoGain::QuietMs;
    }

    AutoGain::forget();
    Wire.touched[0][0x4D] = false;
    AutoGain::run(now + AutoGain::QuietMs);

    CHECK(Wire.touched[0][0x4D]);
}
