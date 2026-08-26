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
#include "../GBSC-Pro-Source code/gbs-control/gbs_types.h"

// The emitted line is the instrument, so one test reads it.
static std::string g_lastLine;
void tv5725Log(const char *line) { g_lastLine = line; }

using namespace Tv5725;

// A settled 320x256@50 source, so the line rate the sweep derives is a real one.
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

    log.sweep(0, 2800, 4000, 100, 10, 1);
    const uint16_t highest = driveToEnd(log, 5);
    const uint16_t ceiling = SamplingLog::DividerCeiling;

    CHECK(highest <= ceiling);
}

TEST_CASE("the divider held on entry goes back when the walk ends")
{
    sourceOnTheBus(2250);
    SamplingLog log;

    log.sweep(0, 1000, 1400, 100, 10, 1);
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

TEST_CASE("the line rate comes off HPERIOD_IF against the chip's own 27 MHz")
{
    CHECK(SamplingLog::lineRateFromHPeriod(431) == 15625u);
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
