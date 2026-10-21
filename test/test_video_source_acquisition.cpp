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
#include "../GBSC-Pro-Source code/gbs-control/src/videosource/VideoSourceSelection.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SamplingClock.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncOnGreen.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
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
static const uint16_t RasterDivider = 1444;

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

// The input formatter's own measurement of the frame, in half-lines, and the
// bit that says it completed. Live only where the separator is in the path,
// which is every composite or sync-on-green source.
static void seedFrame(uint16_t halfLines)
{
    seedField(0, 0x07, 1, 11, halfLines);   // VPERIOD_IF
    seedField(0, 0x00, 0, 1, 1);            // STATUS_IF_VT_OK
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
    InputFormatter inputFormatter;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath path;
    VideoSourceAcquisition acquisition;
    uint32_t nowMs;

    // VideoRoute is the chip's, so it outlives an instance the way it outlives
    // a reset. Boot starts on the scaler and so does every case.
    Acquiring()
        : sampling(inputFormatter), path(clock, sampling, framings, inputFormatter),
          acquisition(sampling, path), nowMs(0)
    {
        g_oneTimingRateHz = 0.0f;
        g_nextSpanRateHz = 0.0f;
        VideoRoute::toScaler();
    }

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
                         + VideoSourceAcquisition::LatchSettlePasses); ++i)
            if (poll())
                return true;
        return false;
    }

    // Poll for a stretch of the fake clock, for a case whose threshold is a
    // duration rather than a run.
    void pollForMs(uint32_t ms)
    {
        for (uint32_t elapsed = 0; elapsed < ms;
             elapsed += VideoSourceAcquisition::DetectionIntervalMs)
            poll();
    }

    void pollFor(uint8_t runs)
    {
        for (uint16_t i = 0;
             i < runs * (SourceMeasurement::SteadySamples
                         + VideoSourceAcquisition::LatchSettlePasses); ++i)
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
// Long enough that a source has plainly been absent rather than momentarily
// unread. No longer a firmware constant: nothing is timed against a source
// being gone, because every measurement a source change needs is taken when the
// source changes.
static const uint32_t AbsentForMs = 10000;

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
    CHECK(VideoProcessor::VDS_HSYNC_RST::read() == 1915);
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
                  + VideoSourceAcquisition::LatchSettlePasses); ++i) {
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
    // under the one scaling-RGBHV standard.
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
    //
    // **IT IS AN EVENT FOR THE RATE TO ANSWER, NOT AN ARM OF ITS OWN.** The
    // engine's own divider, clamp and coast placements switch the separator
    // too, and the chip latches that about a second after they land -- so a
    // solve re-arms itself for ever on a source standing still. Measured on a
    // component source at 480p the engine was measuring perfectly: 19 solves in
    // 60 s, each one blanking the output for an encoder relock, so a correct
    // measurement never reached the panel.
    // docs/investigations/the-reference-clock-is-applied-to-a-working-picture.md
    seedBenchSource();
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    SUBCASE("a quiet source is left alone") {
        g_fieldRateCalls = 0;
        for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + VideoSourceAcquisition::LatchSettlePasses); ++i)
            CHECK_FALSE(unit.poll());
        CHECK(g_fieldRateCalls == 0);
    }

    SUBCASE("an interrupted one whose rate has not moved is left alone") {
        unit.acquisition.sourceInterrupted();

        for (uint8_t i = 0;
             i < 4 * (SourceMeasurement::SteadySamples
                      + VideoSourceAcquisition::LatchSettlePasses); ++i)
            CHECK_FALSE(unit.poll());
    }

    SUBCASE("an interrupted one whose rate has moved is measured again") {
        g_fieldRate = 51.0f;
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
        VideoRoute::toHdBypassChannel();
        g_fieldRate = 51.0f;
        unit.acquisition.sourceInterrupted();
        CHECK(unit.pollUntilSolved(8));
        CHECK_FALSE(unit.path.outputMode()->isBypass());
    }
}

TEST_CASE("the separator interrupt reaches the rate only on the csync path")
{
    // STATUS_INT_SOG_SW says the sync separator switched, and a separate-sync
    // source does not go through one -- so the bit cannot be reporting that
    // source moving, and there it chatters. Measured: set in 9 of 30 reads over
    // twelve seconds with the count, the line total, the separator level and
    // the field rate all still. docs/sync-type-selection.md
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    SUBCASE("a separate-sync source is left alone") {
        REQUIRE_FALSE(SyncMeasurement::isCsync());

        g_fieldRateCalls = 0;
        for (uint8_t i = 0;
             i < 4 * (SourceMeasurement::SteadySamples
                      + VideoSourceAcquisition::LatchSettlePasses); ++i) {
            seedField(0, 0x0F, 1, 1, 1);     // STATUS_INT_SOG_SW, latched again
            CHECK_FALSE(unit.poll());
        }
        CHECK(g_fieldRateCalls == 0);

        // Taken all the same. The bit is latched, so one never acknowledged
        // reports the same disturbance on every later poll -- which is what a
        // gate written as a short circuit would leave behind.
        CHECK(Wire.touched[0][0x58]);
    }

    SUBCASE("a composite-sync source has its rate read") {
        SyncMeasurement::set(true);

        g_fieldRateCalls = 0;
        seedField(0, 0x0F, 1, 1, 1);         // STATUS_INT_SOG_SW, latched
        for (uint8_t i = 0;
             i < 4 * (SourceMeasurement::SteadySamples
                      + VideoSourceAcquisition::LatchSettlePasses); ++i)
            unit.poll();
        CHECK(g_fieldRateCalls > 0);
    }

    SUBCASE("a composite-sync source whose rate has moved is measured again") {
        SyncMeasurement::set(true);

        g_fieldRate = 51.0f;
        seedField(0, 0x0F, 1, 1, 1);         // STATUS_INT_SOG_SW, latched
        CHECK(unit.pollUntilSolved());
    }
}

TEST_CASE("a held rate the source has left is corrected without the source moving")
{
    // EVERY ARM IS A CHANGE DETECTOR -- the count, the interrupt, and the line
    // period the rate arm gates on -- so nothing asks whether what is HELD is
    // still true. A rate taken mid-change and wrong by a per cent is then kept
    // for as long as the source stands still.
    //
    // Measured on the bench: 50.766 Hz held against a source running 50.081,
    // for 65 s, with the state acquired, the count a clean 311 and every
    // register self-consistent, while the frame time lock slid a whole frame
    // every 17 s under it and only a re-detection cleared it.
    //
    // 1.37% is this instrument's mid-change population: a settled reading
    // spreads 0.000% and an unsettled one sits in a 1.3% band, so the error is
    // far above the noise and far below the gross-error net acceptance uses.
    // ../docs/investigations/the-rate-tolerance-answered-five-questions.md
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);   // HPERIOD_IF, steady, as the bench reads it
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.sampling.fieldRateHz() == doctest::Approx(50.08f).epsilon(0.001f));

    // The source running a rate the engine does not hold, with both registers
    // the engine watches -- the line count and the line period -- unmoved.
    g_fieldRate = 50.766f;
    for (uint16_t pass = 0;
         pass < 4 * VideoSourceAcquisition::RateRecheckPasses; ++pass)
        unit.poll();

    CHECK(unit.sampling.fieldRateHz() == doctest::Approx(50.766f).epsilon(0.001f));
}

TEST_CASE("the rate a boot's first solve took is re-measured once")
{
    // The first acquisition runs while WiFi and the filesystem are still
    // coming up, and the readings climb through it. Two agree on the way and
    // the pair is accepted:
    //
    //     sampling: 311 lines x 50.43 Hz -> line rate 15734
    //     sampling: 311 lines x 50.52 Hz -> line rate 15764
    //     sampling: 311 lines x 50.53 Hz -> line rate 15768   <- accepted
    //
    // against the 50.4744 four later re-acquisitions all gave, on one source
    // state. Nothing else can reach it: every arm is a change detector, and
    // 1.14 per thousand is inside RateCorroborationPerThousand, which cannot be
    // narrowed past the instrument's one-line quantisation step.
    // ../docs/known-issues.md
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);   // HPERIOD_IF, steady, as the bench reads it
    Acquiring unit;

    g_fieldRate = 50.5321f;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.sampling.settledFieldRateHz()
            == doctest::Approx(50.5321f).epsilon(0.0002f));

    // What the source was running all along, with the count and the line
    // period it was solved against both unmoved.
    g_fieldRate = 50.4744f;
    for (uint16_t pass = 0;
         pass < 2 * VideoSourceAcquisition::RateRecheckPasses; ++pass)
        unit.poll();

    CHECK(unit.sampling.settledFieldRateHz()
          == doctest::Approx(50.4744f).epsilon(0.0002f));
}

TEST_CASE("the confirmation drops the boot's rate rather than judging against it")
{
    // The worst boot reading measured is twice the source, and once held it is
    // what rateFollowsCount() judges every correct reading against -- so the
    // real rate is refused and the doubled one survives:
    //
    //     sampling: 311 lines x 100.90 Hz -> line rate 31481
    //     sampling: 311 lines x  50.46 Hz -> line rate 0
    //
    // Nothing else reaches it. A settled source takes no field-rate
    // measurement between solves, so the held rate is never challenged and
    // HeldRateRejectionLimit never drains, and every arm is a change detector.
    // ../docs/investigations/the-first-solve-of-a-boot-cannot-be-corroborated.md
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);   // HPERIOD_IF, steady, as the bench reads it
    Acquiring unit;

    g_fieldRate = 100.90f;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.sampling.settledFieldRateHz()
            == doctest::Approx(100.90f).epsilon(0.001f));

    // What the source was running all along, more than
    // RateFollowsCountPerThousand away from what the boot holds.
    g_fieldRate = 50.4744f;
    for (uint16_t pass = 0;
         pass < VideoSourceAcquisition::RateRecheckPasses
                + 4 * SourceMeasurement::SteadySamples; ++pass)
        unit.poll();

    CHECK(unit.sampling.settledFieldRateHz()
          == doctest::Approx(50.4744f).epsilon(0.001f));
}

TEST_CASE("an early arm does not spend the boot's one confirmation")
{
    // The confirmation is the RECHECK'S, not whatever reaches the rate arm
    // first. An arm raised inside the transient re-takes the same wrong rate,
    // and a confirmation spent there leaves the recheck corroborating it for
    // the life of the boot -- which is the defect, arrived at the long way.
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);   // HPERIOD_IF, as the bench reads it
    Acquiring unit;

    g_fieldRate = 50.5321f;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // The line period moving with the rate still the boot's. HPERIOD_IF is
    // read as a change detector, so this arms a re-measure on its own.
    seedField(0, 0x06, 0, 9, 400);
    unit.pollFor(8);
    REQUIRE(unit.sampling.settledFieldRateHz()
            == doctest::Approx(50.5321f).epsilon(0.0002f));

    g_fieldRate = 50.4744f;
    for (uint16_t pass = 0;
         pass < 2 * VideoSourceAcquisition::RateRecheckPasses; ++pass)
        unit.poll();

    CHECK(unit.sampling.settledFieldRateHz()
          == doctest::Approx(50.4744f).epsilon(0.0002f));
}

TEST_CASE("a rate confirmed once is not re-measured again")
{
    // One extra solve per boot. The confirmation is not a second corroboration
    // tolerance: past it the held rate is judged the way every other one is, or
    // a source drifting inside RateCorroborationPerThousand blanks the output
    // every RateRecheckPasses for the life of the boot.
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);
    Acquiring unit;

    g_fieldRate = 50.5321f;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    g_fieldRate = 50.4744f;
    for (uint16_t pass = 0;
         pass < 2 * VideoSourceAcquisition::RateRecheckPasses; ++pass)
        unit.poll();
    REQUIRE(unit.sampling.settledFieldRateHz()
            == doctest::Approx(50.4744f).epsilon(0.0002f));

    // The same distance again, which is a drift the corroboration is sized to
    // ignore.
    g_fieldRate = 50.4167f;
    for (uint16_t pass = 0;
         pass < 2 * VideoSourceAcquisition::RateRecheckPasses; ++pass)
        unit.poll();

    CHECK(unit.sampling.settledFieldRateHz()
          == doctest::Approx(50.4744f).epsilon(0.0002f));
}

TEST_CASE("one source mode change is one solve, however the latch falls")
{
    // The 640x480 -> 320x256 leg, measured on the bench: the source's own mode
    // change latches the separator interrupt, and by the time it lands the
    // count is already outside the source bounds, so the branch that reads the
    // latch is unreachable. The change is armed by the unsettled-count arm
    // instead, and a latch left set fires the moment the solve lands, on the
    // count that solve has just measured:
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

    // The leg lands on a count of its own, which is what makes it a mode change
    // rather than a wobble: the engine has one to arm on, and a latch left set
    // is what would arm the second solve after it.
    seedSourceLines(524);
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

TEST_CASE("a source passed through still reports the raster it measured")
{
    // PASS-THROUGH SPENDS THE MODE CHANGE THE ARM TOOK, so solveFromMeasurement()
    // never runs on this route and the scaling solve is not what names the
    // source here. A bypassed boot reported 0 lines at 0 Hz on the information
    // screen beside a working picture. Measured on the bench at 800x600@60 on
    // `vga`, across a boot and an input round trip.
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();

    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    CHECK(unit.path.reportedKey().valid());
    CHECK(unit.path.reportedKey().lines() == 525);
    CHECK(unit.path.reportedKey().rateHz() == doctest::Approx(60.0f).epsilon(0.01));
}

static unsigned g_frameLockForgotten = 0;
static void forgetNothing() { ++g_frameLockForgotten; }

// The load the sketch runs once a route has been claimed, with the sync watcher
// on as it is in service.
static void configureLoad(Acquiring &unit, bool scalingRgbhv = false)
{
    static bool disconnected;
    static uint8_t stage;
    disconnected = true;
    stage = 0;

    VideoSourceAcquisition::LoadState load;
    load.sourceDisconnected = &disconnected;
    load.applyPresetDoneStage = &stage;
    load.syncWatcherEnabled = true;
    load.scalingRgbhv = scalingRgbhv;
    unit.acquisition.configureAfterLoad(forgetNothing, load);
}

// The scaling RGBHV load had the same second owner the channel's entry had: a
// constant separator level, written over whatever was held for the source. What
// the load leaves is the level the input is due, which the search then refines.
// docs/investigations/the-separator-level-steadies-the-count-but-does-not-present.md
TEST_CASE("the scaling load hands the separator no level of its own")
{
    seedBenchSource();
    Acquiring unit;
    SyncMeasurement::set(true);
    SyncOnGreen::chooseForInput();
    const uint8_t due = SyncOnGreen::level();

    configureLoad(unit, true);

    CHECK(SyncOnGreen::level() == due);
}

// THE CLAMP IS WHAT RESTORES THE BLACK LEVEL, and holding it is a configuration
// the load takes on the way through rather than a state to be left behind. The
// channel's early return jumped the release, and the sketch's own recovery is
// gated on the window not yet being placed -- which the same sequence has
// already placed, so nothing downstream reaches it either. Measured on the Wii
// at 480p on `ypbpr` in pass-through: SP_NO_CLAMP_REG 1 against 0 on the
// scaling path and 0 on the stock build, with the capture dongle reading mean
// luma 0.00 and every configuration register correct.
TEST_CASE("a source passed through clamps like a scaled one")
{
    seedBenchSource();
    seedPassThroughSource();

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    configureLoad(unit);

    CHECK_FALSE(SyncProcessor::clampHeld());
}

// THE CHANNEL'S CLOCK IS INSTALLED BEFORE THE DUTY IS READ, which is the rule
// measureSource() already states for the scaling path. takeDuty() refuses a
// reading through a clock the PLL is not running, and reaching the channel's
// resize is gated on the duty -- so a clock the channel did not choose is one
// nothing downstream can replace.
//
// Measured on `vga`, the RISC PC at 800x600@60 with `SYNC 1` passed through to
// 1920x1080: one failed rate reading installs a reference, and from there 636
// identical `measured:` and `duty:` pairs across 75 s with PLLAD_MD 2040
// against STATUS_SYNC_PROC_HTOTAL 988, STATUS_MISC_PLLAD_LOCK 0 and the count
// read correctly throughout. Writing the channel's 2038 by hand through
// /sampleclock brought it back to acquired at 2038 latched.
TEST_CASE("a clock the channel did not choose is replaced before the duty is read")
{
    seedBenchSource();
    seedPassThroughSource();

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    const uint16_t channel =
        HdBypass::dividerFor(unit.sampling.lineRateHz());
    REQUIRE(channel != 0);

    // What the rate-failure retry leaves behind, with the processor counting the
    // line the PLL is actually running rather than the one written.
    unit.path.installReferenceClock();
    REQUIRE(Adc::PLLAD_MD::read() != channel);
    seedLineSamplesUnlocked(988);

    unit.path.inputTimingsChanged(4);
    unit.pollFor(8);

    CHECK(Adc::PLLAD_MD::read() == channel);
}

// AND IT CLAMPS A PULSE LATER THERE, because the channel's entry inverts the SOG
// reference for a component source and the clamp counts from that reference. Left
// where the scaling path puts it, the window lands on picture: measured on the Wii
// at 480p, a start of 19 against the 83 the inverted reference wants brings ~120
// columns of back porch through as a grey band at level 100 and lifts the frame's
// mean luma from 189 to 220.
//
// The offset is asked of SyncProcessor rather than recomputed, so this drives the
// real entry rather than setting the condition by hand.
TEST_CASE("the clamp follows the SOG reference the entry chose")
{
    seedBenchSource();
    seedPassThroughSource();

    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    GBS::HPERIOD_IF::write(431);
    seedField(0, 0x16, 1, 1, 1);          // STATUS_SYNC_PROC_HSACT

    SyncProcessor::applyForPassThrough(false);
    SyncProcessor::forgetPositions();
    unit.acquisition.placeClampWindow();
    const uint32_t upright = SyncProcessor::SP_CS_CLP_ST::read();

    SyncProcessor::applyForPassThrough(true);
    SyncProcessor::forgetPositions();
    unit.acquisition.placeClampWindow();
    const uint32_t inverted = SyncProcessor::SP_CS_CLP_ST::read();

    SyncProcessor::applyForPassThrough(false);

    CHECK(inverted - upright
          == SyncProcessor::clampOffsetForPulse(unit.sampling.hsync(),
                                                      unit.sampling.lineRateHz()));
    CHECK(inverted > upright);
}

// AND ON THE SCALING PATH IT FOLLOWS THE EDGE THE SYNC TYPE CHOSE, by the same
// pulse: composite sync references the leading edge, which moves the reference
// a pulse earlier, and a clamp left where the default edge put it sits on the
// sync tip. Measured on the Wii at 480p into 1920x1080: mean luma 143 to 162
// and the source's black bars grey until the window moved by the pulse.
TEST_CASE("the clamp follows the edge the sync type chose")
{
    seedBenchSource();

    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    GBS::HPERIOD_IF::write(431);
    seedField(0, 0x16, 1, 1, 1);          // STATUS_SYNC_PROC_HSACT

    SyncProcessor::applyForSyncType(false, Adc::dividerInForce());
    SyncProcessor::forgetPositions();
    unit.acquisition.placeClampWindow();
    const uint32_t trailing = SyncProcessor::SP_CS_CLP_ST::read();

    SyncProcessor::applyForSyncType(true, Adc::dividerInForce());
    SyncProcessor::forgetPositions();
    unit.acquisition.placeClampWindow();
    const uint32_t leading = SyncProcessor::SP_CS_CLP_ST::read();

    CHECK(leading > trailing);
    CHECK(leading - trailing
          == SyncProcessor::clampOffsetForPulse(unit.sampling.hsync(),
                                                unit.sampling.lineRateHz()));
}

// DETECTION TAKES THE ROUTE AWAY WITHOUT TELLING THE ENGINE. Low power
// detection routes the DACs back to the scaler, and the held output mode still
// says bypass -- so the switch that claims the route never runs again and the
// channel plays out into a route nothing selected.
// ../docs/investigations/low-power-detection-strands-pass-through-off-its-route.md
// WHAT A REPORT NAMES IS THE SOURCE, AND PASS-THROUGH SOLVES NOTHING. The key
// is adopted by the scaling solve, so a source handed over to the channel left
// the last SCALED source's key standing -- an info screen naming 311 lines
// while the channel carried a 524-line one.
TEST_CASE("a passed-through source names itself rather than the last scaled one")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.path.reportedKey().lines() == 312);

    unit.acquisition.allowPassThrough(true);
    seedPassThroughSource();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    CHECK(unit.path.reportedKey().lines() == 525);
}

TEST_CASE("a route taken away underneath pass-through is claimed again")
{
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();

    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(g_passThroughSwitches == 1);

    VideoRoute::toScaler();
    g_fieldRate = 61.0f;
    unit.acquisition.sourceInterrupted();

    REQUIRE(unit.pollUntilSolved(8));

    CHECK(g_passThroughSwitches == 2);
}

// A GUARD ON THE ONE STATE THAT COULD NOT ADVANCE ITSELF. Every arm in
// sourceMoved() compares against solvedLines_, which only a solve writes and
// only an arm opens, so a refusal to reach the arms while it is zero could
// never stop being true. sourceMoved() is not what gets here -- the mode change
// solves through prepareToMeasure() first -- and this holds that route open.
TEST_CASE("a bypassed output with nothing solved still arms the first count")
{
    seedBenchSource();
    seedPassThroughSource();

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start(&ModeBypass);

    REQUIRE(unit.path.outputMode());
    REQUIRE(unit.path.outputMode()->isBypass());

    CHECK(unit.pollUntilSolved(8));
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

// AND THE OTHER HALF OF THAT RULE NEEDS A WITNESS THE ROUTE CANNOT POISON.
// A source that changes under pass-through can become unmeasurable BECAUSE of
// the route: the bypass divider is sized from the rate held when the channel
// was entered, installSampling() leaves it alone there, and the sync processor
// counts in ADC clocks -- so a source the ADC PLL will not lock to through that
// divider is counted by nothing, and the pass that would doubt the route is the
// pass that cannot run. Waiting for a completed measurement waits for ever.
//
// The field rate is that witness. It is timed off vsync edges on DEBUG_IN_PIN
// rather than counted in ADC clocks, so it survives exactly what the count does
// not. Measured on the bench, 800x600@60 handed over and the source dropped to
// a 15 kHz mode: the count wandered 195..398 while the field rate read
// 50.47..51.13 against the 60.32 the channel was sized for, every pass.
// docs/investigations/the-reference-clock-can-deadlock-the-measurement.md
TEST_CASE("a field rate that moved in pass-through gives the route back")
{
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    // The bench shape: the source moved, and the ADC PLL will not lock to it
    // through the divider the channel was sized for -- so the duty read never
    // completes and the measurement never finishes. The field rate is taken
    // before that point and before anything the divider touches.
    const uint32_t movedAtMs = unit.nowMs;
    g_fieldRate = 50.6f;
    seedSourceLines(311);
    seedLineSamplesUnlocked(4077);
    for (uint16_t pass = 0; pass < 400 && unit.path.outputMode()->isBypass();
         ++pass)
        unit.poll();

    CHECK_FALSE(unit.path.outputMode()->isBypass());

    // Inside the budget a mode change is held to. This is not a stall to be
    // recovered from after a timeout -- the source said what it had done, on
    // the first pass that read it.
    CHECK(unit.nowMs - movedAtMs < 4000);

    Wire.lockSyncProcessor();
}

TEST_CASE("withdrawing the permission leaves pass-through without the source moving")
{
    // THE PERMISSION MOVES WHILE THE SOURCE STANDS STILL, which is the whole of
    // what the menu's Pass Through row and a resolution choice both do -- and
    // the route is re-answered only on a pass that re-resolves the source. With
    // nothing arming one, the picture stayed handed over until the source next
    // moved: selecting a resolution appeared to do nothing, and the row went on
    // reading ON however often it was pressed.
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    unit.acquisition.allowPassThrough(false);

    REQUIRE(unit.pollUntilSolved(8));
    CHECK_FALSE(unit.path.outputMode()->isBypass());
}

TEST_CASE("granting the permission hands a suitable source over without it moving")
{
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(false);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE_FALSE(unit.path.outputMode()->isBypass());

    unit.acquisition.allowPassThrough(true);

    REQUIRE(unit.pollUntilSolved(8));
    CHECK(unit.path.outputMode()->isBypass());
    CHECK(g_passThroughSwitches == 1);
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
    VideoRoute::toHdBypassChannel();
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

// --- losing presence takes a run, the way earning it does --------------------
//
// ONE SCATTERED COUNT IS NOT A SOURCE LEAVING. Earning presence takes
// SteadySamples agreeing counts, so a single scattered one costs that run its
// whole length and the verdict reads absent for every pass it takes to re-earn.
// Composite sync produces them on its own -- 175, 453 and 557 against a held
// 626, each followed by the right count again -- and the output drop already
// waits for the same reason. docs/known-issues.md

TEST_CASE("one scattered count does not take presence away")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();
    REQUIRE(unit.acquisition.sourceState() == VideoSourceAcquisition::SourceAcquired);

    seedSourceLines(557);
    unit.poll();
    seedSourceLines(311);

    bool left = false;
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i) {
        unit.poll();
        left = left
               || unit.acquisition.sourceState() != VideoSourceAcquisition::SourceAcquired;
    }

    CHECK_FALSE(left);
}

TEST_CASE("one scattered count does not break the acquired run")
{
    // What the flap costs, and why the run belongs on the verdict rather than
    // on one consumer: the frame time lock arms on FrameTimeLock::ArmPasses
    // unbroken acquired passes and the auto gain waits
    // SourceMaintenance::AutoGainReadyPasses, so a verdict that drops on every
    // scattered count zeroes the run both of them wait for and neither ever
    // runs -- under a picture that is perfect throughout.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();
    const uint16_t before = unit.acquisition.acquiredPasses();
    REQUIRE(before > 0);

    seedSourceLines(557);
    unit.poll();
    seedSourceLines(311);
    unit.poll();

    CHECK(unit.acquisition.acquiredPasses() == before + 2);
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

// AN UNLOCKED SOURCE ARMS NOTHING, so the engine names the fault and then waits
// on it. A selection edge installs the reference divider, and a detection that
// returns without solving leaves it in force -- at which point the count is
// plausible, it holds, and it equals the one the last solve measured, so every
// arm below declines while the ADC clocks a line the sync processor is not
// counting. Only a solve rewrites the divider, so nothing else can get out.
// Measured on the bench, 8 of 8 on a composite-sync source against 0 of 8 on a
// separate-sync one: selecting the input already in force lands here and stays
// for as long as it is left.
TEST_CASE("an unlocked source recovers, though its count has not moved")
{
    seedBenchSource();
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();

    // The divider in force is not the one the line is counted against, under a
    // source that has not moved.
    seedLineSamplesUnlocked(976);
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i)
        unit.poll();
    REQUIRE(unit.acquisition.sourceState() == VideoSourceAcquisition::SourceUnlocked);

    bool solved = false;
    for (uint16_t i = 0; i < 4 * VideoSourceAcquisition::AbsentPasses; ++i) {
        solved = unit.poll() || solved;
        // The ADC locks to whatever divider a solve writes, which is what makes
        // this recoverable at all and what the frozen fake cannot do by itself.
        if (unit.path.changing())
            Wire.lockSyncProcessor();
    }

    CHECK(solved);
    CHECK(unit.acquisition.sourceState() == VideoSourceAcquisition::SourceAcquired);
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

// A REFUSED SOLVE OUTLIVES THE SOURCE IT WAS REFUSED FOR. solveFromMeasurement()
// clears the mode change whether the windows solved or not, so a source that
// lands in pass-through next arrives with the deferred solve still outstanding
// -- and pass-through solves nothing, so nothing discharges it. The pass then
// reports a solve on every tick, which costs the presentation as well as the
// verdict: poll() leaves presenting to the passes that did not solve.
// docs/known-issues.md
TEST_CASE("a source passed through with a solve outstanding is present")
{
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());

    // What a solve refused against the departing source leaves behind.
    unit.path.deferSolve();
    for (uint16_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        unit.poll();

    CHECK(unit.acquisition.sourceIsPresent());
}

TEST_CASE("a count no source runs is absent whatever the sampling says")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // Held past AbsentPasses, because the verdict is reached over a run: a
    // count no source runs is still only one reading until it has persisted.
    seedField(0, 0x1B, 0, 11, 97);        // the wrong sync path's count
    for (uint16_t i = 0; i < VideoSourceAcquisition::AbsentPasses + 2; ++i)
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

// A REFERENCE CLOCK THE ADC PLL WILL NOT LOCK TO IS A DEADLOCK, because the
// engine installs a divider only FROM a measurement and the sync processor
// counts in ADC clocks: nothing is measured, so no divider is chosen, so
// nothing is ever measured. Measured on the Wii at 480i on ypbpr -- no
// measurement completes at the bring-up divider for as long as it is left, 60 s
// and five acts, with STATUS_SYNC_PROC_VTOTAL 97..156 against 525 and
// STATUS_MISC_PLLAD_LOCK 0 throughout, and a different divider is measured
// inside 0.1 s and acquires in 1.6 s. One reference is not enough.
// docs/investigations/the-reference-clock-can-deadlock-the-measurement.md
TEST_CASE("a measured source is not running a reference clock")
{
    // What the recovery asks before replacing a clock. A solved divider can land
    // on a reference's own value, so this is held from the measurement having
    // chosen rather than inferred by comparing the two -- compared, a source
    // solved onto a reference value would read as never having been measured
    // and have its clock taken away.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Adc::installReferenceSamplingClock();
    REQUIRE(Adc::referenceSamplingClockInForce());

    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    CHECK(!Adc::referenceSamplingClockInForce());
}

TEST_CASE("a count that dips between readings does not move the scan")
{
    // THE SCAN MODE IS A PROPERTY OF THE SOURCE, so it is decided from the
    // SETTLED count like every other decision here. Taken from the last reading
    // instead it follows a dip: any count from 200 to 1300 is a source, so one
    // below DoubleBelowLines reads as a short source and puts a progressive one
    // on the doubled scan at twice the divider, which the eleven-bit line
    // counter cannot hold -- IF_HSYNC_RST held at half, which IS the doubled
    // scan.
    //
    // Measured on the Wii at 480p into 1080p, from one measurement: 1446
    // undoubled, then 2200 doubled with the counter held at 1100, then 1446
    // again, and the count reading 524, 524, 524, then 390.
    // ../docs/known-issues.md
    seedBenchSource();
    seedSourceLines(524);

    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(!unit.path.lineDoubled());

    // The source moved, which is what arms the measurement again -- and the
    // first readings after one are the dips.
    unit.path.inputTimingsChanged(4);
    seedSourceLines(390);
    unit.poll();

    CHECK(!unit.path.lineDoubled());
}

TEST_CASE("a source the reference clock cannot measure is given another one")
{
    seedBenchSource();
    Adc::installReferenceSamplingClock();
    REQUIRE(Adc::dividerInForce() == Adc::BringUpDivider);

    // A CLOCK IS THE DIVIDER AND THE RATE ITS CROSSOVER ROW IS READ AGAINST,
    // so either moving is another clock: the row alone decides whether the PLL
    // locks, measured at one divider on two rows.
    const uint16_t divider = Adc::dividerInForce();
    const uint32_t rate = Adc::rateInForce();

    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();

    // The free-running count, which settles on no value, so no pass measures
    // the source and the divider is never chosen. COUNTED IN PASSES, not timed:
    // trying the next reference is part of measuring the source, and it waits
    // only as long as a reading takes to settle.
    for (uint16_t i = 0;
         i < VideoSourceAcquisition::ReferenceRetryPasses + 4; ++i) {
        seedSourceLines((uint16_t)(97 + (i % 60)));
        unit.poll();
    }

    REQUIRE(unit.acquisition.sourceState() == VideoSourceAcquisition::SourceAbsent);
    CHECK((Adc::dividerInForce() != divider || Adc::rateInForce() != rate));
}

TEST_CASE("a count that never steadies through a solved divider gets another clock")
{
    // A COUNT THAT NEVER STEADIES IS A CLOCK THAT CANNOT COUNT THE SOURCE, and
    // the recovery was reachable only from the Unmeasurable exit. measureRate()
    // answers NotSteady every pass where the count is outside the source bounds
    // -- sampleSteady() restarts the run and the pass returns before
    // measureLineRate(), so nothing is printed and no status the retry tests is
    // ever produced.
    //
    // Measured on `ypbpr` at 480p: 45 s and 97 s windows of total console
    // silence at `state: absent` with a full solve held, PLLAD_MD 1446 latched
    // against STATUS_SYNC_PROC_HTOTAL 1448 and STATUS_SYNC_PROC_VTOTAL 98, with
    // loop() demonstrably running. A recovery that cannot be reached is a stall.
    // ../docs/known-issues.md
    seedBenchSource();

    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(!Adc::referenceSamplingClockInForce());

    // The source moved, which is what the measurement is armed by -- the bench
    // arms it off the latched interrupt.
    unit.path.inputTimingsChanged(4);
    g_logLines.clear();

    // A count outside the source bounds, never twice the same, with the source
    // still pulsing -- which is what says the signal is there and this clock
    // cannot count it.
    // The settle first, because the state stays acquired through it and the
    // retry's own passes only start counting once a reading has failed.
    for (uint16_t i = 0;
         i < VideoSourceAcquisition::LatchSettlePasses
             + VideoSourceAcquisition::ReferenceRetryPasses + 4; ++i) {
        seedSourceLines((uint16_t)(97 + (i % 60)));
        unit.poll();
    }

    CHECK(loggedContaining("the reference clock measured nothing"));
}

TEST_CASE("each reference clock gets its own run of passes")
{
    // ONE CLOCK AT A TIME. Advancing on every pass past the threshold walks the
    // whole list in a few hundred milliseconds and gives none of them a chance
    // to be measured through -- and each advance re-latches the ADC PLL and
    // forgets the sync processor's positions, so the source is left worse than
    // any single wrong clock left it.
    seedBenchSource();
    Adc::installReferenceSamplingClock();
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();

    g_logLines.clear();
    for (uint16_t i = 0; i < 6 * VideoSourceAcquisition::ReferenceRetryPasses; ++i) {
        seedSourceLines((uint16_t)(97 + (i % 60)));
        unit.poll();
    }

    CHECK(loggedCountContaining("the reference clock measured nothing") <= 6);
}

TEST_CASE("a source that cannot be measured is still retried once the pass count saturates")
{
    // **A RETRY CLOCK MUST NOT SATURATE.** unmeasuredPasses_ stops at
    // AcquiredPassCeiling, so a retry keyed on the gap between it and the pass
    // an attempt was last made on stops firing five seconds in -- and the
    // engine then sits absent and silent for as long as the source is
    // attached, with nothing left that re-measures it.
    //
    // Measured on a component source at 480p: the source reads correctly at
    // the reference clock, a divider is chosen, the count falls to 97, and the
    // console then carries not one sampling line for 150 s across a
    // re-detection and a restart.
    seedBenchSource();
    Adc::installReferenceSamplingClock();
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();

    // Past the ceiling, so the saturation has happened before the window the
    // retry is asked about.
    for (uint16_t i = 0;
         i < VideoSourceAcquisition::AcquiredPassCeiling
                 + VideoSourceAcquisition::ReferenceRetryPasses; ++i) {
        seedSourceLines((uint16_t)(97 + (i % 60)));
        unit.poll();
    }

    g_logLines.clear();
    for (uint16_t i = 0;
         i < 4 * VideoSourceAcquisition::ReferenceRetryPasses; ++i) {
        seedSourceLines((uint16_t)(97 + (i % 60)));
        unit.poll();
    }

    CHECK(loggedCountContaining("the reference clock measured nothing") > 0);
}

// A DIVIDER A MEASUREMENT CHOSE CAN STILL FAIL TO MEASURE, which is what the
// guard on referenceSamplingClockInForce() assumed away. Measured on the Wii at
// 480p on ypbpr: the source is read correctly at the reference clock -- 524
// lines x 59.93 Hz, twice -- a divider of 1446 is chosen and latched, and
// through that divider the field rate reads a stable 39.93 Hz, exactly 1.5
// field periods, with the count a correct 524 and STATUS_SYNC_PROC_HTOTAL equal
// to PLLAD_MD. So the clock is locked, the count is right, and the rate cannot
// be measured through it -- and with no route back to the reference the engine
// never measures the source again.
TEST_CASE("a source its own solved divider cannot measure is given a reference back")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);

    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(!Adc::referenceSamplingClockInForce());

    // The count holds and the ADC stays locked to the line it is counting, so
    // nothing here is a source that left: only the rate stops being readable.
    // The count moves once, which is what arms a re-measure -- on the bench the
    // reading taken right after the latch read 214 lines where the source has
    // 524, and a moved count is accepted uncorroborated.
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples + 2; ++i) {
        seedSourceLines(214);
        unit.poll();
    }

    // From here the count is the source's again and the ADC stays locked to the
    // line it counts, so nothing says the source left: only the rate will not
    // read through the divider the measurement chose.
    // A rate the count refuses, steady across every reading -- 39.9 Hz against
    // a 50.08 Hz source is the shape the bench produced, where a silent source
    // would keep its divider instead.
    g_fieldRate = 39.9f;
    for (uint16_t i = 0; i < 4 * VideoSourceAcquisition::ReferenceRetryPasses; ++i) {
        seedSourceLines(311);
        seedLineSamples(BenchDivider);
        unit.poll();
    }

    CHECK(Adc::referenceSamplingClockInForce());
}

TEST_CASE("a source change puts the sync processor in the state an unmeasured source is counted through")
{
    // **THE ENTRY SEQUENCE IS: DETECT THE CHANGE, BLANK THE OUTPUT, RETURN THE
    // BLOCKS THE MEASUREMENT READS TO A DEFINED STATE, THEN MEASURE.** Step
    // three is the one that was missing: the coast window and the clamp are
    // what an unmeasured source is counted within, and left from the previous
    // source they can be a window this one is not inside -- then
    // STATUS_SYNC_PROC_HTOTAL does not follow the divider even written and
    // latched by hand, which is the bench signature.
    //
    // A DEFINED STATE, NOT A RESET. Pulsing SFTRST_SYNC_RSTZ zeroes SP_RT_HS_SP
    // and leaves the block with no origin to retime from, which is worse than
    // what it was found in -- measured on the bench as a source that stopped
    // acquiring at all.
    seedBenchSource();
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // A coast window the previous source left, which this one is not inside.
    seedField(5, 0x4D, 0, 12, 0x600);    // SP_H_CST_ST
    seedField(5, 0x4F, 0, 12, 0x700);    // SP_H_CST_SP

    unit.path.inputTimingsChanged(4);

    CHECK(SyncProcessor::SP_H_CST_ST::read() == 0x10);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 0x100);
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

TEST_CASE("a source that stops driving V while it is being measured is re-probed")
{
    // The arm above is asked only between mode changes. A source that changes
    // its sync type changes its count for a moment as the mode is re-applied,
    // so the engine is INSIDE a measurement when the separator is needed and
    // nothing in that loop asks the question: it cycles the reference clocks
    // through the separate arrangement until the caller gives up and drops to
    // low power, about seventy seconds later.
    // docs/known-issues.md
    seedBenchSource();
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(g_probeCalls == 1);
    REQUIRE_FALSE(SyncMeasurement::isCsync());

    // The source becomes composite: the count moves for a moment, which arms a
    // measurement, and through the separate arrangement nothing steadies and V
    // is not seen on its own pin.
    g_hasOwnVsync = false;
    seedSourceLines(307);
    for (uint16_t i = 0; i < 4 * SourceMeasurement::SteadySamples
                         && !unit.path.changingMode(); ++i)
        unit.poll();
    REQUIRE(unit.path.changingMode());
    seedVsyncActive(false);

    for (uint16_t i = 0;
         i < 6 * (SourceMeasurement::SteadySamples
                  + VideoSourceAcquisition::LatchSettlePasses); ++i) {
        seedSourceLines((uint16_t)(97 + (i % 60)));
        unit.poll();
    }

    CHECK(g_probeCalls == 2);
    CHECK(SyncMeasurement::isCsync());
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
          == Tv5725::VideoSignal::lineRateFor(312, 60.29f));
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
                  + VideoSourceAcquisition::LatchSettlePasses); ++i) {
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

    // A count of nothing is one reading like any other -- the verdict is
    // reached over AbsentPasses, so a source has to stay gone to read gone.
    seedSourceLines(0);
    for (uint16_t i = 0; i < VideoSourceAcquisition::AbsentPasses + 2; ++i)
        unit.poll();

    CHECK_FALSE(unit.acquisition.sourceIsPresent());
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
    // the recovery's budget mean what it meant beside runSyncWatcher().
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

    // NEVER HOLDING STILL IS THE POINT, so the run can never complete and the
    // verdict is reached on the count having failed for AbsentPasses instead.
    // Asked from there on rather than from the first reading: one scattered
    // count is a miscount, and what tells the two apart is that these keep
    // coming.
    static const uint16_t unlocked[] = { 216, 271, 276, 312, 305 };
    bool present = false;
    for (uint16_t pass = 0; pass < 4 * VideoSourceAcquisition::AbsentPasses; ++pass) {
        seedSourceLines(unlocked[pass % 5]);
        unit.poll();
        if (pass >= VideoSourceAcquisition::AbsentPasses)
            present = present || unit.acquisition.sourceIsPresent();
    }

    CHECK_FALSE(present);
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
                  + VideoSourceAcquisition::LatchSettlePasses); ++i)
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

// The count the chip miscounts alternates further than an interlaced field's
// half line, and the spread that forgives one did not reach it.
//
// Measured on the Wii at 480p, 110 s of console: 400 `source absent: 524 lines`
// and 400 `source acquired: 521 lines`, alternating ~3.6 times a second with
// nothing else between them. 524 is correct for a 525-line raster; 521 is the
// chip losing three. Each crossing restarted the presence run AND armed a
// re-solve, so the source was never present for two consecutive polls.
TEST_CASE("a count alternating by three is the same source, not a mode change")
{
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(g_probeCalls == 1);

    for (uint8_t i = 0; i < 16 * SourceMeasurement::SteadySamples; ++i) {
        seedSourceLines(i % 2 == 0 ? 311 : 308);
        unit.poll();
    }

    CHECK(g_probeCalls == 1);
    CHECK(unit.acquisition.sourceIsPresent());
}

TEST_CASE("a count the input formatter's frame contradicts is not the source moving")
{
    // TWO MEASUREMENTS OF ONE FRAME, and on a composite source the sync
    // processor's is the unreliable one. Measured on `vga` at 800x600@60
    // composite in pass-through, sampled from inside loop(): the count takes 76
    // distinct values from 0 to 1937 and is wrong in 54% of samples, while
    // VPERIOD_IF reads 1255 half-lines -- the source's own 627.5-line frame --
    // in 52%. A wrong count repeats for up to eight consecutive samples, so no
    // steadiness run can reject it, and during the longest such run the
    // formatter went on reading 1255. The pair cannot be reconciled, which is
    // what says the counter lost a vertical pulse.
    // ../docs/investigations/the-composite-sync-count-re-arms-its-own-solve.md
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.sourceIsPresent());

    g_logLines.clear();
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples; ++i) {
        seedSourceLines(451);
        seedFrame(623);
        unit.poll();
    }

    CHECK_FALSE(loggedContaining("TIMINGS change (count)"));
}

TEST_CASE("a count the formatter contradicts for long enough is re-measured")
{
    // A contradicted reading is dropped before the state or any arm is reached,
    // which is right for the pulse a composite separator loses for a few
    // samples and wrong for ever: a source that changed mode under a divider
    // the processor cannot count it through contradicts the formatter on every
    // pass, so the previous solve stood with the state acquired and nothing
    // armed. Measured on `vga` returning from 800x600@60 to 320x256@50 on
    // composite sync: STATUS_SYNC_PROC_VTOTAL 160 through the 1342 divider
    // against VPERIOD_IF 623, acquired and silent for as long as it was left.
    // ../docs/known-issues.md
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.sourceIsPresent());

    g_logLines.clear();
    for (uint8_t i = 0; i < 20 * SourceMeasurement::SteadySamples; ++i) {
        seedSourceLines(160);
        seedFrame(623);
        unit.poll();
    }

    CHECK(loggedContaining("TIMINGS change (contradicted count)"));
}

TEST_CASE("a contradiction at the count already solved does not arm for ever")
{
    // The arm above exists to escape a solve the processor cannot count
    // through, which shows as a count far from the solved one. A count that
    // EQUALS the solved one has nothing to escape to: the re-measure returns
    // the same reading, contradicts again, and arms again.
    //
    // Measured on `ypbpr`, the Wii in PAL 576i: VPERIOD_IF 624 against a count
    // of 310, where reconciledFrame() refuses 625 whole lines as 314 above the
    // count and skips halving it because 625 is odd. The console printed
    // `contradicted count), 310 lines, solved 310` every 2.5 s for as long as
    // it was left, dropping the sync pad for about 1.2 s of each cycle -- the
    // emitted frame pure black, and auto gain never completing, which is the
    // magenta. ../docs/known-issues.md
    // SEEDED BEFORE THE SOLVE, because that is where the source is. Measured
    // on the Wii in 576i over `ypbpr`, 50 samples across 75 s: VPERIOD_IF 624,
    // STATUS_SYNC_PROC_VTOTAL 310 and STATUS_IF_VT_OK 1, one value each with
    // no spread, and no arm in the window. So the formatter's frame does not
    // move here and the refusal below still holds. A fixture that moves it
    // after the solve describes a source that changed mode, which is the
    // opposite case and the one the test below it covers.
    seedBenchSource();
    seedFrame(1249);
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.sourceIsPresent());
    const uint16_t solved = unit.sampling.sourceLines();

    // seedSourceLines() writes the REGISTER, which counts from zero, so the
    // frame the engine reported back is seeded one short of itself.
    g_logLines.clear();
    for (uint8_t i = 0; i < 20 * SourceMeasurement::SteadySamples; ++i) {
        seedSourceLines((uint16_t)(solved - 1));
        seedFrame(1249);
        unit.poll();
    }

    CHECK_FALSE(loggedContaining("TIMINGS change (contradicted count)"));
}

TEST_CASE("a count frozen at the solved one arms when the formatter's frame moved")
{
    // **A SYNC PROCESSOR THAT STOPS COUNTING HOLDS ITS LAST VALUE EXACTLY**,
    // which every arm reads as a source standing still -- they all compare
    // against solvedLines_, and the frozen count IS solvedLines_. Measured on
    // `vga` composite going 320x256@50 -> 640x512@50:
    // STATUS_SYNC_PROC_VTOTAL 308 in every reading across 45 s, where the same
    // register dithers 305..309 on a healthy source, while VPERIOD_IF read the
    // arriving 533 with STATUS_IF_VT_OK 1. Acquired and silent, for as long as
    // it was left.
    //
    // The formatter's frame is the second instrument and it is the one that
    // followed the source, 623 to 533. That is what separates this from the
    // 576i case above, where it never moves.
    // docs/investigations/the-coast-window-blocks-a-source-it-has-lost.md
    seedBenchSource();
    seedFrame(623);
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.sourceIsPresent());
    const uint16_t solved = unit.sampling.sourceLines();

    g_logLines.clear();
    for (uint8_t i = 0; i < 20 * SourceMeasurement::SteadySamples; ++i) {
        seedSourceLines((uint16_t)(solved - 1));
        seedFrame(533);
        unit.poll();
    }

    CHECK(loggedContaining("TIMINGS change (contradicted count)"));
}

TEST_CASE("a frozen count arms where the formatter's frame is gated off")
{
    // **THE SECOND INSTRUMENT IS NOT ALWAYS THERE.**
    // InputFormatter::verticalPeriod() returns 0 while STATUS_IF_VT_OK reads 0,
    // and both guards above read 0 as nothing moved -- so where the bit is
    // clear the frozen count equals the solve, the frame says nothing, and
    // neither of them can arm. Measured on `vga` composite into 640x512@50,
    // where the bit reads 0 for as long as the state lasts: VPERIOD_IF carried
    // the arriving frame correctly underneath the gate, and the engine held
    // `state: acquired` on the departing mode's timing for over five minutes.
    //
    // What is left is the pair rateMoved() already reads -- HPERIOD_IF 431
    // when solved against 251 here, and no vsync edge on DEBUG_IN_PIN at all.
    // A field rate that cannot be measured cannot refuse a line period that
    // moved, so it must not be the reading that settles it.
    // docs/investigations/the-formatters-frame-is-gated-off-when-the-count-stops.md
    seedBenchSource();
    seedFrame(623);
    seedField(0, 0x06, 0, 9, 431);        // HPERIOD_IF, this source at 50 Hz
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.sourceIsPresent());
    const uint16_t solved = unit.sampling.sourceLines();

    // The mode change, as the state reads: the count frozen at the solve, the
    // formatter's reading thrown away by its own bit, the line period moved,
    // and the pin quiet.
    seedField(0, 0x00, 0, 1, 0);          // STATUS_IF_VT_OK
    seedField(0, 0x06, 0, 9, 251);        // HPERIOD_IF, the arriving mode
    g_fieldRate = 0.0f;

    g_logLines.clear();
    for (uint8_t i = 0; i < 20 * SourceMeasurement::SteadySamples; ++i) {
        seedSourceLines((uint16_t)(solved - 1));
        unit.poll();
    }

    CHECK(loggedContaining("TIMINGS change (rate)"));
}

TEST_CASE("a mode change the formatter's frame agrees with still arms one")
{
    // The corroboration refuses a count the formatter contradicts, not a count
    // it has not seen yet. A source that really moved moves BOTH readings, so
    // they reconcile at the new frame and the arm fires as it always did.
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    g_logLines.clear();
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples; ++i) {
        seedSourceLines(524);
        seedFrame(1049);
        unit.poll();
    }

    CHECK(loggedContaining("TIMINGS change (count)"));
}

TEST_CASE("a count twice the solved one at an unchanged line period is not a move")
{
    // THE CORROBORATION CANNOT REFUSE THESE. Both halves derive from the same
    // separated vertical, so when the separator loses a pulse they lose it
    // together and agree on a doubled frame -- 27 count arms in 125 s survived
    // the corroboration that way, each one discarding a complete solve.
    //
    // Doubling the count at an unchanged field rate doubles the LINE rate, so a
    // source that really moved there moves the line period with it. A period
    // that held still is what says the frame did not.
    // ../docs/investigations/the-composite-sync-count-re-arms-its-own-solve.md
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);   // HPERIOD_IF, steady, as the bench reads it
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(unit.acquisition.sourceIsPresent());

    g_logLines.clear();
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples; ++i) {
        seedSourceLines(622);
        seedFrame(1245);
        unit.poll();
    }

    CHECK_FALSE(loggedContaining("TIMINGS change (count)"));
}

TEST_CASE("a count that doubled because the line rate did is a mode change")
{
    // The escape the arithmetic needs. 320x256@50 to 800x600@60 is 2.02 times
    // the count, which the multiple test cannot tell from a lost pulse -- and
    // the line period halves across it, which is what leaves the arm standing.
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    g_logLines.clear();
    seedField(0, 0x06, 0, 9, 214);   // 37.9 kHz against the 15.6 it solved at
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples; ++i) {
        seedSourceLines(622);
        seedFrame(1245);
        unit.poll();
    }

    CHECK(loggedContaining("TIMINGS change (count)"));
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

    // Searching is the verdict's answer too -- a source is searched for once
    // it reads absent, which takes AbsentPasses.
    seedSourceLines(0);
    for (uint16_t i = 0; i < VideoSourceAcquisition::AbsentPasses + 2; ++i)
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

TEST_CASE("a solved source has its coast window placed from the line rate")
{
    // A window left at its bring-up default coasts over the wrong part of the
    // line, and the count that produces is several lines short of the source's
    // own -- which reads as a source that moved, so the solve repeats for ever.
    // Measured on the Wii at 480p: 524 lines in 95% of samples against 11.7%.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    SyncProcessor::forgetPositions();
    SyncProcessor::applyDefaultCoastWindow();
    REQUIRE(unit.pollUntilSolved());

    CHECK(SyncProcessor::coastPlaced());
    CHECK(SyncProcessor::SP_H_CST_SP::read() != SyncProcessor::DefaultCoastStop);
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
    unit.acquisition.applySyncProcessorDynamic();

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
static bool syncPadAway() { return Chip::PAD_SYNC_OUT_ENZ::read() == 1; }

static bool outputBlanked()
{
    return Chip::PAD_SYNC_OUT_ENZ::read() == 1
           || VideoProcessor::VDS_DIS_VB_ST::read()
                  <= VideoProcessor::VDS_DIS_VB_SP::read() + 1;
}

// What the pad costs is the DROP -- a full sink re-acquisition -- so what a test
// asks is whether it was ever taken away, not whether s0_49 was written. The
// engine restores a pad it did not take down, because a second writer on the
// power path can leave one disabled, and that write is free.
static bool syncPadEverTakenAway()
{
    for (size_t i = 0; i < Wire.trace.size(); ++i) {
        const FakeTwoWire::Traced &w = Wire.trace[i];
        if (w.segment == 0 && w.reg == 0x49 && (w.value & 0x04) != 0)
            return true;
    }
    return false;
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

    // On a count no source runs, and inside OutputBlankPasses: still well
    // ahead of any arm.
    seedSourceLines(57);
    for (uint16_t i = 0; i < VideoSourceAcquisition::OutputBlankPasses; ++i)
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
    for (uint16_t i = 0; i < VideoSourceAcquisition::OutputBlankPasses; ++i)
        unit.poll();
    REQUIRE(outputBlanked());

    seedSourceLines(311);
    unit.pollFor(4);
    CHECK_FALSE(outputBlanked());
}

// UNDER PASS-THROUGH THE PAD IS THE ONLY MECHANISM. There is no display
// aperture to blank -- video routes around the VDS entirely -- so the twin
// above cannot cover this route, and a source that drops and returns unchanged
// leaves the encoder transmitting the timing it had.
// docs/investigations/encoder-stale-timing.md
TEST_CASE("a pass-through source that returns unchanged gets the sync pad back")
{
    seedPassThroughSource();
    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    unit.poll();
    REQUIRE(unit.path.outputMode()->isBypass());
    REQUIRE_FALSE(syncPadAway());

    seedSourceLines(0);
    unit.pollFor(1);
    REQUIRE(syncPadAway());

    seedPassThroughSource();
    unit.pollFor(4);
    CHECK_FALSE(syncPadAway());
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
        Wire.trace.clear();
        unit.pollFor(8);

        CHECK_FALSE(syncPadEverTakenAway());
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
        Wire.trace.clear();

        bool solved = false;
        for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples && !solved; ++i)
            solved = unit.poll();
        REQUIRE(solved);

        CHECK(syncPadEverTakenAway());
    }
}

TEST_CASE("a source that comes back raises the whole output, not just the pad")
{
    // Low power takes the output down as a unit -- OUT_SYNC_CNTRL, the DAC power
    // and, through setResetParameters() zeroing segment 0, the sync pad -- and
    // the only thing that ever wrote them back was doPostPresetLoadSteps(). A
    // source that returns and acquires without a preset load on the way leaves
    // the panel dark with state acquired and every geometry register correct.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    unit.poll();
    REQUIRE_FALSE(outputBlanked());

    // The teardown, as goLowPowerWithInputDetection() writes it, behind the
    // engine's back. The pad is left alone: setResetParameters() zeroes segment
    // 0, which ENABLES it, and the only thing that drives it high is the
    // engine's own blank, which knows it did.
    Chip::OUT_SYNC_CNTRL::write(0);
    Chip::DAC_RGBS_PWDNZ::write(0);

    seedSourceLines(0);
    for (uint8_t i = 0; i < 8; ++i)
        unit.poll();
    seedBenchSource();
    seedLineSamples(BenchDivider);
    REQUIRE(unit.pollUntilSolved(8));
    for (uint8_t i = 0; i < 40; ++i)
        unit.poll();

    CHECK(Chip::PAD_SYNC_OUT_ENZ::read() == 0);
    CHECK(Chip::DAC_RGBS_PWDNZ::read() == 1);
    CHECK(Chip::OUT_SYNC_CNTRL::read() == 1);
}

TEST_CASE("a count that wobbles does not drop the output")
{
    // Measured on the Wii at 480p on ypbpr, whose vertical count alternates
    // 521/524 in bursts: a pair three apart restarts the idle steadiness run,
    // the state reads absent for the few passes it takes to re-earn, and the
    // output was dropped on every one of them -- 111 blanks in 30 s on a solve
    // that never moved, with no arm and no install behind any of them. The
    // panel flickers and the encoder relocks each time.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    for (uint8_t i = 0; i < 40; ++i)
        unit.poll();
    REQUIRE(Chip::PAD_SYNC_OUT_ENZ::read() == 0);

    SUBCASE("a pair the steadiness run cannot hold is ridden out") {
        for (uint8_t burst = 0; burst < 6; ++burst) {
            for (uint8_t i = 0; i < 6; ++i) {
                seedSourceLines(burst % 2 ? 308 : 311);
                unit.poll();
            }
        }
        CHECK_FALSE(outputBlanked());
    }

    SUBCASE("a source that stays away is still dropped") {
        seedSourceLines(0);
        for (uint16_t i = 0; i < VideoSourceAcquisition::OutputBlankPasses; ++i)
            unit.poll();
        CHECK(outputBlanked());
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

// --- the transition presents once, after the whole setup -------------------
//
// A mode change blanks on the pass that arms it, measures until every fact has
// settled, sets the mode up once, and presents once the divider has latched and
// the sampling phase has been searched. Nothing the setup writes lands after the
// sync pad returns, so the sink locks to the line the source will keep.

// Where in the trace the pad was last driven, or -1 for never.
static long lastPadDrive()
{
    long at = -1;
    for (size_t i = 0; i < Wire.trace.size(); ++i) {
        const FakeTwoWire::Traced &w = Wire.trace[i];
        if (w.segment == 0 && w.reg == 0x49 && (w.value & 0x04) == 0)
            at = (long)i;
    }
    return at;
}

// Writes to what a setup moves -- the divider, the sampling phases, the raster
// and the aperture -- after the pad was last driven.
static unsigned setupWritesAfterPadDriven()
{
    const long from = lastPadDrive();
    unsigned n = 0;
    for (size_t i = from < 0 ? 0 : (size_t)from + 1; i < Wire.trace.size(); ++i) {
        const FakeTwoWire::Traced &w = Wire.trace[i];
        const bool divider = w.segment == 5 && (w.reg == 0x12 || w.reg == 0x13);   // PLLAD_MD
        const bool phase = w.segment == 5 && (w.reg == 0x18 || w.reg == 0x19);     // PA_ADC_S, PA_SP_S
        const bool raster = w.segment == 3 && (w.reg == 0x01 || w.reg == 0x02);    // VDS_HSYNC_RST
        const bool aperture = w.segment == 3 && w.reg >= 0x10 && w.reg <= 0x12;    // VDS_DIS_HB_*
        if (divider || phase || raster || aperture)
            ++n;
    }
    return n;
}

// How many times a divider was installed: writes of its low byte to PLLAD_MD.
static unsigned dividerWrites(uint16_t divider)
{
    unsigned n = 0;
    for (size_t i = 0; i < Wire.trace.size(); ++i)
        if (Wire.trace[i].segment == 5 && Wire.trace[i].reg == 0x12
            && Wire.trace[i].value == (divider & 0xFF))
            ++n;
    return n;
}

// Passes until the output is on, bounded well past the latch and the hold.
static uint16_t pollUntilPresented(Acquiring &unit)
{
    uint16_t passes = 0;
    while (outputBlanked() && passes < 80) {
        unit.poll();
        ++passes;
    }
    return passes;
}

TEST_CASE("a mode change takes the sync pad away when it is armed, not when it is solved")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    pollUntilPresented(unit);
    REQUIRE_FALSE(syncPadAway());

    // A new source: the arm is the first thing that knows the picture is stale.
    seedPassThroughSource();
    bool armed = false;
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples && !armed; ++i) {
        REQUIRE_FALSE(unit.poll());
        armed = unit.path.changingMode();
    }
    REQUIRE(armed);
    CHECK(syncPadAway());
}

TEST_CASE("the output is presented once the divider has latched and the phase is searched")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Adc::forgetPhase();
    Acquiring unit;
    unit.acquisition.useWatchdogFeed(countWatchdogFeed);
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // The solve is not the presentation.
    CHECK(syncPadAway());
    CHECK(outputBlanked());

    REQUIRE(pollUntilPresented(unit) < 80);
    CHECK_FALSE(syncPadAway());
    CHECK(Adc::phaseFound());
    CHECK(setupWritesAfterPadDriven() == 0);
}

TEST_CASE("the output is not presented on the pass that solved")
{
    // The caller matches the output rate to the source when poll() reports a
    // solve, after it returns, and the rate belongs before the encoder sees
    // the line -- so even with the hold long elapsed, the picture waits a pass.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Adc::forgetPhase();
    Acquiring unit;
    unit.acquisition.useWatchdogFeed(countWatchdogFeed);
    unit.start();
    unit.poll();
    unit.nowMs += 2 * VideoSourceAcquisition::MinimumSyncAwayMs;
    REQUIRE(unit.pollUntilSolved());
    CHECK(outputBlanked());

    unit.poll();
    CHECK_FALSE(outputBlanked());
}

TEST_CASE("the divider is chosen against the output the arriving rate will run")
{
    // A 75 Hz source falls back to 1024p. The 60 Hz source that follows returns
    // to 1080p, and its divider has to be the 1080p raster's, installed once --
    // not the fallback raster's, kept because the rate had not moved.
    seedBenchSource();
    seedSourceLines(630);
    seedField(0, 0x19, 0, 12, 129);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x16, 0, 1, 0);       // STATUS_SYNC_PROC_HSPOL, negative-going
    g_fieldRate = 84.68f;
    Acquiring unit;
    unit.start(&Mode1080p);
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->frameLines() == Mode1024p.frameLines());

    seedPassThroughSource();
    Wire.trace.clear();
    bool solved = false;
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples && !solved; ++i)
        solved = unit.poll();
    REQUIRE(solved);

    CHECK(unit.path.outputMode()->frameLines() == Mode1080p.frameLines());
    CHECK(Adc::PLLAD_MD::read() == RasterDivider);
    CHECK(dividerWrites(RasterDivider) == 1);
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
    resolveUntilSolved(unit.acquisition, unit.nowMs);

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
    resolveUntilSolved(unit.acquisition, unit.nowMs);

    CHECK(unit.path.outputMode()->isBypass());
    CHECK(g_passThroughSwitches == 1);
}

// A COLD ENGINE HAS TO ARM ITS OWN FIRST SOLVE. sourceMoved() is the only
// armer, and it compared the count against solvedLines_ -- which only a solve
// writes, and only an arm opens a solve. The one other armer is a preset load,
// so a boot whose detection pass is refused leaves the engine with no route in
// at all: measured on the bench, 120 s of a plausible 627-line source with the
// recovery ladder cycling, the sync processor counting it perfectly, and no arm
// ever raised.
// docs/investigations/sp-sog-mode-had-two-owners.md
TEST_CASE("an engine that has never solved arms the first solve from the count")
{
    seedBenchSource();
    Acquiring unit;

    // The output resolution alone, WITHOUT the mode change a preset load
    // arms -- which is the state a refused detection leaves behind.
    unit.acquisition.setOutputResolution(&Mode1080p);

    CHECK(unit.pollUntilSolved());
}

// NOTHING OUTSIDE THE ENGINE CAN RE-SIZE A CHANNEL ALREADY IN PASS-THROUGH.
// The switch is what sizes it, and the switch runs on ENTRY only -- the route
// has not moved, so there is nothing for it to do. VideoPath::installSampling()
// and prepareToMeasure() both return early while passed through, deliberately,
// because the bypass divider is not theirs. So a source that changes mode
// inside pass-through re-measures correctly and keeps the previous mode's
// divider: measured on the bench, 800x600 to 640x480 left PLLAD_MD at 2039
// against a 524-line source with STATUS_MISC_PLLAD_LOCK 0 and the picture
// stretched.
TEST_CASE("a source that changes rate while passed through is re-sized")
{
    seedBenchSource();
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.start();

    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());
    const uint32_t sizedFor524 = HdBypass::HD_HSYNC_RST::read();

    // Another raster the sink can still take, so the route does not move and
    // the switch is not called again.
    seedSourceLines(627);
    g_fieldRate = 60.0f;
    unit.pollFor(10);

    CHECK(g_passThroughSwitches == 1);
    CHECK(HdBypass::HD_HSYNC_RST::read() != sizedFor524);
}

TEST_CASE("the boot's own input selection is not a source change")
{
    // The acquisition layer is constructed before setup() applies the saved
    // input, so the selection it latched at construction is not the one the
    // boot goes on to make. runPass() reaches sourceMoved() only once a mode
    // change has finished, so nothing refreshes the latch while the boot's own
    // acquisition is in flight -- and the edge is then read as a source change
    // that re-acquires everything the first solve has just established.
    //
    // Measured on the bench, six boots of six:
    //
    //     source key: 311@50.45/732++, framing recalled, shape 0
    //     rate match: source 50451 mHz, output 50475 mHz
    //     source moved: input (311 lines, solved 311)
    //
    // The count is the same on both sides of it, so nothing but the selection
    // could have armed it.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    VideoSourceSelection::forgetSelection();
    Acquiring unit;

    // setup() applying the saved input, after the layer was constructed.
    VideoSourceSelection::select(VideoSourceSelection::Vga);
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    unit.pollFor(4);

    CHECK_FALSE(loggedContaining("source moved: input"));
    VideoSourceSelection::forgetSelection();
}

TEST_CASE("selecting another input re-establishes the sync arrangement")
{
    // THE ARRANGEMENT IS HELD AGAINST THE SELECTION IT WAS CHOSEN FOR. Measured
    // on the bench: with the RISC PC on `vga` at 800x600@60 and the Wii on
    // `ypbpr`, `ADC_INPUT_SEL` followed the selection and `SP_EXT_SYNC_SEL`
    // stayed 0, so the sync processor went on watching the external H/V pins --
    // which still carry the VGA connector's hsync. Moving the RISC PC to
    // 320x256@50 moved the count reported on `ypbpr` from 627 to 311, so it was
    // measuring the other source live.
    //
    // Nothing arms a re-probe there and the reason is circular: the probe runs
    // per source MODE change, and the source never appears to change mode
    // because it is the same physical signal either side of the switch.
    // docs/known-issues.md, "The sync arrangement outlives an input change"
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    VideoSourceSelection::select(VideoSourceSelection::Vga);
    g_hasOwnVsync = true;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(SyncProcessor::SP_SOG_MODE::read() == 0);

    VideoSourceSelection::select(VideoSourceSelection::Ypbpr);
    g_hasOwnVsync = false;
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        unit.poll();

    CHECK(SyncProcessor::SP_SOG_MODE::read() == 1);
    VideoSourceSelection::forgetSelection();
}

TEST_CASE("nothing parks the sync separator fully open")
{
    // THE LAST RUNG BRICKED THE SEPARATOR. ReopenSogSeparator called choose(0),
    // the one value no ratchet can climb back out of, and every leg that failed
    // for 25 s reached it -- after which every later leg failed too, with
    // ADC_SOGCTRL 0, STATUS_SYNC_PROC_VTOTAL 97 and a correct VPERIOD_IF beside
    // it. `/sc?~` does not recover it, because the held level is what apply()
    // writes back.
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();
    REQUIRE(unit.pollUntilSolved());

    seedSourceLines(97);
    bool parked = false;
    for (uint32_t ms = 0; ms < AbsentForMs
                                   + 6 * AbsentForMs;
         ms += VideoSourceAcquisition::DetectionIntervalMs) {
        unit.poll();
        if (SyncOnGreen::level() == 0)
            parked = true;
    }

    CHECK_FALSE(parked);
}

TEST_CASE("a resolution the encoder cannot transmit gives way to one it can")
{
    // The bench point. 630 lines at 84.68 Hz asks 1080p for 2200 x 1125 x 84.68
    // = 209.6 MHz of TMDS clock against the MS9288A's 165, and it used to be
    // written out anyway: a 1134 px raster at 95.3 kHz, the STV9426 overlay
    // smeared into blue bands across the top of the frame, and every register
    // reading self-consistent beside it.
    //
    // 1024p asks 152.4 MHz and is the tallest that fits, so that is what the
    // source gets -- and the CHOICE is not written over, so 1080p comes back
    // with a source it can be met on.
    // docs/investigations/the-encoder-ceiling-is-the-raster-floor.md
    seedBenchSource();
    seedSourceLines(630);
    seedField(0, 0x19, 0, 12, 129);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x16, 0, 1, 0);       // STATUS_SYNC_PROC_HSPOL, negative-going
    g_fieldRate = 84.68f;

    Acquiring unit;
    unit.start(&Mode1080p);
    REQUIRE(unit.pollUntilSolved(8));

    REQUIRE(unit.path.outputMode());
    CHECK(unit.path.outputMode()->frameLines() == Mode1024p.frameLines());

    SUBCASE("and the raster it lands on is one the encoder can carry") {
        CHECK(unit.path.outputMode()->encoderCanTransmit(84.68f));
        CHECK(VideoProcessor::VDS_VSYNC_RST::read() + 1 == Mode1024p.frameLines());
    }

    SUBCASE("and the choice returns when the source does") {
        seedBenchSource();
        seedLineSamples(BenchDivider);
        unit.pollFor(8);

        CHECK(unit.path.outputMode()->frameLines() == Mode1080p.frameLines());
    }
}

// --- the grace defers the teardown, not the configuration --------------------

// THE WHOLE COST OF A SYNC-ON-GREEN ACQUISITION WAS A CONFIGURATION NO
// SELECTION MADE. Measured on the bench, Wii on ypbpr in 480i: the ADC PLL
// locks at the reference divider inside two seconds and STATUS_SYNC_PROC_VTOTAL
// then sits at 97 -- the value it holds when it is not following the source at
// all -- for 13.65 s of total console silence, until a recovery wrote the
// search configuration and the source acquired 1.6 s later.
//
// The search configuration is the SELECTION'S, so the source has it from the
// outset and the first count takes it back. Nothing waits on a recovery for it.
TEST_CASE("a selection asks for the search configuration, not a recovery")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.start();

    // No count from the outset, so nothing the engine does can be mistaken for
    // the search configuration arriving with a measurement.
    seedSourceLines(0);
    unit.poll();
    REQUIRE(unit.acquisition.sourceIsSearching());

    unit.acquisition.applySyncProcessorDynamic();

    CHECK(SyncProcessor::SP_H_CST_ST::read() == 0x10);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 0x100);
}

// **THE TAKE-BACK WAS ONE-WAY AND NOTHING PUT IT BACK.** The search
// configuration was applied once and withdrawn on the first count, with a latch
// so the withdrawal happened once -- so a source that fell back into searching
// kept the SETTLED configuration, and the coast inversion with it.
//
// Measured on the bench, Wii in 480i on ypbpr: SP_COAST_INV_REG 0 with
// STATUS_SYNC_PROC_VTOTAL at 97 and VPERIOD_IF a correct 524 beside it, which
// is the exact state the inversion exists to avoid -- uninverted from the
// start the block counts nothing at all.
TEST_CASE("a source that falls back into searching gets the search configuration again")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.path.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(SyncProcessor::SP_SOG_MODE::read() == 1);
    unit.pollFor(2);
    REQUIRE(SyncProcessor::SP_COAST_INV_REG::read() == 0);

    // A count no source runs, held long enough for the run to give up on it.
    seedSourceLines(97);
    unit.pollFor(4);
    REQUIRE(unit.acquisition.sourceIsSearching());

    CHECK(SyncProcessor::SP_COAST_INV_REG::read() == 1);
    CHECK(SyncProcessor::SP_H_CST_ST::read() == 0x10);
    CHECK(SyncProcessor::SP_H_CST_SP::read() == 0x100);
}

TEST_CASE("the dynamic configuration follows the sync type")
{
    // THE LATCH HOLDS EVERY FACT THE CONFIGURATION IS KEYED ON. Half of
    // SyncProcessor::applyDynamic() is decided by csync -- the coast inversion,
    // the separation thresholds and the pulse ignore -- so a latch that
    // remembers only the searching answer reports the whole configuration
    // applied when the csync half was skipped, and nothing puts it back.
    //
    // The pass that falls back into searching is also the pass that forgets the
    // sync type, the unusable-count arm doing both, which is how the search
    // coast inversion came to be written on none of them.
    seedPassThroughSource();
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.path.useSyncTypeProbe(probeOwnVsync);

    VideoSourceSelection::select(VideoSourceSelection::Vga);
    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    unit.pollFor(2);
    REQUIRE(SyncProcessor::SP_SOG_MODE::read() == 1);

    // THE PULSE WIDTH DIFFERENCE IS THE WITNESS, NOT THE COAST. The coast is
    // 0/0 on an unserrated csync source as well as on separate sync, so it
    // cannot tell the two branches apart; SP_DLT_REG is written by both and
    // differs on each.
    const uint32_t onCsync = SyncProcessor::SP_DLT_REG::read();
    REQUIRE(onCsync >= 0x70);

    unit.path.forgetSyncType();
    REQUIRE_FALSE(SyncMeasurement::isCsync());
    unit.poll();

    CHECK(SyncProcessor::SP_DLT_REG::read() != onCsync);
}

TEST_CASE("a selection carries the threshold of the line, not of the source before")
{
    // THE SEPARATE-SYNC VALUE ADMITTED NO PULSE AT ALL, and the csync branch
    // wrote every field of the arrangement but this one -- so a source selected
    // after a separate-sync one ran on 0xff. Measured on the bench, RISC PC on
    // vga then the Wii selected on ypbpr: the coast went to 7/6 and SP_DLT_REG
    // to 192 while SP_H_PULSE_IGNOR stayed 0xff, and the sync processor counted
    // 270/271 lines with the ADC PLL free-running until the recovery ladder
    // repaired it about fifteen seconds later. The same value reached a
    // sync-on-green source through a sync type that had been forgotten, where
    // nothing repaired it at all. ../docs/known-issues.md
    //
    // Nothing can be inherited now: the threshold is a fraction of the line in
    // force on every arrangement, so the predecessor cannot leave a value
    // behind for the next source to run on.
    //
    // The predecessor is seedPassThroughSource(), 524 lines at 60 Hz, selected
    // as separate sync.
    seedPassThroughSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    VideoSourceSelection::select(VideoSourceSelection::Vga);
    g_hasOwnVsync = true;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE_FALSE(unit.acquisition.sourceLowLineRate());
    const uint32_t onSeparate = SyncProcessor::SP_H_PULSE_IGNOR::read();
    REQUIRE(onSeparate != 0xff);

    VideoSourceSelection::select(VideoSourceSelection::Ypbpr);
    g_hasOwnVsync = false;
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        unit.poll();

    REQUIRE(SyncProcessor::SP_SOG_MODE::read() == 1);
    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() != 0xff);

    VideoSourceSelection::forgetSelection();
}

// THE SCAN IS WHAT DECIDES THE SEPARATION THRESHOLD, AND IT ARRIVES AFTER THE
// SOURCE DOES NOT. The scan used to choose between two values, and the one it
// chose for a progressive source sat under the floor an unserrated composite
// source imposes -- so the pass that settled the scan was the pass that made
// the source unreadable. Measured on `vga`, the RISC PC at 800x600@60 with
// `SYNC 1` through its 2038-sample line: that value counts 213 lines at
// 36.99 Hz where the fraction counts 622 at 60.32.
// docs/investigations/the-engine-makes-the-composite-count-unmeasurable.md
TEST_CASE("a composite source's threshold does not move when the scan settles")
{
    seedBenchSource();
    seedLineSamples(BenchDivider);
    Acquiring unit;
    unit.acquisition.allowMaintenance(true);
    unit.path.useSyncTypeProbe(probeOwnVsync);

    VideoSourceSelection::select(VideoSourceSelection::Ypbpr);
    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(SyncProcessor::SP_SOG_MODE::read() == 1);

    const uint32_t beforeTheScan = SyncProcessor::SP_H_PULSE_IGNOR::read();

    SUBCASE("a steady count settles the scan and keeps what acquired it") {
        for (uint8_t pass = 0; pass < 4 * SourceMeasurement::SteadySamples; ++pass)
            unit.poll();

        CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == beforeTheScan);
    }

    SUBCASE("a count alternating by one keeps what acquired it") {
        for (uint8_t pass = 0; pass < 4 * SteadyRun::CrossingsForInterlace; ++pass) {
            seedSourceLines(pass % 2 ? 312 : 311);
            unit.poll();
        }

        CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == beforeTheScan);
    }

    VideoSourceSelection::forgetSelection();
}

// AND ON THE PASSED-THROUGH ROUTE EITHER, which is the one with nothing else to
// re-place the threshold: the channel solves nothing, so what the arrangement
// wrote stands for the life of the route.
//
// Measured on the Wii at 480p on ypbpr through the channel's 2038-sample line,
// frozen with the divider latched: every threshold from 10 to 146 counts 524 at
// 59.94 Hz with the emitted frame at mean luma 194, the source's own sync pulse
// measuring 148 samples. The earlier reading that 99 emitted mean luma 0.00 was
// taken at a 1446-sample line, where 99 is 93% of a 106-sample pulse.
// docs/investigations/the-engine-makes-the-composite-count-unmeasurable.md
TEST_CASE("a composite source passed through keeps the threshold too")
{
    seedPassThroughSource();
    g_passThroughSwitches = 0;

    Acquiring unit;
    unit.acquisition.usePassThroughSwitch(enterPassThrough);
    unit.acquisition.allowPassThrough(true);
    unit.acquisition.allowMaintenance(true);
    unit.path.useSyncTypeProbe(probeOwnVsync);

    VideoSourceSelection::select(VideoSourceSelection::Ypbpr);
    g_hasOwnVsync = false;
    unit.start();
    REQUIRE(unit.pollUntilSolved(8));
    REQUIRE(unit.path.outputMode()->isBypass());
    REQUIRE(SyncProcessor::SP_SOG_MODE::read() == 1);

    const uint32_t entered = SyncProcessor::SP_H_PULSE_IGNOR::read();

    for (uint8_t pass = 0; pass < 4 * SourceMeasurement::SteadySamples; ++pass)
        unit.poll();

    CHECK(SyncProcessor::SP_H_PULSE_IGNOR::read() == entered);

    VideoSourceSelection::forgetSelection();
}

TEST_CASE("a boot rate the corroboration agrees with is not re-solved")
{
    // A re-solve blanks the output for the encoder relook, so one taken on a
    // rate that had not moved costs the user ~0.9 s of black ten seconds into
    // every boot and gives back an identical picture. Measured on the bench
    // once the boot's readings were spanned:
    //
    //     source moved: rate (311 lines, solved 311)
    //     sync pad: away
    //     source key: 311@50.48/732++, framing recalled, shape 13333
    //     sync pad: driven
    //
    // with the key either side identical.
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);   // HPERIOD_IF, steady, as the bench reads it
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // The source doing exactly what it was solved against.
    for (uint16_t pass = 0;
         pass < 2 * VideoSourceAcquisition::RateRecheckPasses; ++pass)
        CHECK_FALSE(unit.poll());
}

TEST_CASE("a corroboration disagreeing on one timing is asked again over a span")
{
    // One timing carries a whole source line of edge latency -- 3.2 per
    // thousand at 311 lines, against the 2 the corroboration compares at -- so
    // a disagreement from one is not a measurement. Measured on the bench, a
    // single timing read 50.73 Hz on a source running 50.47, and the arm it
    // bought blanked the output for ~0.9 s to give back an identical picture.
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);   // HPERIOD_IF, steady, as the bench reads it
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // The source unmoved, and one timing of it 5 per thousand high. Long
    // enough to reach the rechecks AFTER the boot's confirmation, which are the
    // ones that read a single timing.
    g_oneTimingRateHz = 50.73f;
    for (uint16_t pass = 0;
         pass < 4 * VideoSourceAcquisition::RateRecheckPasses; ++pass)
        CHECK_FALSE(unit.poll());

    g_oneTimingRateHz = 0.0f;
}

TEST_CASE("a boot confirmation disagreeing once is asked again before arming")
{
    // Spanning divides the edge latency rather than removing it, so the
    // confirmation's own reading is occasionally out too -- measured, one boot
    // of ten armed on one, at RateRecheckPasses, and gave back an identical
    // picture. Every disagreement is re-asked, whichever path found it.
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);   // HPERIOD_IF, steady, as the bench reads it
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());

    // One spanned reading 1.2 per thousand off, which is outside
    // BootRateConfirmPerThousand, on a source that has not moved.
    g_nextSpanRateHz = 50.14f;
    for (uint16_t pass = 0;
         pass < 2 * VideoSourceAcquisition::RateRecheckPasses; ++pass)
        CHECK_FALSE(unit.poll());

    g_nextSpanRateHz = 0.0f;
}

// --- a restoration that lands after the solve -------------------------------

TEST_CASE("a restoration landing after the solve re-solves rather than staying short")
{
    // THE HOLD NEEDS TWO AGREEING OFFERS AND THE SOLVE DOES NOT WAIT FOR THEM.
    // A formatter reading that completes a pass late leaves the capture sized
    // from the RAW count, and the count-move arm cannot recover it: a
    // restoration is at most VerticalSyncMaxLines and
    // SourceMeasurement::CountPairSpread admits 3, so every width up to 3
    // agrees with the count already solved and arms nothing.
    //
    // It is the width that decides, which is why the framing matrix splits on
    // it. 640x480@60 restores by 2 and 320x256@50 by 3, and both keep a short
    // capture -- 9|10 and 3|1 rows of black; 800x600@60 restores by 4, exceeds
    // the spread, re-solves and is flush at both ends.
    // ../docs/known-issues.md
    seedBenchSource();
    Acquiring unit;
    unit.path.useSyncTypeProbe(probeOwnVsync);

    // Composite, and the formatter has not completed a vertical measurement
    // yet: the count stands alone and nothing can restore it.
    g_hasOwnVsync = false;
    seedSourceLines(308);
    seedField(0, 0x00, 0, 1, 0);        // STATUS_IF_VT_OK
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    const uint16_t shortCapture = unit.path.lineUnitsOn(AxisVertical);
    REQUIRE(unit.sampling.sourceLines() == 309);

    // The frame arrives. 623 half-lines is 312 whole ones against a counted
    // 309, so the pulse the counter lost is 3 -- inside the spread.
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples; ++i) {
        seedSourceLines(308);
        seedFrame(623);
        unit.poll();
    }

    CHECK(unit.path.lineUnitsOn(AxisVertical) != shortCapture);
}
