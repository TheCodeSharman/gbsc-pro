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
#include "RegistersWritten.h"

#include "../GBSC-Pro-Source code/gbs-control/src/videosource/VideoSourceAcquisition.h"
#include "../GBSC-Pro-Source code/gbs-control/src/videosource/SyncRecovery.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/BringUp.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoRoute.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ColourSpace.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/HdBypass.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

using namespace Tv5725;

// The bench RiscPC as SolvedEngine seeds it, but solved through the layer
// rather than by calling the engine's own poll().
static void seedBenchSource()
{
    Wire.reset();
    poisonChip();
    g_fieldRate = 50.08f;
    seed(3, 0x01, 0, 12, 1915);
    seed(3, 0x02, 4, 11, 1124);
    seed(1, 0x0E, 0, 11, 1276);
    seed(0, 0x19, 0, 12, 181);
    seed(5, 0x12, 0, 12, 2553);
    seed(0, 0x1B, 0, 11, 311);
    seed(0, 0x16, 0, 1, 1);              // STATUS_SYNC_PROC_HSPOL, positive-going
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

// What the sync processor counts along the line, against the divider the solve
// chose. Equal means the ADC is locked to the line being counted.
static void seedLineSamples(uint16_t samples)
{
    seedField(0, 0x17, 0, 12, samples);   // STATUS_SYNC_PROC_HTOTAL
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
        for (uint16_t i = 0; i < runs * SourceMeasurement::SteadySamples; ++i)
            if (poll())
                return true;
        return false;
    }

    void pollFor(uint8_t runs)
    {
        for (uint16_t i = 0; i < runs * SourceMeasurement::SteadySamples; ++i)
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

// The divider the engine derives for the bench source. It is UNDER the capture
// write limit rather than at it: 2250 ADC samples is the limit, and a value
// pinned there is what this file expected while the divider was chosen by the
// cap instead of from the measured line. docs/capture-limits.md
static const uint16_t BenchDivider = 2230;

// And for the 524-line 60 Hz mode the transition cases move to, which is
// progressive and negative-going. The bench runs exactly this on the Wii at
// 480p -- 524 lines x 59.80 Hz, PLLAD_MD 1096.
static const uint16_t VesaDivider = 1096;

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

    CHECK(unit.acquisition.sourceLineRateHz() == unit.sampling.heldLineRateHz());
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
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i) {
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
        for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
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
    seed(5, 0x12, 0, 12, channelDivider);
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
    seedLineSamples(3250);                // what the bench measured, against the solved divider
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());

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
    seedLineSamples(3250);
    Acquiring unit;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
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
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
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

TEST_CASE("a field rate the line count cannot show re-solves the source")
{
    // 320x256 at 50, 55 and 60 all count 312 lines, so the count says the
    // source did not move and the raster stays sized for the rate before it.
    // HPERIOD_IF reads 431 / 392 / 359 across those three, correctly every
    // time, and it costs one register read -- so the rate is affordable on the
    // idle path that the field rate measurement never was.
    seedBenchSource();
    seedField(0, 0x06, 0, 9, 431);     // HPERIOD_IF, this source at 50 Hz
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(Adc::PLLAD_MD::read() == BenchDivider);

    // The same 311 lines at 60 Hz. What has to move is the HELD RATE: the
    // divider cannot witness this, because the framable line binds at both
    // rates -- Adc::oversampleFor() carries the crossover row's oversampling
    // rather than the one asked for, which puts the rate ceiling above
    // DividerMax either side of the change. docs/capture-limits.md
    seedField(0, 0x06, 0, 9, 359);
    g_fieldRate = 60.29f;
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples; ++i)
        unit.poll();

    // 27 MHz over four times the counter's reading, which is what HPERIOD_IF
    // 359 means: the rate the register reports, not the nominal one.
    CHECK(unit.acquisition.sourceLineRateHz() == 27000000u / (4 * (359 + 1)));
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
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i) {
        seedField(0, 0x06, 0, 9, i % 2 ? 255 : 431);
        unit.poll();
    }

    CHECK(g_probeCalls == 1);
}

TEST_CASE("a rate the field rate does not confirm leaves the source alone")
{
    // HPERIOD_IF rails to a value that is WRONG AND STABLE, which is the one
    // failure no run can reject: 511 on the bench source reads as 13183 Hz
    // against a real 15625, holds across every poll, and passes the bounds and
    // the agreement inside lineRateFromHPeriod(). The field rate is measured a
    // different way, so it does not rail with it -- and it is affordable here
    // because a corroborated disagreement is rare.
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

    seedLineSamples(3250);
    for (uint16_t i = 0; i < SyncRecovery::FirstEscalationPass; ++i)
        unit.poll();
    REQUIRE(unit.acquisition.recoveryDue() != SyncRecovery::None);

    seedLineSamples(BenchDivider);
    unit.poll();

    CHECK(unit.acquisition.recoveryDue() == SyncRecovery::None);
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

    seedLineSamples(3250);
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

    seedLineSamples(3250);
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
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
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
    g_probeCalls = 0;
    unit.start();
    REQUIRE(unit.pollUntilSolved());
    REQUIRE(g_probeCalls == 1);

    seedSourceLines(524);
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        unit.poll();

    CHECK(g_probeCalls == 2);
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
    seedLineSamples(3250);
    Acquiring unit;

    unit.start();
    REQUIRE(unit.pollUntilSolved());
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
