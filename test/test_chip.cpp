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

using Tv5725::Chip;

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

TEST_CASE("the scaler takes the DACs off the bypass route")
{
    routedTo(Chip::routeToHdBypass);
    Chip::routeToScaler();

    CHECK(Chip::DAC_RGBS_ADC2DAC::read() == 0);
    CHECK(Chip::DAC_RGBS_BYPS2DAC::read() == 0);
    CHECK(Chip::OUT_SYNC_SEL::read() == 0);
}

TEST_CASE("NOTHING routes the ADC to the DACs")
{
    // The ADC-to-DAC route is retired. It has no YUV-to-RGB converter in
    // circuit, so it could only ever carry an RGB source, where the HD bypass
    // channel carries either.
    // docs/investigations/one-bypass-route-carries-rgbhv.md
    fresh();
    Chip::routeToHdBypass();
    CHECK(Chip::DAC_RGBS_ADC2DAC::read() == 0);

    fresh();
    Chip::routeToScaler();
    CHECK(Chip::DAC_RGBS_ADC2DAC::read() == 0);

    fresh();
    Chip::init();
    CHECK(Chip::DAC_RGBS_ADC2DAC::read() == 0);
}

TEST_CASE("the bypass route takes the DACs off the scaler")
{
    routedTo(Chip::routeToScaler);
    Chip::routeToHdBypass();

    CHECK(Chip::DAC_RGBS_BYPS2DAC::read() == 1);
    CHECK(Chip::DAC_RGBS_ADC2DAC::read() == 0);
}

TEST_CASE("the HD bypass route leaves the sync select to the standard")
{
    // setOutModeHdBypass() writes OUT_SYNC_SEL 1 and then 2 for interlaced SD,
    // so a route that wrote it would undo the standard's choice.
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
