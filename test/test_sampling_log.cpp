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
#include <vector>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SamplingLog.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/gbs_types.h"

// The emitted line is the instrument, so one test reads it. A poll emits more
// than one kind, so the run is kept as well as the last line.
static std::string g_lastLine;
static std::vector<std::string> g_lines;
void tv5725Log(const char *line) { g_lastLine = line; g_lines.push_back(line); }

// SourceMeasurement.h declares the two the sketch supplies; nothing here spins
// for a field rate.
#include "DebugPinStub.h"
uint32_t debugPinSpanTicks(uint32_t pulses)
{
    return pulses * ticksForHz(0.0f);
}

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

// The sync width belongs in the ROW rather than in a second poll beside it.
//
// Measured on the Wii at 480p: the count reads 521 where the raster is 525
// lines, and reads the correct 524 only in ~45 ms bursts that carry the
// latched SOG switch with a dropped PLL lock and a railed HPERIOD_IF. Whether
// the sync width moves with that switch is the next question, and two readings
// taken through separate HTTP polls cannot answer it -- they are two states,
// not two instruments.
TEST_CASE("the row carries the sync processor's measured sync width")
{
    sourceOnTheBus(2250);
    GBS::STATUS_SYNC_PROC_HLOW_LEN::write(181);

    SamplingLog log;
    log.monitor(0, 10, 1000);
    log.poll(10);

    CHECK(g_lastLine.rfind("smp,", 0) == 0);
    CHECK(g_lastLine.find(",181,") != std::string::npos);
}

TEST_CASE("an event names the branch a decision took, with the count it took it on")
{
    // The sync watcher's RGBHV decisions are taken on a line count against a
    // threshold, inside loop(), and are over before any HTTP read can see
    // them. A register dump afterwards shows the destination and not the
    // choice, so the choice has to say so as it is made.
    g_lastLine.clear();

    SamplingLog::event(1234, "rgbhv-bypass", 627);

    CHECK(g_lastLine == "evt,1234,rgbhv-bypass,627");
}

TEST_CASE("an event is one line whatever the caller passes")
{
    g_lastLine.clear();

    SamplingLog::event(0, "rgbhv-scale", 311);

    CHECK(g_lastLine == "evt,0,rgbhv-scale,311");
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
    SamplingLog::event(10, "held", 311);
    g_lastLine.clear();

    SamplingLog::event(20, "held", 311);
    CHECK(g_lastLine.empty());

    SUBCASE("a different branch is news") {
        SamplingLog::event(30, "moved", 311);
        CHECK(g_lastLine == "evt,30,moved,311");
    }

    SUBCASE("the same branch on a different count is news") {
        SamplingLog::event(30, "held", 312);
        CHECK(g_lastLine == "evt,30,held,312");
    }
}

// What a run has to be compared on is where the engine SOLVED, and reading that
// over HTTP changes the outcome: /getreg is deferred to loop(), so a full dump
// per return is hundreds of requests through the very loop being measured. The
// affordable subset that was used instead left out VDS_HS_ST, and an output
// sync start is a pan.
static void seedSolvedOutput(uint16_t hsyncStart)
{
    GBS::VDS_HSYNC_RST::write(1915);
    GBS::VDS_VSYNC_RST::write(1125);
    GBS::VDS_HSCALE::write(850);
    GBS::VDS_VSCALE::write(512);
    GBS::VDS_DIS_HB_ST::write(99);
    GBS::VDS_DIS_HB_SP::write(1501);
    GBS::VDS_DIS_VB_ST::write(20);
    GBS::VDS_DIS_VB_SP::write(620);
    GBS::VDS_HS_ST::write(hsyncStart);
    GBS::VDS_HS_SP::write(62);
    GBS::VDS_VS_ST::write(4);
    GBS::VDS_VS_SP::write(8);
    GBS::IF_HSYNC_RST::write(1104);
    GBS::IF_HBIN_SP::write(336);

    // The memory window, which the display window sits inside: the strip
    // between them shows whatever the playback stage fetches.
    GBS::VDS_HB_ST::write(1897);
    GBS::VDS_HB_SP::write(8);
    GBS::VDS_VB_ST::write(1121);
    GBS::VDS_VB_SP::write(1);

    // The capture window, and the measured sync pulse the whole framing
    // coordinate system is anchored to.
    // docs/investigations/framing-is-anchored-to-a-measured-pulse.md
    GBS::IF_HB_SP2::write(132);
    GBS::IF_HB_ST2::write(1105);
    GBS::STATUS_SYNC_PROC_HLOW_LEN::write(181);

    GBS::PLL648_CONTROL_01::write(0x75);
}

// The columns, with the leader and the timestamp taken off, so a case pins the
// record without pinning the cadence it is written at.
static std::string solveColumns()
{
    for (size_t i = g_lines.size(); i-- > 0;) {
        const std::string &line = g_lines[i];
        if (line.rfind("sol,", 0) != 0 || line.rfind("sol,header", 0) == 0)
            continue;
        const size_t afterMs = line.find(',', 4);
        return line.substr(afterMs + 1);
    }
    return "";
}

static void driveMonitor(SamplingLog &log, uint32_t fromMs, uint32_t toMs)
{
    for (uint32_t now = fromMs; now <= toMs; now += 10)
        log.poll(now);
}

TEST_CASE("a monitor run reports the solved output from inside the loop")
{
    sourceOnTheBus(2250);
    seedSolvedOutput(10);
    SamplingLog log;

    log.monitor(0, 10, 10000);
    g_lines.clear();
    log.poll(10);

    CHECK(solveColumns() == "1915,1125,850,512,99,1501,20,620,10,62,4,8,1104,336,"
            "1897,8,1121,1,132,1105,117,181");
}

TEST_CASE("a solve that has not moved is not repeated")
{
    // The same reason a repeated decision is not emitted: the console is the
    // instrument every other reading is taken from, and a line per sample of a
    // solve that is holding drowns it.
    sourceOnTheBus(2250);
    seedSolvedOutput(10);
    SamplingLog log;
    log.monitor(0, 10, 10000);
    log.poll(10);
    g_lines.clear();

    driveMonitor(log, 20, 3000);

    CHECK(solveColumns().empty());
}

TEST_CASE("the measured sync pulse is reported but does not re-emit on its own")
{
    // It dithers by a unit continuously -- measured on the bench at 156/157 --
    // and triggering on it would emit a line twice a second saying the solve
    // had not moved. It matters only where it CROSSES, and the crossing shows
    // as the capture window moving, which does trigger.
    // docs/investigations/framing-is-anchored-to-a-measured-pulse.md
    sourceOnTheBus(2250);
    seedSolvedOutput(10);
    SamplingLog log;
    log.monitor(0, 10, 10000);
    log.poll(10);
    g_lines.clear();

    GBS::STATUS_SYNC_PROC_HLOW_LEN::write(180);
    driveMonitor(log, 20, 3000);

    CHECK(solveColumns().empty());
}

TEST_CASE("an output sync start that moved is news, because it is a pan")
{
    sourceOnTheBus(2250);
    seedSolvedOutput(10);
    SamplingLog log;
    log.monitor(0, 10, 10000);
    log.poll(10);
    g_lines.clear();

    seedSolvedOutput(62);
    driveMonitor(log, 20, 3000);

    CHECK(solveColumns() == "1915,1125,850,512,99,1501,20,620,62,62,4,8,1104,336,"
            "1897,8,1121,1,132,1105,117,181");
}

// The rows, with the leader and the timestamp taken off, newest last.
static std::vector<std::string> sampleColumns()
{
    std::vector<std::string> out;
    for (size_t i = 0; i < g_lines.size(); ++i) {
        const std::string &line = g_lines[i];
        if (line.rfind("smp,", 0) != 0 || line.rfind("smp,header", 0) == 0
            || line == "smp,done")
            continue;
        out.push_back(line.substr(line.find(',', 4) + 1));
    }
    return out;
}

// A fault that shows once every hundred seconds cannot be sampled at the rate
// the link will carry: at one row a sample the console delivers 138 a second
// and a run of 600 s is 83000 lines, which is 25 times what a change-triggered
// run emits for the same window -- so the interval, not the fault, is what
// decides how much of it is seen.
TEST_CASE("a run asked for changes only does not repeat a row that has not moved")
{
    sourceOnTheBus(2250);
    SamplingLog log;

    log.monitor(0, 10, 10000, true);
    log.poll(10);
    g_lines.clear();

    driveMonitor(log, 20, 3000);

    CHECK(sampleColumns().empty());
}

TEST_CASE("a count that moved is news")
{
    sourceOnTheBus(2250);
    SamplingLog log;
    log.monitor(0, 10, 10000, true);
    log.poll(10);
    g_lines.clear();

    GBS::STATUS_SYNC_PROC_VTOTAL::write(120);
    driveMonitor(log, 20, 3000);

    CHECK(sampleColumns().size() == 1);
}

// Both are known to move continuously on a state judged perfect, so triggering
// on either emits a row several times a second and buries the readings that do
// mean something. They stay in the row: a line carries its own context.
// docs/investigations/pllad-lock-is-a-duty-cycle-not-a-state.md,
// docs/investigations/hperiod-if-railing.md
TEST_CASE("the lock bit and the line period are reported but do not trigger a row")
{
    sourceOnTheBus(2250);
    SamplingLog log;
    log.monitor(0, 10, 10000, true);
    log.poll(10);
    g_lines.clear();

    GBS::STATUS_MISC_PLLAD_LOCK::write(1);
    GBS::HPERIOD_IF::write(500);
    driveMonitor(log, 20, 3000);

    CHECK(sampleColumns().empty());
}

// The rate a run achieved is what every count taken from it is divided by, and
// a dropped line cannot be told from a sample never taken. The device counts
// its own.
TEST_CASE("a run says how many samples it took and over how long")
{
    sourceOnTheBus(2250);
    SamplingLog log;
    log.monitor(0, 10, 100, true);
    driveMonitor(log, 10, 200);

    CHECK(g_lastLine == "smp,done,9,100");
}

// Count the emitted lines of one kind, header excluded.
static int emitted(const char *leader)
{
    const std::string header = std::string(leader) + "header";
    int seen = 0;
    for (const std::string &line : g_lines)
        if (line.rfind(leader, 0) == 0 && line.rfind(header, 0) != 0)
            ++seen;
    return seen;
}

// The run's closing line lands after the last window, so g_lastLine is not it.
static std::string lastOf(const char *leader)
{
    const std::string header = std::string(leader) + "header";
    std::string found;
    for (const std::string &line : g_lines)
        if (line.rfind(leader, 0) == 0 && line.rfind(header, 0) != 0)
            found = line;
    return found;
}

static std::vector<std::string> columnsOf(const std::string &line)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= line.size()) {
        const size_t comma = line.find(',', at);
        out.push_back(line.substr(at, comma == std::string::npos
                                      ? std::string::npos : comma - at));
        if (comma == std::string::npos)
            break;
        at = comma + 1;
    }
    return out;
}

// The sync separator measures the sync pulse once a LINE -- 15625 a second on
// the bench source -- so an interval run at its fastest samples one line in
// several hundred and a disturbance lasting a few frames cannot appear in it.
// A run that reads only this register keeps up, and the console carries it
// because the row is a WINDOW rather than a reading.
TEST_CASE("a width run emits one row per window and not one per reading")
{
    sourceOnTheBus(2250);
    GBS::STATUS_SYNC_PROC_HLOW_LEN::write(153);
    g_lines.clear();

    SamplingLog log;
    log.widths(0, 20, 100, 120, 200);
    for (uint32_t now = 1; now <= 100; ++now)
        log.poll(now);

    CHECK(emitted("wid,") == 5);
}

TEST_CASE("a window carries more readings than it has polls")
{
    sourceOnTheBus(2250);
    GBS::STATUS_SYNC_PROC_HLOW_LEN::write(153);
    g_lines.clear();

    SamplingLog log;
    log.widths(0, 20, 20, 120, 200);
    for (uint32_t now = 1; now <= 20; ++now)
        log.poll(now);

    const std::vector<std::string> row = columnsOf(lastOf("wid,"));
    CHECK(std::stoi(row[2]) > 20);
}

// Min and max alone say a window held an outlier; the count says whether it
// held one line or eighty, which is the difference between a stray reading and
// a disturbance lasting a field.
TEST_CASE("a window counts the readings below the band")
{
    sourceOnTheBus(2250);
    GBS::STATUS_SYNC_PROC_HLOW_LEN::write(1);
    g_lines.clear();

    SamplingLog log;
    log.widths(0, 20, 20, 120, 200);
    for (uint32_t now = 1; now <= 20; ++now)
        log.poll(now);

    const std::vector<std::string> row = columnsOf(lastOf("wid,"));
    CHECK(std::stoi(row[5]) == std::stoi(row[2]));
}

TEST_CASE("a window reports the lowest and highest reading it saw")
{
    sourceOnTheBus(2250);
    GBS::STATUS_SYNC_PROC_HLOW_LEN::write(150);
    Wire.drift(0, 0x19);
    g_lines.clear();

    SamplingLog log;
    log.widths(0, 20, 20, 120, 200);
    for (uint32_t now = 1; now <= 20; ++now)
        log.poll(now);

    const std::vector<std::string> row = columnsOf(lastOf("wid,"));
    CHECK(std::stoi(row[3]) < std::stoi(row[4]));
}

TEST_CASE("a width run stops once its duration is up")
{
    sourceOnTheBus(2250);
    SamplingLog log;

    log.widths(0, 20, 100, 120, 200);
    for (uint32_t now = 1; now <= 1000 && log.active(); ++now)
        log.poll(now);

    CHECK_FALSE(log.active());
}
