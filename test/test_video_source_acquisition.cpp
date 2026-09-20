// Host tests for VideoSourceAcquisition -- `make -C test input-acquisition`.
//
// It owns the tick and calls down for each piece, so what is asserted here is
// the OUTCOME: a source driven through this class alone reaches the same solved
// registers that driving Tv5725::VideoPath directly reaches. A test that only
// checked the call was forwarded would pass against a class that forwarded it
// to nothing useful. docs/video-source-acquisition.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "SolvedEngine.h"
#include "MeasuredSource.h"
#include "RegistersWritten.h"

#include "../GBSC-Pro-Source code/gbs-control/src/videosource/VideoSourceAcquisition.h"
#include "../GBSC-Pro-Source code/gbs-control/src/videosource/SyncRecovery.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SamplingClock.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncOnGreen.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/BringUp.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoRoute.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ColourSpace.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/HdBypass.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/FrameBuffer.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

using namespace Tv5725;

// What a 1080p raster affords this source: Axis::maximumCapture(1600), rounded
// even. Not a constant of the part -- change the output resolution and it
// changes with it.
static const uint16_t RasterDivider = 1446;

// The bench RiscPC as SolvedEngine seeds it, but solved through the layer
// rather than by calling the engine's own poll().
static void seedBenchSource()
{
    Wire.reset();
    poisonChip();
    Wire.lockSyncProcessor();
    g_fieldRate = 50.08f;
    seed(3, 0x01, 0, 12, 1915);
    seed(3, 0x02, 4, 11, 1124);
    seed(1, 0x0E, 0, 11, 1276);
    seed(0, 0x19, 0, 12, 181);
    seed(5, 0x12, 0, 12, 2553);
    seed(0, 0x1B, 0, 11, 311);
    seed(0, 0x16, 0, 1, 1);              // STATUS_SYNC_PROC_HSPOL, positive-going
    seed(0, 0x16, 3, 1, 1);              // STATUS_SYNC_PROC_VSACT, V on its own pin

    // A bus that answers. Nothing per-source is written to a board that may
    // not be there, so every case below needs this established first.
    Chip::holdPower(true);

    // A quiet interrupt byte. The poison sets every latched bit, and the
    // sync-separator one arms a re-measure on every pass.
    seed(0, 0x0F, 0, 8, 0);
}

// A field written straight into the fake's banks, so seeding an INPUT does not
// read as the code under test having written it.
static void seedField(uint8_t seg, uint8_t reg, uint8_t offset, uint8_t width,
                      uint32_t value)
{
    seed(seg, reg, offset, width, value);
}

static void seedSourceLines(uint16_t lines)
{
    seedField(0, 0x1B, 0, 11, lines);     // STATUS_SYNC_PROC_VTOTAL
}


// Whether V is arriving on the source's own pin. Read in the separate-sync
// configuration -- the separator out -- so it reports the SOURCE rather than
// the path.
static void seedVsyncActive(bool active)
{
    seedField(0, 0x16, 3, 1, active ? 1 : 0);   // STATUS_SYNC_PROC_VSACT
}

// What the sync processor counts along the line, against the divider the solve
// chose. Equal means the ADC is locked to the line being counted.
static void seedLineSamples(uint16_t samples)
{
    seedField(0, 0x17, 0, 12, samples);   // STATUS_SYNC_PROC_HTOTAL
}

// The processor counting a line the ADC is not clocking, which is what a lost
// lock looks like from here: the count is live, and the divider it was taken
// against is not the one in force.
static void seedLineSamplesUnlocked(uint16_t samples)
{
    Wire.lockSyncProcessor(false);
    seedLineSamples(samples);
}

// The layer with everything under it, driven the way loop() drives it: one tick
// an interval, so the steadiness run behind the source event is counted in the
// units the layer counts them in.
struct Acquiring {
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath path;
    VideoSourceAcquisition acquisition;
    uint32_t nowMs;

    Acquiring()
        : path(clock, sampling, framings), acquisition(sampling, path), nowMs(0) {}

    void start(const OutputMode *mode = &Mode1080p)
    {
        acquisition.setOutputResolution(mode);
        path.inputTimingsChanged(4);
    }

    bool poll()
    {
        nowMs += VideoSourceAcquisition::DetectionIntervalMs;
        return acquisition.poll(nowMs);
    }

    bool pollUntilSolved(uint8_t runs = 4)
    {
        for (uint16_t i = 0;
             i < runs * (SourceMeasurement::SteadySamples
                         + SourceMeasurement::LatchSettlePasses); ++i)
            if (poll())
                return true;
        return false;
    }

    void pollFor(uint8_t runs)
    {
        for (uint16_t i = 0;
             i < runs * (SourceMeasurement::SteadySamples
                         + SourceMeasurement::LatchSettlePasses); ++i)
            poll();
    }
};


// The register-level switch into pass-through. The engine decides, the switch
// is whoever knows how to move the route -- the sketch on the board, this here.
static unsigned g_passThroughSwitches = 0;
static void enterPassThrough()
{
    ++g_passThroughSwitches;

    // What the register-level switch does that the engine has to undo: the
    // route moves, the scaled blocks are left in reset because nothing scaled
    // is running, and the bring-up is armed because the chip has been
    // configured away from the scaling setup.
    VideoRoute::toHdBypassChannel();
    Chip::resetVideoBlocks();
    HdBypass::applyColourPath(Adc::inputIsComponent());
    BringUp::arm();
}

// A source the panel takes straight: progressive, above the line doubler, at a
// rate that reaches the sink.
static void seedPassThroughSource()
{
    seedField(0, 0x1B, 0, 11, 524);    // STATUS_SYNC_PROC_VTOTAL
    seedField(0, 0x19, 0, 12, 129);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x16, 0, 1, 0);       // STATUS_SYNC_PROC_HSPOL, negative-going
    seedField(0, 0x16, 3, 1, 1);       // STATUS_SYNC_PROC_VSACT, V on its own pin
    g_fieldRate = 60.0f;
}

// The sync-type probe, and how often it was asked. Whether a source carries its
// own V sync cannot be read back, so the engine is handed a function that says.
static bool g_hasOwnVsync = true;
static unsigned g_probeCalls = 0;
static bool probeOwnVsync()
{
    ++g_probeCalls;
    return g_hasOwnVsync;
}

// The divider the engine derives for the bench source, which is also what the
// sync processor counts along the line once the ADC is locked to it -- so the
// seeds below are taken from it rather than written out, and a count that does
// not match it is a source locked to every other hsync.
static const uint16_t BenchDivider = 2200;

// And for the 524-line 60 Hz mode the transition cases move to, which is
// progressive and negative-going. The bench runs exactly this on the Wii at
// 480p, 524 lines x 59.80 Hz.
// The measured kept-count ceiling, not the line counter's wall.
static const uint16_t VesaDivider = RasterDivider;

// The bench anchors: the raster this output asks for, and the divider the
// engine solves for a 311-line 50 Hz source -- not the 2553 the seed left,
// which is the previous load's.
static void checkBenchAnchors()
{
    CHECK(VideoProcessor::VDS_HSYNC_RST::read() == 1919);
    CHECK(Adc::PLLAD_MD::read() == BenchDivider);
    CHECK(InputFormatter::IF_HSYNC_RST::read() == BenchDivider / 2);
}

TEST_CASE("a source driven through VideoSourceAcquisition solves the same registers")
{
    seedBenchSource();
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    checkBenchAnchors();
}

TEST_CASE("the layer reports what the source is running")
{
    // The measurement is coordinated here, so this is where the answer comes
    // from. Asserted against the solve rather than against the seed: a
    // publisher wired to a second SourceMeasurement would read zero.
    seedBenchSource();
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    CHECK(unit.acquisition.sourceLineRateHz() == unit.sampling.lineRateHz());
    CHECK(unit.acquisition.sourceLineRateHz() != 0);
    CHECK(unit.acquisition.sourceFieldRateHz() == doctest::Approx(50.08f));

    // The bench source is a 15 kHz line, which is what decides whether bypass
    // can be displayed at all.
    CHECK(unit.acquisition.sourceLowLineRate());
}

TEST_CASE("detection runs on the layer's cadence, not on every call")
{
    // loop() goes round far faster than the interval, so a run counted per call
    // is not the same length as one counted per tick -- and every threshold
    // keyed on it means something different. The cadence is the layer's because
    // the layer owns the tick; the engine no longer sees a clock at all.
    seedBenchSource();
    Acquiring unit;
    unit.start();

    uint32_t now = 0;
    for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i) {
        now += VideoSourceAcquisition::DetectionIntervalMs;
        if (unit.acquisition.poll(now))
            break;
    }
    REQUIRE_FALSE(unit.path.changing());

    // The source moves, and the layer is hammered inside one interval.
    seed(0, 0x1B, 0, 11, 524);
    for (uint16_t i = 0; i < 200; ++i)
        unit.acquisition.poll(now);
    CHECK_FALSE(unit.path.changing());

    // On the cadence, the same source change is noticed.
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples
                        && !unit.path.changing(); ++i) {
        now += VideoSourceAcquisition::DetectionIntervalMs;
        unit.acquisition.poll(now);
    }
    CHECK(unit.path.changing());
}

// The run gate, on the tick rather than inside the engine. loop() reaches this
// layer directly rather than through the sync watcher, so the freeze five sketch
// functions honour reached nothing -- and a bench measurement that froze
// automation had the solver rewriting the windows underneath it.
static bool g_mayRun = true;
static bool runGate() { return g_mayRun; }

TEST_CASE("a shut gate stops the path writing anything")
{
    seedBenchSource();
    Acquiring unit;
    unit.acquisition.useRunGate(runGate);
    g_mayRun = false;

    unit.start();
    Wire.reset();
    poisonChip();

    CHECK_FALSE(unit.pollUntilSolved());
    CHECK(registersWritten() == 0);
}

TEST_CASE("the gate is asked per tick, so what it stopped resumes")
{
    // A change outstanding when the gate shuts is still outstanding when it
    // opens: the mode change is picked back up rather than lost.
    seedBenchSource();
    Acquiring unit;
    unit.acquisition.useRunGate(runGate);
    g_mayRun = false;

    unit.start();
    REQUIRE_FALSE(unit.pollUntilSolved());

    g_mayRun = true;
    REQUIRE(unit.pollUntilSolved());
    checkBenchAnchors();
}

TEST_CASE("no gate runs, which is what every caller did before")
{
    seedBenchSource();
    Acquiring unit;
    unit.start();

    CHECK(unit.pollUntilSolved());
}

// --- the idle pass ---------------------------------------------------------
//
// What the source IS, and whether it moved. Measured here because this class
// owns the tick: the run behind every answer is counted in detection passes,
// and a second party advancing it would make it a different length.
// docs/video-source-acquisition.md

TEST_CASE("the engine arms itself when the source line count changes")
{
    // A mode change IS a change in the source, and the sync processor's line
    // count is the engine's own measurement of it. Waiting to be told makes the
    // trigger a classification: the sketch reloads a preset when getVideoMode()
    // reports a different STANDARD, and two RISC OS modes that are nothing alike
    // can share one -- a 311-line and a 524-line RGBHV source are both filed
    // under PresetLoad::ScalingRgbhvStandard.
    //
    // Measured on the bench: a 524 -> 311 return reloaded its preset,
    // GBS_PRESET_ID 5 -> 21, and still held PLLAD_MD 1124 sixty seconds later
    // with every other reading healthy. The solve had completed against the
    // count taken while the source was still moving, and nothing re-armed when
    // it settled.
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(Adc::PLLAD_MD::read() == BenchDivider);

    // The source moves, and NOBODY tells the unit.path.
    seedField(0, 0x1B, 0, 11, 524);    // STATUS_SYNC_PROC_VTOTAL
    seedField(0, 0x19, 0, 12, 129);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x16, 0, 1, 0);       // STATUS_SYNC_PROC_HSPOL, negative-going
    seedField(0, 0x16, 3, 1, 1);       // STATUS_SYNC_PROC_VSACT, V on its own pin
    g_fieldRate = 60.0f;

    bool solved = false;
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples && !solved; ++i)
        solved = unit.poll();

    CHECK(solved);
    CHECK(Adc::PLLAD_MD::read() == VesaDivider);
    CHECK(InputFormatter::IF_PRGRSV_CNTRL::read() == 1);
}

TEST_CASE("the source is counted on a cadence, not once a loop pass")
{
    // The steadiness run behind sourceIsPresent() is counted in detection
    // passes, and loop() goes round far faster than the 20 ms the sketch's own
    // counters advance on -- so a run counted per pass is a different length
    // from one counted per tick, and every threshold keyed on it means
    // something else. The measurement is what the run is over, so the
    // measurement takes the cadence.
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(Adc::PLLAD_MD::read() == BenchDivider);

    seedSourceLines(524);
    seedField(0, 0x19, 0, 12, 129);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x16, 0, 1, 0);       // STATUS_SYNC_PROC_HSPOL, negative-going
    seedField(0, 0x16, 3, 1, 1);       // STATUS_SYNC_PROC_VSACT, V on its own pin
    g_fieldRate = 60.0f;

    // Inside one interval, so however many times loop() comes round the source
    // has not been seen to move.
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples; ++i)
        CHECK_FALSE(unit.acquisition.poll(unit.nowMs));
    CHECK(Adc::PLLAD_MD::read() == BenchDivider);

    bool solved = false;
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples && !solved; ++i)
        solved = unit.poll();

    CHECK(solved);
    CHECK(Adc::PLLAD_MD::read() == VesaDivider);
}

TEST_CASE("an interrupt re-measures a source whose line count did not move")
{
    // The line count is the only change sourceMoved() can see, so a source that
    // returns at the same count and a different field rate is invisible to it.
    // The chip latches the disturbance instead, and measuring is now an act that
    // moves the sampling clock, so it needs an event rather than a schedule.
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    SUBCASE("a quiet source is left alone") {
        g_fieldRateCalls = 0;
        for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i)
            CHECK_FALSE(unit.poll());
        CHECK(g_fieldRateCalls == 0);
    }

    SUBCASE("an interrupted one is measured again") {
        unit.acquisition.sourceInterrupted();
        g_fieldRateCalls = 0;
        CHECK(unit.pollUntilSolved());
        CHECK(g_fieldRateCalls > 0);
    }

    SUBCASE("a bypassed output is re-decided rather than left alone") {
        // Pass-through is a statement about the source, so the event that says
        // the source may have moved has to reach it. This one cannot stay: the
        // bench source is line-doubled and 15 kHz, which no panel takes raw.
        unit.path.setOutputMode(&ModeBypass);
        unit.acquisition.sourceInterrupted();
        CHECK(unit.pollUntilSolved(8));
        CHECK_FALSE(unit.path.outputMode()->isBypass());
    }
}

TEST_CASE("a disturbance answered by one re-measure does not arm a second")
{
    // The 640x480 -> 320x256 leg, measured on the bench: the source's own mode
    // change latches the separator interrupt, and by the time it lands the
    // count is already outside the source bounds, so the branch that reads the
    // latch is unreachable. The change is armed by the unsettled-count arm
    // instead, which leaves the latch set -- and it fires the moment the solve
    // lands, on the count that solve has just measured:
    //
    //     source moved: unsettled count (227 lines, solved 524)
    //     externalClockGenSyncInOutRate()
    //     source moved: interrupt (311 lines, solved 311)
    //
    // The second re-measure re-installs the reference sampling clock and blanks
    // the output again, so the encoder relocks twice on one mode change.
    // docs/investigations/the-reference-clock-is-applied-to-a-working-picture.md
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    seedSourceLines(57);
    unit.poll();
    unit.acquisition.sourceInterrupted();
    for (uint16_t i = 0; i < 4 * SourceMeasurement::SteadySamples + 1; ++i) {
        seedSourceLines((uint16_t)((i % 2) ? 191 : 292));
        unit.poll();
    }

    seedBenchSource();
    seedLineSamples(BenchDivider);

    unsigned solves = 0;
    for (uint16_t i = 0; i < 16 * SourceMeasurement::SteadySamples; ++i)
        if (unit.poll())
            ++solves;

    CHECK(solves == 1);
}

TEST_CASE("a source the panel takes straight is passed through, not scaled")
{
    // Pass-through is decided from the MEASUREMENT rather than from a
    // classification of the source: a raster above the line doubler at a rate
    // that reaches the sink arrives intact only by being handed over, because
    // the capture's write limit takes the sampling density away exactly as the
    // source gains detail. docs/capture-limits.md
    seedBenchSource();
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();

    REQUIRE(unit.pollUntilSolved(8));
    CHECK(unit.path.outputMode()->isBypass());
    CHECK(g_passThroughSwitches == 1);
}

TEST_CASE("pass-through is refused until the source has been measured")
{
    // BYPASS HANDS THE SOURCE'S OWN TIMING TO THE ENCODER, so a mode entered on
    // a rate nobody measured is one the display may show nothing at all for --
    // which reads as a scaler with no output rather than as a refused mode.
    // docs/rgbhv-bypass-trap.md
    //
    // The count alone is not enough to decide it. 524 lines needs no doubling
    // and clears the sink floor at 60 Hz, so a source read as 524 with no rate
    // measured looks passable on everything except the rate.
    seedBenchSource();
    seedPassThroughSource();
    g_fieldRate = 0.0f;
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();

    for (uint16_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        unit.poll();

    CHECK(g_passThroughSwitches == 0);
    CHECK_FALSE(unit.path.outputMode()->isBypass());

    SUBCASE("and taken once one arrives") {
        g_fieldRate = 60.0f;
        REQUIRE(unit.pollUntilSolved(12));
        CHECK(unit.path.outputMode()->isBypass());
    }
}

TEST_CASE("a pass-through source is not dropped because a measurement failed")
{
    // THE OTHER HALF OF THE SAME RULE. Not entering on an unmeasured source is
    // the safe answer one way round; not LEAVING on one is the safe answer the
    // other, because leaving re-solves the scaling path and a dropped
    // field-rate sample would cost the picture for a source that never moved.
    //
    // The route is decided only on a pass whose measurement completed, which is
    // what gives both halves the same answer without either being guessed at.
    seedBenchSource();
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    // The source has not moved; only the reading of it has stopped.
    g_fieldRate = 0.0f;
    for (uint16_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        unit.poll();

    CHECK(unit.path.outputMode()->isBypass());
}

TEST_CASE("pass-through refused leaves the same source scaled")
{
    // The interim stand-in for a per-source override. It cannot express one, so
    // it is off by default and is not the decision -- but where it is set, the
    // measurement does not get to overrule it.
    seedBenchSource();
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(false);
    unit.start();

    REQUIRE(unit.pollUntilSolved(8));
    CHECK_FALSE(unit.path.outputMode()->isBypass());
    CHECK(g_passThroughSwitches == 0);
}

TEST_CASE("a source below the line doubler is scaled even where pass-through is allowed")
{
    // The bench source: 15 kHz and line-doubled, which no panel takes raw.
    seedBenchSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();

    REQUIRE(unit.pollUntilSolved());
    CHECK_FALSE(unit.path.outputMode()->isBypass());
    CHECK(g_passThroughSwitches == 0);
}

TEST_CASE("the chosen output resolution survives a pass-through excursion")
{
    // A resolution and "hand the source over" are independent facts. Stored in
    // one field the second destroys the first, and the way back has to invent a
    // resolution nobody asked for -- which is how a 1024p choice came back as
    // 1080p after a round trip, in RAM and in the preferences file alike.
    seedBenchSource();
    seedPassThroughSource();

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start(&Mode1024p);
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    seedBenchSource();
    seedLineSamples(BenchDivider);

    unit.pollFor(8);

    REQUIRE_FALSE(unit.path.outputMode()->isBypass());
    CHECK(unit.path.outputMode()->frameLines() == Mode1024p.frameLines());
}

TEST_CASE("leaving pass-through puts the colour path back")
{
    // Pass-through takes the decimator's matrix out, because the HD bypass
    // channel carries the conversion itself. On the scaling path the matrix is
    // what makes an RGB source RGB, so left bypassed the picture comes back
    // with the green channel inverted -- magenta whites over green blacks.
    seedBenchSource();
    seedPassThroughSource();

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    seedBenchSource();
    seedLineSamples(BenchDivider);
    HdBypass::applyColourPath(Adc::inputIsComponent());
    BringUp::arm();
    REQUIRE(ColourSpace::DEC_MATRIX_BYPS::read() == 1);

    unit.pollFor(8);

    CHECK(ColourSpace::DEC_MATRIX_BYPS::read() == 0);
}

TEST_CASE("leaving pass-through releases the blocks pass-through held")
{
    // Entering pass-through leaves the memory blocks, both FIFOs, the
    // deinterlacer and the VDS in reset, because nothing scaled is running.
    // Nothing on this path claims them back -- the preset load that used to is
    // what the engine replaces -- so without this the output routes to a scaler
    // whose blocks are all still held and the sink reports no signal.
    seedBenchSource();
    seedPassThroughSource();

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    // After the re-seed, because seedBenchSource() poisons the bus and the
    // poison byte has this bit set -- asserted before it, the hold is undone
    // by the seeding and the check below passes against a chip nothing held.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    VideoRoute::toHdBypassChannel();
    Chip::resetVideoBlocks();
    BringUp::arm();
    REQUIRE(Chip::SFTRST_VDS_RSTZ::read() == 0);

    unit.pollFor(8);

    CHECK(Chip::SFTRST_VDS_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_MEM_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_MEM_FF_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_FIFO_RSTZ::read() == 1);
    CHECK(Chip::SFTRST_DEINT_RSTZ::read() == 1);
}

TEST_CASE("a resolution chosen while the source is passed through is recorded")
{
    // The measurement decides pass-through, so a resolution arriving now is not
    // a reason to leave it -- it is where the output returns when the source
    // stops qualifying. Refusing it instead sent the caller into a preset load,
    // which takes the chip off the bypass route with nothing telling the engine.
    seedBenchSource();
    seedPassThroughSource();

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    CHECK(unit.acquisition.setOutputResolution(&Mode1024p));
    CHECK(unit.path.outputMode()->isBypass());
}

TEST_CASE("a resolution chosen while passed through puts the chip back")
{
    // VideoPath configures the chip for the mode it is told, and a resolution is
    // not pass-through -- so being told one IS the leave, by the same route as
    // any other. Without that the raster solved landed on a chip whose VDS was
    // still held and whose route still went round it.
    seedBenchSource();
    seedPassThroughSource();

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());
    REQUIRE(Chip::SFTRST_VDS_RSTZ::read() == 0);

    unit.path.setOutputMode(&Mode720p);

    CHECK(Chip::SFTRST_VDS_RSTZ::read() == 1);
}

TEST_CASE("a source that changes under a bypassed output is solved for")
{
    // The bench fault: with the output bypassed the source changes mode, the
    // layer never looks, and every register stays sized for the mode that was
    // left. The divider is one of them, so the count is read through the wrong
    // sampling clock, never settles, and the solve that would re-derive it is
    // the thing that cannot be reached.
    // docs/investigations/leaving-bypass-needs-a-count-the-divider-cannot-give.md
    seedBenchSource();
    seedSourceLines(524);
    seedField(0, 0x19, 0, 12, 129);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x16, 0, 1, 0);       // STATUS_SYNC_PROC_HSPOL, negative-going
    seedField(0, 0x16, 3, 1, 1);       // STATUS_SYNC_PROC_VSACT, V on its own pin
    g_fieldRate = 60.0f;

    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // Passed through, on a raster the panel takes straight, with the divider
    // the bypass switch chose rather than the one the last solve did.
    unit.path.setOutputMode(&ModeBypass);
    REQUIRE(unit.path.outputMode()->isBypass());
    seed(5, 0x12, 0, 12, 1886);

    // and the source drops to the bench mode, which is 15 kHz and line-doubled,
    // so pass-through no longer reaches the panel at all.
    seedBenchSource();
    seedLineSamples(BenchDivider);

    CHECK(unit.pollUntilSolved(8));
    CHECK_FALSE(unit.path.outputMode()->isBypass());
    CHECK(Adc::PLLAD_MD::read() == BenchDivider);
}

TEST_CASE("a measurement under a bypassed output leaves the channel's divider alone")
{
    // prepareToMeasure() installs the engine's REFERENCE sampling clock so a
    // count is never taken through the last mode's divider. In pass-through the
    // divider in force is the CHANNEL's -- HdBypass::dividerFor(), which the
    // entry wrote and which HD_HSYNC_RST is sized for -- so it is already a
    // known value and the reference buys nothing. Installing it anyway leaves
    // the channel raster describing a line the ADC no longer delivers.
    // docs/investigations/the-reference-clock-is-applied-to-a-working-picture.md
    seedBenchSource();
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    // What the sketch's switch writes and this fake does not: the channel's
    // own divider, which HD_HSYNC_RST is then sized for.
    const uint16_t channelDivider = HdBypass::dividerFor(525 * 60);
    Adc::applyDivider(channelDivider);
    seedLineSamples(channelDivider);

    // The chip latches a disturbance. The source has not moved and the picture
    // is intact, so what a re-measure must not do is move the sampling clock
    // out from under the raster.
    unit.acquisition.sourceInterrupted();
    for (uint8_t i = 0; i < 8; ++i)
        unit.poll();

    CHECK(unit.path.outputMode()->isBypass());
    CHECK(Adc::PLLAD_MD::read() == channelDivider);
}

TEST_CASE("a source counted steadily and sampled at the chosen divider is acquired")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);        // the divider the solve writes
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();

    CHECK(unit.acquisition.sourceState() == VideoSourceAcquisition::SourceAcquired);
    CHECK(unit.acquisition.sourceIsPresent());
}



TEST_CASE("a source counted steadily at a line the ADC is not sampling is unlocked")
{
    seedBenchSource();
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // The processor loses the line the ADC is clocking, under a source
    // that has not moved.
    seedLineSamplesUnlocked(3250);

    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();

    CHECK(unit.acquisition.sourceState() == VideoSourceAcquisition::SourceUnlocked);
}

TEST_CASE("unlocked is not absent, because the two want opposite things")
{
    // Absent withholds maintenance and runs recovery; acquired does the
    // reverse. Unlocked wants recovery too, and reporting it as absent would be
    // right by accident -- but a caller that wants to tell a source that is
    // gone from one that is there and wrong cannot, and the sync-type re-probe
    // is only worth running on the second.
    seedBenchSource();
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // The processor loses the line the ADC is clocking, under a source
    // that has not moved.
    seedLineSamplesUnlocked(3250);
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();

    CHECK(unit.acquisition.sourceState() != VideoSourceAcquisition::SourceAbsent);
    CHECK_FALSE(unit.acquisition.sourceIsPresent());
}

TEST_CASE("a source is not present while a mode change is still working through")
{
    // sourceMoved() runs only on the idle pass, so a change in flight leaves
    // the last idle verdict standing -- and that verdict is `acquired`, taken
    // before the source moved. A gate reading it then withholds recovery for
    // exactly as long as the engine is failing to settle. Measured on the
    // bench: SP_VTOTAL 97 with the state still reading acquired.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();
    REQUIRE(unit.acquisition.sourceIsPresent());

    unit.start();

    CHECK(unit.path.changing());
    CHECK_FALSE(unit.acquisition.sourceIsPresent());
}

TEST_CASE("a count no source runs is absent whatever the sampling says")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    seedField(0, 0x1B, 0, 11, 97);        // the wrong sync path's count
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();

    CHECK(unit.acquisition.sourceState() == VideoSourceAcquisition::SourceAbsent);
}

// --- reacquiring the sync type, the escalation ladder's rung -----------------
//
// The recovery a source that will not lock eventually reaches. A held value
// that already agrees is the state that costs a standoff: SP_SOG_MODE 1 against
// a held type of separate, with no route back, because a correction asking the
// held value never fires.
// docs/investigations/the-gate-runs-a-ladder-that-is-not-safe-yet.md

TEST_CASE("a count that never settles leaves the divider the source was solved on")
{
    // Noise is not a divider fault, so there is nothing here for a rewrite to
    // put right -- and one would re-latch the ADC PLL on a source that is
    // already sampled correctly, restarting the settle every time the count
    // happened to hold. The divider is sized from the rate measured, so a run
    // that measures none writes nothing and the ladder escalates instead.
    //
    // The real 640x480 -> 320x256 leg is the opposite case, a count that is
    // wrong BECAUSE the divider is the previous mode's. The correction against
    // the divider in force resolves that, which is where it is tested.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(Adc::PLLAD_MD::read() == BenchDivider);

    for (uint16_t i = 0; i < 2 * SyncRecovery::CycleLength; ++i) {
        seedSourceLines((uint16_t)(191 + (i % 64)));
        unit.poll();
    }

    CHECK(Adc::PLLAD_MD::read() == BenchDivider);
    CHECK(unit.acquisition.sourceState() == VideoSourceAcquisition::SourceAbsent);
}

// A probe that finds own V sync used to put the pass counter back to 2, and it
// finds one on every cycle of a separate-sync source, so the ladder could never
// escalate past the re-probe's position -- a livelock rather than slow
// progress. Own V sync is proof of a SOURCE, which is a reason not to toggle
// the input; it is not a reason to forget what has already been tried.
// docs/known-issues.md, "The recovery ladder livelocks"
TEST_CASE("own V sync does not cap the ladder at the re-probe")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.path.useSyncTypeProbe(probeOwnVsync);
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    g_hasOwnVsync = true;
    g_logLines.clear();
    seedSourceLines(0);
    for (uint16_t i = 0; i < SyncRecovery::positionOf(SyncRecovery::FullReset) + 4;
         ++i)
        unit.poll();

    REQUIRE(loggedContaining("recovery: reprobe sync type at pass"));
    CHECK(loggedContaining("recovery: restart sampling clock at pass"));
    CHECK(loggedContaining("recovery: full reset at pass"));
}

TEST_CASE("own V sync keeps the ladder off the input toggle")
{
    // What the restart was buying: a live source must not have the mux moved
    // out from under it. That survives as the toggle's own precondition.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.path.useSyncTypeProbe(probeOwnVsync);
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    const uint8_t input = Adc::ADC_INPUT_SEL::read();

    g_hasOwnVsync = true;
    g_logLines.clear();
    seedSourceLines(0);
    for (uint16_t i = 0; i < SyncRecovery::CycleLength + 4; ++i)
        unit.poll();

    REQUIRE(loggedContaining("recovery: toggle input at pass"));
    CHECK(Adc::ADC_INPUT_SEL::read() == input);
}

TEST_CASE("an ordinary mode change reuses the sync type instead of re-probing")
{
    // THE SYNC TYPE IS A PROPERTY OF THE SOURCE, NOT OF ITS MODE, and it changes
    // far more rarely than the mode does. Probing costs the path settle plus the
    // probe's own window -- measured at 1.00 s of a 2.0 s transition on a
    // composite source, which has no V to arrive and so spends the whole window
    // every time. So a mode change reuses what is held and the escalation ladder
    // pays for a change of sync type, which is the rare event.
    // docs/investigations/own-vsync-probe-window.md
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(g_probeCalls == 1);

    seedPassThroughSource();
    bool solved = false;
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples && !solved; ++i)
        solved = unit.poll();
    REQUIRE(solved);

    CHECK(g_probeCalls == 1);
}

TEST_CASE("a count no source runs re-establishes the sync type")
{
    // The wrong sync path counts 97..137 on a 311-line source, held for twenty
    // seconds on the bench, and every one of those is outside the source
    // bounds. So the state that most needs the sync type re-probed is exactly
    // the state that used to guarantee it never would.
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(g_probeCalls == 1);

    seedSourceLines(97);
    for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i)
        unit.poll();

    CHECK(g_probeCalls == 2);
}

TEST_CASE("a count no source runs arms the probe once, not once a poll")
{
    // The probe moves the sync path and costs a settle plus a window. A fault
    // that persists is the normal case here -- the count stays wrong until the
    // probe has fixed the path -- so arming per poll is a probe storm.
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    seedSourceLines(97);
    for (uint8_t i = 0; i < 16 * SourceMeasurement::SteadySamples; ++i)
        unit.poll();

    CHECK(g_probeCalls == 2);
}

TEST_CASE("a source held as separate sync that drives no V re-establishes the sync type")
{
    // A composite-sync source the engine holds as separate-sync runs uncoasted:
    // the count is short by the lines the vertical pulse occupies -- 308 against
    // 311 -- and dithers, and the picture bounces. Every one of those counts is
    // PLAUSIBLE, so the unusable-count arm above never fires and the wrong path
    // stands for as long as the source is attached.
    //
    // The separate-sync answer is what puts the separator out, which is the
    // probe's own measuring configuration, so the V-active bit read there
    // reports whether the source drives V rather than which path is configured.
    // docs/investigations/a-sync-type-change-arms-no-probe.md
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(g_probeCalls == 1);
    REQUIRE_FALSE(SyncMeasurement::isCsync());

    seedVsyncActive(false);
    unit.pollFor(4);

    CHECK(g_probeCalls == 2);
}

TEST_CASE("a source that drives V is left on the sync type it was probed for")
{
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(g_probeCalls == 1);

    unit.pollFor(8);

    CHECK(g_probeCalls == 1);
}

TEST_CASE("V missing for one pass is not a sync type change")
{
    // The bit flickers while the sync processor settles, which is why the probe
    // re-confirms its own positive. A single sample must not cost the settle
    // and the window a re-probe spends.
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    seedVsyncActive(false);
    unit.poll();
    seedVsyncActive(true);
    unit.pollFor(4);

    CHECK(g_probeCalls == 1);
}

TEST_CASE("a source held as composite sync is not re-probed for the V it cannot drive")
{
    // On the composite-sync path the separator is IN, and the bit then reports
    // the path rather than the source -- it reads 0 for the whole time a
    // composite source is correctly configured. Arming on it there is a probe
    // every few hundred milliseconds, for ever.
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = false;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(g_probeCalls == 1);
    REQUIRE(SyncMeasurement::isCsync());

    seedVsyncActive(false);
    unit.pollFor(8);

    CHECK(g_probeCalls == 1);
}

TEST_CASE("a field rate the line count cannot show re-solves the source")
{
    // 320x256 at 50, 55 and 60 all count 312 lines, so the count says the
    // source did not move and the raster stays sized for the rate before it.
    // HPERIOD_IF reads 431 / 392 / 359 across those three and costs one
    // register read, so it is what NOTICES the change from the idle path --
    // where measuring the field rate would mean a vsync spin every pass. What
    // the rate is then measured with is the field rate.
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);     // HPERIOD_IF, this source at 50 Hz
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // The divider follows the measured rate rather than a cap.
    REQUIRE(Adc::PLLAD_MD::read()
            == SamplingClock::recommendedDivider(
                   unit.acquisition.sourceLineRateHz(), 4, true));

    // The same 311 lines at 60 Hz. What has to move is the HELD RATE.
    seedField(0, 0x06, 0, 9, 359);
    g_fieldRate = 60.29f;
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples; ++i)
        unit.poll();

    // The measured field rate over the source's own frame. The counter armed
    // the re-solve; it did not supply the number.
    CHECK(unit.acquisition.sourceLineRateHz()
          == Tv5725::VideoSignal::lineRateFor(311, 60.29f));
}

TEST_CASE("a rate seen once does not re-solve the source")
{
    // HPERIOD_IF rails, and the run it rails through passes every check the
    // rate judgement makes: three identical samples inside the agreement, and a
    // field rate well inside the bounds. Measured on the bench source it reads
    // 10 / 140 / 431 / 511 while the sync processor holds a perfect 311 -- so a
    // rate taken once arms a solve, the solve records the rail as the rate it
    // ran against, and the next rail differs again. Twenty-four probes in fifty
    // seconds, on a source that never moved.
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);     // HPERIOD_IF, this source at 50 Hz
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(g_probeCalls == 1);

    // One poll's worth of a railed reading, then the source's own rate back.
    for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i) {
        seedField(0, 0x06, 0, 9, i % 2 ? 255 : 431);
        unit.poll();
    }

    CHECK(g_probeCalls == 1);
}

TEST_CASE("a rate the field rate does not confirm leaves the source alone")
{
    // A rail that STAYS railed compares equal to a reference taken from it, so
    // the cheap half says nothing moved and nothing is spent. What this guards
    // is the other order: a reference taken before the rail, which does read as
    // a move -- and the field rate, measured a different way, then says the
    // source is where it was. Affordable because a corroborated disagreement is
    // rare.
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);     // HPERIOD_IF, this source at 50 Hz
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(g_probeCalls == 1);

    // The register rails and stays railed. The source has not moved.
    seedField(0, 0x06, 0, 9, 511);
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples; ++i)
        unit.poll();

    CHECK(g_probeCalls == 1);
}

TEST_CASE("a source the sync processor is counting is present")
{
    // The sketch's classifier reports 0 on an RGBHV source whose two STATUS_16
    // bits have gone quiet, and its no-sync handling then walks the ADC and the
    // sync processor off a source the engine is solving against correctly --
    // SP_H_PULSE_IGNOR 2 against the 255 applyForSyncType() writes, ADC_SOGCTRL
    // ratcheted 12 to 5, and a black screen.
    // docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    CHECK(unit.acquisition.sourceIsPresent());
}

TEST_CASE("a source that stops counting is not present")
{
    // The case the sketch's no-sync handling exists for, and the one it must
    // still reach: a signal that has genuinely gone.
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.sourceIsPresent());

    seedSourceLines(0);
    unit.poll();

    CHECK_FALSE(unit.acquisition.sourceIsPresent());
}

TEST_CASE("the ladder's position follows the layer's own measurement")
{
    // rto->noSyncCounter indexes SyncRecovery::stepAt() from the sketch, and it
    // advances on a source this class calls present -- which is what walks the
    // ADC and the sync processor off a source the engine is solving correctly.
    // The count belongs beside the measurement that decides it, and the
    // positions carry over unchanged because runSyncWatcher() is called on the
    // same 20 ms cadence as poll().
    // docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.recoveryDue() == SyncRecovery::None);

    seedSourceLines(0);
    for (uint16_t i = 0; i < SyncRecovery::FirstEscalationPass; ++i)
        unit.poll();

    CHECK(unit.acquisition.recoveryDue() == SyncRecovery::LiftSogFloor);
}

TEST_CASE("a source that comes back puts the ladder away")
{
    // Unlocked rather than absent, so the escalation is exercised against a
    // source that can then be re-acquired: the ladder must retreat, or a source
    // that recovers keeps taking recoveries it no longer needs.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.recoveryDue() == SyncRecovery::None);

    seedLineSamplesUnlocked(3250);
    for (uint16_t i = 0; i < SyncRecovery::FirstEscalationPass; ++i)
        unit.poll();
    REQUIRE(unit.acquisition.recoveryDue() != SyncRecovery::None);

    seedLineSamples(BenchDivider);
    unit.poll();

    CHECK(unit.acquisition.recoveryDue() == SyncRecovery::None);
}

TEST_CASE("a recovery names the rung it fired and the pass it fired at")
{
    // THE LADDER IS INVISIBLE OTHERWISE. Every rung is a register write made to
    // a source nobody can see, and only one of the eleven says anything -- so a
    // unit that has been hunting for a minute gives no account of what it has
    // already tried, and the position is the only thing that says which rungs
    // are still to come.
    seedBenchSource();
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    g_logLines.clear();
    seedSourceLines(0);
    for (uint16_t i = 0; i < 40; ++i)
        unit.poll();

    CHECK(loggedContaining("recovery: lift SOG floor at pass"));
}

TEST_CASE("a recovery that settles the question restarts the run")
{
    // Two rungs end the run rather than advancing it: a lock found on the other
    // ADC input, and a sync-type re-probe that finds no V sync. Whether a step
    // settled anything is the caller's to say, so it says so rather than
    // writing a sentinel into the count -- which is what 0x07fe was.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    seedLineSamplesUnlocked(3250);
    for (uint16_t i = 0; i < SyncRecovery::FirstEscalationPass; ++i)
        unit.poll();
    REQUIRE(unit.acquisition.recoveryDue() != SyncRecovery::None);

    unit.acquisition.restartRecovery();

    CHECK(unit.acquisition.recoveryDue() == SyncRecovery::None);
}

TEST_CASE("the run of acquired passes is the layer's too")
{
    // rto->continousStableCounter counts passes since the source came good, and
    // the sketch keys maintenance off it -- the sampling phase, the dynamic
    // sync-processor write, the deinterlacer. It is the same run as the ladder's
    // seen from the other side, so it is counted beside the same measurement.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    const uint16_t settled = unit.acquisition.acquiredPasses();
    REQUIRE(settled > 0);

    unit.poll();
    unit.poll();
    CHECK(unit.acquisition.acquiredPasses() == settled + 2);
}

TEST_CASE("a source that goes away zeroes the acquired run")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.acquiredPasses() > 0);

    seedLineSamplesUnlocked(3250);
    unit.poll();

    CHECK(unit.acquisition.acquiredPasses() == 0);
    CHECK(unit.acquisition.unmeasuredPasses() == 1);
}

TEST_CASE("the run counts detection passes, not loop passes")
{
    // loop() calls poll() every time round and runSyncWatcher() every 20 ms, so
    // a run counted per call is a different length from the one every threshold
    // was tuned against. It advances on the cadence, which is what makes
    // SyncRecovery's positions mean what they meant beside runSyncWatcher().
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    const uint16_t settled = unit.acquisition.acquiredPasses();

    uint32_t now = unit.nowMs;
    for (uint8_t i = 0; i < VideoSourceAcquisition::DetectionIntervalMs - 1; ++i)
        unit.acquisition.poll(++now);       // 1 ms apart, inside one interval

    CHECK(unit.acquisition.acquiredPasses() == settled);
}

TEST_CASE("the pass that advanced the run is the pass that says so")
{
    // The maintenance cadence has to run once per count or its thresholds mean
    // a different length of time from the one they were tuned against. loop()
    // used to gate it on a timer of its own beside this one, so the two drifted
    // and a count could be advanced twice or skipped.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    uint32_t now = unit.nowMs;
    for (uint8_t i = 0; i < VideoSourceAcquisition::DetectionIntervalMs - 1; ++i) {
        unit.acquisition.poll(++now);
        CHECK_FALSE(unit.acquisition.runAdvanced());
    }

    const uint16_t before = unit.acquisition.acquiredPasses();
    unit.acquisition.poll(++now);
    CHECK(unit.acquisition.runAdvanced());
    CHECK(unit.acquisition.acquiredPasses() == before + 1);
}

TEST_CASE("counts that never hold still are not a source")
{
    // The reverted attempt gated the sketch's recovery on the range check
    // alone, and an unlocked sync processor sits inside that range: 216, 271,
    // 276, 312, 305 measured over 80 s with the source genuinely gone, every
    // one of them plausible and every one of them meaningless.
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.sourceIsPresent());

    static const uint16_t unlocked[] = { 216, 271, 276, 312, 305 };
    for (uint8_t pass = 0; pass < 4; ++pass)
        for (uint8_t i = 0; i < 5; ++i) {
            seedSourceLines(unlocked[i]);
            unit.poll();
            CHECK_FALSE(unit.acquisition.sourceIsPresent());
        }
}

TEST_CASE("a source that cannot be measured is not present while a change is pending")
{
    // While a mode change is outstanding poll() takes the solving branch and
    // never reaches sourceMoved(), so presence kept its last idle answer for as
    // long as the solve went on failing -- and a solve that cannot measure is
    // exactly when the sketch's recovery has to run. Measured on the bench with
    // the answer wired to that gate: counts thrashing 236, 308, 438, 511 at
    // 0.00 Hz, the engine still reporting a source, and the output blanked
    // permanently because nothing was left to unstick it.
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.sourceIsPresent());

    // A count that holds still -- so the cheap gate passes -- with no field
    // rate behind it, which is what an unlocked sync processor produces.
    unit.start();
    g_fieldRate = 0.0f;
    seedSourceLines(283);
    for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i)
        unit.poll();

    CHECK_FALSE(unit.acquisition.sourceIsPresent());

    g_fieldRate = 50.08f;
}

TEST_CASE("nothing has been solved, so no source is present")
{
    // Boot, and every state that has forgotten the source. The sketch's
    // detection has to be free to run, so the engine must not claim a source it
    // has never measured.
    seedBenchSource();
    Acquiring unit;

    unit.poll();

    CHECK_FALSE(unit.acquisition.sourceIsPresent());
}


TEST_CASE("a count alternating by one is the same source, not a mode change")
{
    // SteadyRun treats a pair differing by one as agreeing, because an
    // interlaced field carries a half line and a run demanding identical
    // samples never completes on one. The source event compared a RAW sample
    // against the solve's raw sample instead, so on any source whose count
    // alternates the two disagreed on half the polls and each disagreement
    // armed a whole mode change -- a sync-type probe, a re-measure and a
    // re-solve, for ever.
    //
    // Measured on the bench RiscPC under composite sync, counting 308/309:
    // `source moved: count (309 lines, solved 308)` followed by
    // `own V sync: no after 1001ms` every two to four seconds, with the sink
    // reporting no signal throughout.
    // ../docs/known-issues.md
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(g_probeCalls == 1);

    for (uint8_t i = 0; i < 16 * SourceMeasurement::SteadySamples; ++i) {
        seedSourceLines(i % 2 == 0 ? 311 : 312);
        unit.poll();
    }

    CHECK(g_probeCalls == 1);
}

TEST_CASE("a count that moves by more than one is still a mode change")
{
    // The tolerance is one count, not a licence to ignore the count. 311 to 524
    // is a real mode change and has to arm one.
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE_FALSE(unit.path.changingMode());

    seedSourceLines(524);
    bool armed = false;
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples && !armed; ++i) {
        unit.poll();
        armed = unit.path.changingMode();
    }

    CHECK(armed);
}

TEST_CASE("a source the sync processor is counting is not searching")
{
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    CHECK_FALSE(unit.acquisition.sourceIsSearching());
}

TEST_CASE("a source that has stopped counting is searching")
{
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    seedSourceLines(0);
    unit.poll();

    CHECK(unit.acquisition.sourceIsSearching());
}

TEST_CASE("a live count stops the search on a source the run has given up on")
{
    // The sweep this gates walks a source off its settings, and an unlocked
    // source is still a source. Being wrong the other way costs one pass.
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    seedLineSamplesUnlocked(3250);
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();

    REQUIRE_FALSE(unit.acquisition.sourceIsPresent());
    CHECK_FALSE(unit.acquisition.sourceIsSearching());
}

TEST_CASE("the run stops the search across a count the sync processor lost")
{
    // One bad read is not a source going away, and the count is read live here
    // rather than off the run.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();
    REQUIRE(unit.acquisition.sourceIsPresent());

    seedSourceLines(0);

    CHECK_FALSE(unit.acquisition.sourceIsSearching());
}

// The sampling phase, which belongs here because its GATE does: the search is
// only worth running once the divider the sync processor counts against is the
// one the engine chose, and this is the class holding that measurement.
// Tv5725::Adc owns the two adjusters and needs no SourceMeasurement to do it.

static unsigned g_watchdogFeeds = 0;
static void countWatchdogFeed() { ++g_watchdogFeeds; }

TEST_CASE("an unlatched divider is not worth searching the phase for")
{
    // What the sync processor counts is what the ADC is RUNNING, and PLLAD_MD
    // reports what was last WRITTEN -- the two differ between a write and the
    // latch that loads it. Scored through the wrong clock every phase reads bad
    // and the search picks noise.
    seedBenchSource();
    Acquiring unit;
    unit.acquisition.useWatchdogFeed(countWatchdogFeed);
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // What the search is offered is a divider the ADC is not running.
    seedLineSamplesUnlocked(BenchDivider + 40);

    Adc::choosePhaseSyncProcessor(9);
    Adc::choosePhaseAdc(24);
    g_watchdogFeeds = 0;

    CHECK_FALSE(unit.acquisition.acquireSamplingPhase());
    CHECK(Adc::phaseSyncProcessor() == 9);
    CHECK(Adc::phaseAdc() == 24);
    CHECK(g_watchdogFeeds == 0);
}

TEST_CASE("a latched divider gets the search, and the watchdog is fed through it")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.useWatchdogFeed(countWatchdogFeed);
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    SyncOnGreen::choose(SyncOnGreen::DefaultLevel);
    g_watchdogFeeds = 0;

    CHECK(unit.acquisition.acquireSamplingPhase());
    CHECK(g_watchdogFeeds > 0);
}

TEST_CASE("a starved separator gets the mid of the field rather than a sweep")
{
    // The sweep scores phases by the sync processor's count, and a starved
    // separator makes that noise -- so there is nothing worth 34 steps of it.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.useWatchdogFeed(countWatchdogFeed);
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    SyncOnGreen::choose(SyncOnGreen::StarvedLevel);
    Adc::choosePhaseSyncProcessor(3);
    g_watchdogFeeds = 0;

    CHECK(unit.acquisition.acquireSamplingPhase());
    CHECK(Adc::phaseSyncProcessor() == 16);
    CHECK(g_watchdogFeeds == 0);
}

// The sync processor's three per-source writes. Each was a sketch wrapper
// around a Tv5725::SyncProcessor call, gathering its facts from this class and
// gating on rto->sourceDisconnected -- which is sourceIsSearching() spelled a
// second time, and the one that does not follow the measurement.

TEST_CASE("a source nothing is counting gets no window placed")
{
    // A window measured off a line nobody is sending clamps to picture or
    // coasts over the wrong part of the line, and the placement is what a live
    // count is the precondition for.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    seedSourceLines(0);
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();
    REQUIRE(unit.acquisition.sourceIsSearching());

    SyncProcessor::forgetPositions();
    unit.acquisition.placeCoastWindow(false);
    unit.acquisition.placeClampWindow();

    CHECK_FALSE(SyncProcessor::coastPlaced());
    CHECK_FALSE(SyncProcessor::clampPlaced());
}

TEST_CASE("a counted source gets both windows placed on its own line")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // The coast window is measured from the line HPERIOD_IF reports and the
    // clamp from what the sync processor counts, so both readings have to be
    // there before either can be placed.
    GBS::HPERIOD_IF::write(431);
    seedField(0, 0x16, 1, 1, 1);          // STATUS_SYNC_PROC_HSACT
    SyncProcessor::forgetPositions();
    unit.acquisition.placeCoastWindow(false);
    unit.acquisition.placeClampWindow();

    CHECK(SyncProcessor::coastPlaced());
    CHECK(SyncProcessor::clampPlaced());
}

TEST_CASE("an unpowered board gets no window and no dynamic write")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    SyncProcessor::forgetPositions();
    Chip::holdPower(false);

    unit.acquisition.placeCoastWindow(false);
    unit.acquisition.placeClampWindow();
    const uint32_t before = SyncProcessor::SP_H_PULSE_IGNOR::read();
    SyncProcessor::SP_H_PULSE_IGNOR::write(before ^ 0xff);
    unit.acquisition.applySyncProcessorDynamic(false);

    CHECK_FALSE(SyncProcessor::coastPlaced());
    CHECK_FALSE(SyncProcessor::clampPlaced());
    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == (before ^ 0xff));

    Chip::holdPower(true);
}

// --- the output sync across a mode change ------------------------------------
//
// The encoder samples the analog output and does not always notice the timing
// under it moved: it carries on transmitting the mode it locked to before and
// the panel shows nothing. Taking sync away is what makes it look again.
// Measured with the blank removed, a 320x256 -> 640x480 change left the panel
// dark for the whole 20 s it was watched, the TV locked and painting black with
// every scaler register correct.
// docs/investigations/encoder-stale-timing.md

// Whether the panel is being shown a picture, either way it can be taken away.
// The display aperture is what a scaled output blanks with, because closing it
// never reaches the encoder; the sync pad is what is left where there is no
// aperture in the path, and what the encoder is made to re-look with.
static bool outputBlanked()
{
    return Chip::PAD_SYNC_OUT_ENZ::read() == 1
           || VideoProcessor::VDS_DIS_VB_ST::read()
                  <= VideoProcessor::VDS_DIS_VB_SP::read() + 1;
}

TEST_CASE("a mode change takes the output sync away")
{
    seedBenchSource();
    Acquiring unit;
    unit.start();
    unit.poll();
    CHECK(outputBlanked());
}

TEST_CASE("the output sync comes back once the source is acquired")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    unit.poll();
    CHECK_FALSE(outputBlanked());
}

TEST_CASE("a re-arm takes the output sync away again")
{
    // The blank belongs to whatever change is outstanding, so a second change
    // arriving after a solve gets its own.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    unit.poll();
    REQUIRE_FALSE(outputBlanked());

    unit.path.inputTimingsChanged(4);
    unit.poll();
    CHECK(outputBlanked());
}

TEST_CASE("the output sync goes as soon as the source stops being acquired")
{
    // A MODE CHANGE IS DETECTED BEFORE IT IS ARMED. Measured on the
    // 640x480 -> 320x256 leg, the engine sees the source go at 27.11 and does
    // not arm until 27.59, because the unsettled-count arm has to sit through
    // its passes first -- and for that 0.48 s the panel is showing the previous
    // mode's geometry applied to a source that has left.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    unit.poll();
    REQUIRE_FALSE(outputBlanked());

    // One pass, on a count no source runs: too early for any arm.
    seedSourceLines(57);
    unit.poll();
    CHECK(outputBlanked());
}

TEST_CASE("the output sync comes back for a source that returns unchanged")
{
    // Nothing arms when the count comes back where it was, so the solve that
    // re-enables the pad never runs. Without this the blank above is a panel
    // that never lights again. docs/investigations/encoder-stale-timing.md
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    unit.poll();

    seedSourceLines(57);
    unit.poll();
    REQUIRE(outputBlanked());

    seedSourceLines(311);
    unit.pollFor(4);
    CHECK_FALSE(outputBlanked());
}

TEST_CASE("the encoder is made to look again only when the output timing moves")
{
    // THE SYNC PAD IS THE ENCODER'S AND NOTHING ELSE'S. Dropping it costs a full
    // sink re-acquisition -- 3.3 s to 5.6 s of dark panel after the pad comes
    // back, quantised, measured on the bench -- so it is spent only where the
    // encoder would otherwise carry on transmitting a timing that has moved
    // underneath it. Everything else blanks the display aperture, which the
    // encoder never sees.
    // docs/investigations/the-transition-is-mostly-the-encoder.md

    SUBCASE("a source that returns at the same timing never touches it") {
        seedBenchSource();
        seedLineSamples(BenchDivider);
        Acquiring unit;
        unit.start();
        REQUIRE(unit.pollUntilSolved());
        unit.poll();

        seedSourceLines(57);
        unit.pollFor(1);
        seedSourceLines(311);
        Wire.touched[0][0x49] = false;
        unit.pollFor(8);

        CHECK_FALSE(Wire.touched[0][0x49]);   // PAD_SYNC_OUT_ENZ
        CHECK_FALSE(outputBlanked());
    }

    SUBCASE("a source at a new field rate does") {
        seedBenchSource();
        seedLineSamples(BenchDivider);
        Acquiring unit;
        unit.start();
        REQUIRE(unit.pollUntilSolved());
        unit.poll();

        // The output frame time follows the source, so a new field rate is a new
        // display clock and the encoder is locked to the old one.
        seedField(0, 0x1B, 0, 11, 524);    // STATUS_SYNC_PROC_VTOTAL
        seedField(0, 0x19, 0, 12, 129);    // STATUS_SYNC_PROC_HLOW_LEN
        seedField(0, 0x16, 0, 1, 0);       // STATUS_SYNC_PROC_HSPOL
        seedField(0, 0x16, 3, 1, 1);       // STATUS_SYNC_PROC_VSACT, V on its own pin
        g_fieldRate = 60.0f;
        Wire.touched[0][0x49] = false;

        bool solved = false;
        for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples && !solved; ++i)
            solved = unit.poll();
        REQUIRE(solved);

        CHECK(Wire.touched[0][0x49]);   // PAD_SYNC_OUT_ENZ
    }
}

TEST_CASE("an output resolution change makes the encoder look again")
{
    // CHANGING THE OUTPUT MOVES THE RASTER, so the encoder is locked to a timing
    // that has gone and has to be made to look at the new one. No source solve
    // follows an output change -- the source has not moved -- so the re-look
    // cannot wait on one, and a pad left away is a panel that says no signal
    // with every register correct.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    unit.poll();
    REQUIRE_FALSE(outputBlanked());

    REQUIRE(unit.acquisition.setOutputResolution(&Mode720p));
    unit.poll();
    CHECK(Chip::PAD_SYNC_OUT_ENZ::read() == 1);

    // And it comes back on its own, without the source doing anything.
    for (uint8_t i = 0; i < 40; ++i)
        unit.poll();
    CHECK(Chip::PAD_SYNC_OUT_ENZ::read() == 0);
}

TEST_CASE("a mode change does not freeze the capture")
{
    // The blank covers the whole change, so there is nothing on the panel for a
    // frozen capture to hide. Measured, the freeze alone is not sufficient
    // anyway: with the blank removed and the freeze left, a 320x256 -> 640x480
    // change left the panel dark for the whole 20 s it was watched.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    seedField(4, 0x21, 0, 1, 1);          // CAPTURE_ENABLE, running
    Acquiring unit;
    unit.start();
    unit.poll();
    CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);

    REQUIRE(unit.pollUntilSolved());
    CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);
}

TEST_CASE("withdrawing the pass-through permission scales the same source")
{
    // The permission is the user's veto and it is changed while a source is
    // settled, so it takes effect against the source in force rather than at
    // the next one. Applied only on a measurement the source itself moved, the
    // picture stays handed over until something else disturbs it.
    seedBenchSource();
    seedPassThroughSource();

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    unit.acquisition.allowPassThrough(false);
    resolveUntilSolved(unit.acquisition);

    REQUIRE(unit.pollUntilSolved(8));
    CHECK_FALSE(unit.path.outputMode()->isBypass());
}

TEST_CASE("granting the pass-through permission hands the same source over")
{
    seedBenchSource();
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(false);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE_FALSE(unit.path.outputMode()->isBypass());

    unit.acquisition.allowPassThrough(true);
    resolveUntilSolved(unit.acquisition);

    CHECK(unit.path.outputMode()->isBypass());
    CHECK(g_passThroughSwitches == 1);
}
