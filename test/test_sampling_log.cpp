// Host-compiled unit tests for Tv5725::SamplingLog -- `make -C test sampling-log`.
//
// The class drives PLLAD_MD itself, so its safety property is the one worth
// pinning: the divider held on entry goes back when the walk ends, and the walk
// never asks for one above the ceiling. Driving 3008 took the unit off the
// network and only a power cycle brought it back.
//
// Time is injected rather than read, which is what lets any of this run on the
// host at all: millis() is deliberately absent from test/fake/Arduino.h.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SamplingLog.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/gbs_types.h"

// The emitted line is the instrument, so one test reads it.
static std::string g_lastLine;
void tv5725Log(const char *line) { g_lastLine = line; }

// SourceMeasurement.h declares the two the sketch supplies; nothing here spins
// for a field rate.
float getSourceFieldRate(boolean) { return 0.0f; }

using namespace Tv5725;

// A settled 320x256@50 source. HeldLineRateHz is what the engine measured for
// it, which is what the walk is handed.
static const uint32_t HeldLineRateHz = 15625;

static void sourceOnTheBus(uint16_t divider)
{
    Wire.reset();
    GBS::HPERIOD_IF::write(431);
    GBS::PLLAD_MD::write(divider);
}

// Run the walk to completion, returning the highest divider it ever asked for.
static uint16_t driveToEnd(SamplingLog &log, uint32_t stepMs)
{
    uint16_t highest = 0;
    uint32_t now = 0;
    for (int guard = 0; guard < 100000 && log.active(); ++guard) {
        now += stepMs;
        log.poll(now);
        const uint16_t asked = GBS::PLLAD_MD::read();
        if (asked > highest)
            highest = asked;
    }
    return highest;
}

TEST_CASE("the walk never asks for a divider above the ceiling")
{
    sourceOnTheBus(2250);
    SamplingLog log;

    log.sweep(0, 2800, 4000, 100, 10, 1, HeldLineRateHz);
    const uint16_t highest = driveToEnd(log, 5);
    const uint16_t ceiling = SamplingLog::DividerCeiling;

    CHECK(highest <= ceiling);
}

TEST_CASE("the divider held on entry goes back when the walk ends")
{
    sourceOnTheBus(2250);
    SamplingLog log;

    log.sweep(0, 1000, 1400, 100, 10, 1, HeldLineRateHz);
    driveToEnd(log, 5);

    CHECK(GBS::PLLAD_MD::read() == 2250);
}

TEST_CASE("a monitor run stops once its duration is up")
{
    sourceOnTheBus(2250);
    SamplingLog log;

    log.monitor(0, 16, 100);
    uint32_t now = 0;
    for (int guard = 0; guard < 1000 && log.active(); ++guard) {
        now += 16;
        log.poll(now);
    }

    CHECK_FALSE(log.active());
    CHECK(now <= 200);
}

TEST_CASE("the sample carries the chip's interrupt status")
{
    // s0_0F is a LATCHED interrupt status byte, and bit 3 is documented "input
    // source switch the mode". Whether it fires for a source mode change on
    // this board is open, and it cannot be answered by polling from the host:
    // register reads are deferred to loop(), so a poll fast enough to catch the
    // transition starves the loop it is trying to observe -- measured, it
    // wedges the mode change it was watching.
    //
    // Sampled from loop() it costs one register read and disturbs nothing.
    // Read without acknowledging: the bit latches, so one sample after the
    // change is enough to say whether it ever set.
    // docs/investigations/divider-latched-measurement.md
    sourceOnTheBus(2250);
    GBS::STATUS_0F::write(0x08);   // bit 3, the mode switch

    SamplingLog log;
    log.monitor(0, 10, 1000);
    log.poll(10);

    // Last column, so the header the run emits stays aligned with the row.
    CHECK(g_lastLine.rfind("smp,", 0) == 0);
    CHECK(g_lastLine.substr(g_lastLine.rfind(',')) == ",8");
}

TEST_CASE("an event names the branch a decision took, with the count it took it on")
{
    // The sync watcher's RGBHV decisions are taken on a line count against a
    // threshold, inside loop(), and are over before any HTTP read can see
    // them. A register dump afterwards shows the destination and not the
    // choice, so the choice has to say so as it is made.
    g_lastLine.clear();

    SamplingLog::event(1234, "rgbhv-bypass", 627, 15);

    CHECK(g_lastLine == "evt,1234,rgbhv-bypass,627,15");
}

TEST_CASE("an event is one line whatever the caller passes")
{
    g_lastLine.clear();

    SamplingLog::event(0, "rgbhv-scale", 311, 14);

    CHECK(g_lastLine == "evt,0,rgbhv-scale,311,14");
}

TEST_CASE("the walk clocks the ADC from the rate it is handed, not from HPERIOD_IF")
{
    // HPERIOD_IF rails on the scaling path with a perfect picture, and reads 10
    // in the unlocked state the walk exists to interrogate. Converted, that is a
    // 613 kHz line: the post divider comes out 0 and the oversampling collapses
    // to 1, so the walk would move the whole clock group and answer a different
    // question from the one asked of PLLAD_MD.
    Wire.reset();
    GBS::HPERIOD_IF::write(10);
    GBS::PLLAD_MD::write(2250);

    SamplingLog log;
    log.sweep(0, 2250, 2250, 100, 10, 4, HeldLineRateHz);

    CHECK(GBS::PLLAD_KS::read() == 2);
}

// The walk writes PLLAD_MD, which the engine owns and re-solves from held
// state. Two writers on one field is the fault this project exists to remove,
// so the walk has to say it is running -- and a monitor run must NOT, because
// watching a live engine is the whole point of it.
TEST_CASE("the walk says it is running, so the engine can be held off it")
{
    sourceOnTheBus(2250);
    SamplingLog log;
    CHECK_FALSE(log.sweeping());

    SUBCASE("a walk is sweeping until it finishes") {
        log.sweep(0, 1600, 1800, 100, 40, 4, HeldLineRateHz);
        CHECK(log.sweeping());
        driveToEnd(log, 10);
        CHECK_FALSE(log.sweeping());
    }

    SUBCASE("a monitor run never is") {
        log.monitor(0, 25, 200);
        CHECK(log.active());
        CHECK_FALSE(log.sweeping());
    }
}

TEST_CASE("a decision repeated is not news, so only a change is emitted")
{
    // Measured on a locked bench source: rgbhv-keep-scaling fired 37 times a
    // SECOND, which floods the console every other diagnostic is read from and
    // says nothing the first line did not. A branch that holds shows as a gap
    // between timestamps; how often it is re-entered inside that gap is what a
    // monitor run answers.
    SamplingLog::event(10, "held", 311, 14);
    g_lastLine.clear();

    SamplingLog::event(20, "held", 311, 14);
    CHECK(g_lastLine.empty());

    SUBCASE("a different branch is news") {
        SamplingLog::event(30, "moved", 311, 14);
        CHECK(g_lastLine == "evt,30,moved,311,14");
    }

    SUBCASE("the same branch on a different count is news") {
        SamplingLog::event(30, "held", 312, 14);
        CHECK(g_lastLine == "evt,30,held,312,14");
    }
}
