// Host-compiled unit tests for src/tv5725/Chip.cpp -- `make -C test chip`.
//
// The three DAC routes are ALTERNATIVES, and nothing outside this class clears
// any of them. Two set at once sums the paths at the DACs: measured on the
// bench, the black level lifts and the colours desaturate while every other
// register reads correct.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <Arduino.h>

#include "fake/Wire.h"

// SourceMeasurement links in behind HdBypass and wants these from the sketch.
float getSourceFieldRate(boolean) { return 50.0f; }
void tv5725Log(const char *) {}

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/HdBypass.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoRoute.h"

using Tv5725::Chip;
using Tv5725::VideoRoute;

static const uint8_t Poison = 0xA5;

static void fresh()
{
    Wire.reset();
    Wire.poison(Poison);
}

// The state a route has to overwrite: whichever one ran before it.
static void routedTo(void (*route)())
{
    fresh();
    route();
}

TEST_CASE("the scaler takes the DACs off bypass")
{
    routedTo(Chip::enterHdBypass);
    Chip::routeToScaler();

    CHECK(Chip::DAC_RGBS_ADC2DAC::read() == 0);
    CHECK(Chip::DAC_RGBS_BYPS2DAC::read() == 0);
    CHECK(Chip::OUT_SYNC_SEL::read() == 0);
}

TEST_CASE("bypass puts the DACs on the HD bypass channel and takes its sync")
{
    // The ADC-to-DAC route is retired: it has no converter in circuit, so it
    // carries RGB and nothing else. One route serves every source.
    routedTo(Chip::routeToScaler);
    Chip::enterHdBypass();

    CHECK(Chip::DAC_RGBS_BYPS2DAC::read() == 1);
    CHECK(Chip::DAC_RGBS_ADC2DAC::read() == 0);
    CHECK(Chip::OUT_SYNC_SEL::read() == 1);
}

TEST_CASE("the bypass switch leaves nothing of the scaler on the DACs")
{
    routedTo(Chip::enterHdBypass);
    Chip::routeToHdBypass();

    CHECK(Chip::DAC_RGBS_BYPS2DAC::read() == 1);
    CHECK(Chip::DAC_RGBS_ADC2DAC::read() == 0);
}

TEST_CASE("the HD bypass route leaves the sync select to the standard")
{
    // The entry writes OUT_SYNC_SEL 1 and HdBypass::applySd() then writes 2 for
    // interlaced SD, so a route that wrote it would undo the standard's choice.
    fresh();
    Chip::routeToHdBypass();

    CHECK(Chip::OUT_SYNC_SEL::read() == ((Poison >> 6) & 0x3));
}

TEST_CASE("the bring-up leaves every DAC route off")
{
    fresh();
    Chip::init();

    CHECK(Chip::DAC_RGBS_ADC2DAC::read() == 0);
    CHECK(Chip::DAC_RGBS_BYPS2DAC::read() == 0);
    CHECK(Chip::DAC_RGBS_BYPS_IREG::read() == 0);
}

TEST_CASE("the route in force follows the registers that select it")
{
    // The held value and s0_4b cannot disagree, because the writer records it.
    routedTo(Chip::routeToScaler);
    Chip::enterHdBypass();

    CHECK(VideoRoute::route() == VideoRoute::HdBypassChannel);
}

TEST_CASE("the colour DACs follow their input rather than resting at minimum")
{
    // Nothing writes these bits the other way, so a load that clears one leaves
    // a tinted picture with no register that reads wrong.
    fresh();
    Chip::dacsFollowInput();

    CHECK(Chip::DAC_RGBS_R0ENZ::read() == 1);
    CHECK(Chip::DAC_RGBS_G0ENZ::read() == 1);
    CHECK(Chip::DAC_RGBS_B0ENZ::read() == 1);
}

// The block resets, pulsed rather than established. The sketch spelled this out
// over twenty-five writes of fields this class declares, which made it a second
// owner of every one of them -- and the order is the whole content: a block
// released before the one feeding it is configured discards that configuration.
TEST_CASE("the scaler's blocks come back released")
{
    fresh();
    VideoRoute::toScaler();

    Chip::resetVideoBlocks();

    CHECK(Chip::SFTRST_IF_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_DEINT_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_MEM_FF_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_MEM_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_FIFO_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_OSD_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_VDS_RSTZ::read() == 1);
}

TEST_CASE("on the bypass channel the scaler's blocks are left in reset")
{
    // Nothing scaled is running, so releasing them would start blocks with no
    // configuration behind them.
    fresh();
    VideoRoute::toHdBypassChannel();

    Chip::resetVideoBlocks();

    CHECK(Chip::SFTRST_IF_RSTZ::read() == 0);
    CHECK(Chip::SFTRST_DEINT_RSTZ::read() == 0);
    CHECK(Chip::SFTRST_MEM_FF_RSTZ::read() == 0);
    CHECK(Chip::SFTRST_MEM_RSTZ::read() == 0);
    CHECK(Chip::SFTRST_FIFO_RSTZ::read() == 0);
    CHECK(Chip::SFTRST_OSD_RSTZ::read() == 0);
    CHECK(Chip::SFTRST_VDS_RSTZ::read() == 0);
}

TEST_CASE("the decimator, mode detect and sync processor are released either way")
{
    SUBCASE("on the scaling path") {
        fresh();
        VideoRoute::toScaler();
        Chip::resetVideoBlocks();
    }
    SUBCASE("on the bypass channel") {
        fresh();
        VideoRoute::toHdBypassChannel();
        Chip::resetVideoBlocks();
    }

    CHECK(Chip::SFTRST_DEC_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_MODE_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_SYNC_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_INT_RSTZ::read() == 1);
}

TEST_CASE("a bypass block that was not running is not started by the pulse")
{
    // The pass is held across the sequence whatever it was doing, so what says
    // whether to let it go again is what it was doing on entry.
    fresh();
    VideoRoute::toScaler();
    Tv5725::HdBypass::hold();

    Chip::resetVideoBlocks();

    CHECK(Tv5725::HdBypass::enabled() == false);
}

TEST_CASE("a bypass block that was running is let go again")
{
    fresh();
    VideoRoute::toScaler();
    Tv5725::HdBypass::release();

    Chip::resetVideoBlocks();

    CHECK(Tv5725::HdBypass::enabled());
}

// Whether a field was ever written clear, anywhere in the run. A block taken
// through reset and released again reads the same afterwards as one left
// running, so no final-state assertion can tell them apart.
// The state this runs against: a configured chip with every block out of reset.
// Started from the poison instead, a byte written for one field carries
// whatever the poison left in the bits beside it, and the run reads as having
// held blocks it never touched.
static void everyBlockRunning()
{
    Chip::SFTRST_IF_RSTZ::write(1);
    Chip::SFTRST_DEINT_RSTZ::write(1);
    Chip::SFTRST_MEM_FF_RSTZ::write(1);
    Chip::SFTRST_MEM_RSTZ::write(1);
    Chip::SFTRST_FIFO_RSTZ::write(1);
    Chip::SFTRST_OSD_RSTZ::write(1);
    Chip::SFTRST_VDS_RSTZ::write(1);
    Wire.trace.clear();
}

template <typename Field>
static bool everHeld()
{
    for (size_t i = 0; i < Wire.trace.size(); ++i) {
        const FakeTwoWire::Traced &t = Wire.trace[i];
        if (t.segment == Field::segment && t.reg == Field::byteOffset
            && !(t.value & (1u << Field::bitOffset)))
            return true;
    }
    return false;
}

TEST_CASE("the scaling path restarts the memory blocks between the two ends, not the ends")
{
    // The input formatter feeds the chain and the scaler reads it, and both are
    // configured by the time this runs. Holding either as well is a reset pulse
    // of a block that was working.
    fresh();
    VideoRoute::toScaler();
    everyBlockRunning();

    Chip::resetVideoBlocks();

    CHECK_FALSE(everHeld<Chip::SFTRST_IF_RSTZ>());
    CHECK_FALSE(everHeld<Chip::SFTRST_VDS_RSTZ>());
    CHECK(everHeld<Chip::SFTRST_MEM_RSTZ>());
    CHECK(everHeld<Chip::SFTRST_DEINT_RSTZ>());
}
