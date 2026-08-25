#include "Geometry.h"

#include <math.h>

#include "Adc.h"
#include "CaptureWindow.h"
#include "FrameBuffer.h"
#include "InputFormatter.h"
#include "Memory.h"
#include "MemoryMap.h"
#include "OutputMode.h"
#include "SyncProcessor.h"

namespace Tv5725 {

// --- Geometry ----------------------------------------------------------

Geometry::Geometry(DisplayClock &displayClock)
    : displayClock_(displayClock),
      usableHorizontal_(0), usableVertical_(0),
      samplingPending_(false), sourceInterrupted_(false), referenceRateHz_(0),
      scanModeApplied_(false), solvedLines_(0),
      idleLines_(0), idleRun_(0),
      solvePending_(false), modePending_(false), modeOversample_(4),
      rasterMode_(0),
      rasterLinePx_(0), rasterFrameLines_(0), activeStop_(0),
      activeLinesStop_(0) {}

const PanAndZoom &Geometry::framing() const { return framing_; }

bool Geometry::changing() const { return modePending_ || solvePending_; }

uint16_t Geometry::capturableOn(const Axis &axis) const
{
    return axis.vertical() ? usableVertical_ : usableHorizontal_;
}

uint16_t Geometry::originUnitsOn(const Axis &axis) const
{
    return (uint16_t)lrintf(framing_.originOn(axis) * (float)capturableOn(axis));
}

uint16_t Geometry::extentUnitsOn(const Axis &axis) const
{
    return (uint16_t)lrintf(framing_.extentOn(axis) * (float)capturableOn(axis));
}

float Geometry::sourceFieldRateHz() const { return sampling_.fieldRateHz(); }

bool Geometry::sourceLowLineRate() const { return sampling_.lowLineRate(); }

uint32_t Geometry::sourceLineRateHz() const { return sampling_.heldLineRateHz(); }

bool Geometry::resolve()
{
    // The entry points outside a poll pass -- a pad press, a retimed total --
    // have no measurement of their own, so this is where they take one. poll()
    // has already measured and calls solveWindows() directly.
    if (!sampling_.measureLineRate())
        return fail();
    return solveWindows();
}

bool Geometry::solveWindows()
{
    CaptureWindow capture;
    if (!measureSourceTimings(capture))
        return false;
    if (!calculateInputFormatterRegisters(capture))
        return false;

    VideoProcessorTimings solved = calculateOutputRaster(capture);
    if (!solved.usable())
        return fail();

    write(solved, capture);
    horizontalScale_ = solved.horizontalScale();
    verticalScale_ = solved.verticalScale();
    solvePending_ = false;
    return true;
}

bool Geometry::solveRaster(const OutputMode *mode)
{
    // Remembered so the deferred retry solves the mode the load asked for. It
    // cannot re-derive one: by then the caller's preference is out of reach and
    // the raster registers may hold a half-written attempt.
    rasterMode_ = mode;
    return solveRaster();
}

bool Geometry::solveRaster()
{
    // The mode is an input, not a read-back. Deriving it from VDS_VSYNC_RST
    // would leave the preset table -- the thing this replaces -- its only
    // writer. docs/chip-initialisation.md.
    const OutputMode *mode = rasterMode_;
    if (mode == 0) {
        // Not a failure, and NOT a fall back to 1080p: the caller could not name
        // a mode, and a raster nobody has swept keeps what it had.
        return false;
    }

    // The pass's own measurement. A raster solved at the wrong rate is out by
    // the ratio of the rates, and lineRateFrom() is what refuses one: it bounds
    // the line count, picks the nominal rate from it, and requires the measured
    // rate to agree within 2%. Reaching here means that passed, so re-reading
    // both to re-apply it would only add a second answer to disagree with.
    // docs/firmware-geometry-engine.md
    // EngineCeilingHz, not the higher WorkingCeilingHz the part is measured to
    // run at: a wider raster costs zoom travel. See the constant.
    OutputTimings raster = mode->solve(sampling_.fieldRateHz(),
                                        OutputMode::EngineCeilingHz);
    if (!raster.usable()) {
        // Refused, not deferred: the frame height and the rate are settled, so
        // waiting produces the same answer at the cost of a measurement a pass.
        return false;
    }

    // Totals before sync positions, per
    // docs/investigations/preset-abandonment-audit.md. Both hold total-1.
    GBS::VDS_HSYNC_RST::write(raster.horizontalTotal - 1);
    GBS::VDS_VSYNC_RST::write(raster.verticalTotal - 1);
    rasterLinePx_ = raster.horizontalTotal;
    rasterFrameLines_ = raster.verticalTotal;

    // One quantity in three registers, so all three are written here.
    // VDS_VSYN_SIZE1 and _2 are the vertical totals the frame-rate selector
    // picks between, and VDS_FR_SELECT never alternates, so both are the frame.
    // doPostPresetLoadSteps() wrote them once, in a function the deferred solve
    // never re-enters, leaving them sized for the total the preset loaded with.
    GBS::VDS_VSYN_SIZE1::write(raster.verticalTotal + 1);
    GBS::VDS_VSYN_SIZE2::write(raster.verticalTotal + 1);

    GBS::VDS_HS_ST::write(raster.hsyncStart);
    GBS::VDS_HS_SP::write(raster.hsyncStop);
    GBS::VDS_VS_ST::write(raster.vsyncStart);
    GBS::VDS_VS_SP::write(raster.vsyncStop);

    // The seed, last of the raster group and first of the clock's. It names the
    // frequency to run at, not the byte to write: select() puts the part on
    // PCLKIN when a generator can serve that frequency, and on the seed's own
    // internal divider when none can.
    displayClock_.hold(raster.divider);
    displayClock_.select();

    // The porch is not a register, so the next solve cannot read it back.
    activeStop_ = raster.activeStop;
    activeLinesStop_ = raster.activeLinesStop;

    return true;
}

void Geometry::adoptRaster()
{
    rasterLinePx_ = GBS::VDS_HSYNC_RST::read() + 1;
    rasterFrameLines_ = GBS::VDS_VSYNC_RST::read() + 1;
    displayClock_.adopt();
}

void Geometry::modeChanged(const OutputMode *mode,
                           uint8_t oversample)
{
    // The windows land seconds from now, once the source has settled into the
    // mode; until then the previous mode's geometry is what the new source
    // would be shown through.
    FrameBuffer::freezeCapture();

    modePending_ = true;
    modeOversample_ = oversample;
    scanModeApplied_ = false;
    rasterMode_ = mode;

    // The line count is about to move, so the steadiness run so far means
    // nothing.
    sampling_.resetSteadiness();

    // The sampling clock, BEFORE anything tries to measure. A load leaves the
    // ADC PLL on the bring-up's crossover row, and the sync processor counts in
    // ADC clocks -- so every measurement is garbage until this runs, the
    // steadiness gate never passes, and the pass that would have fixed the
    // clock never arrives.
    writeSampling();
}

bool Geometry::poll()
{
    if (!modePending_) {
        if (sourceMoved())
            modeChanged(rasterMode_, modeOversample_);
        return modePending_ ? false : (solvePending_ ? resolve() : false);
    }

    // **BEFORE THE GATES BELOW, AND THIS IS THE POINT OF IT.** The input
    // formatter's own measurements are only meaningful once its scan mode
    // matches the source, so a scan mode left wrong makes the gates fail and a
    // scan mode derived after them is never reached. The sync processor counts
    // the source directly and is indifferent to the scan mode, which is what
    // makes the line count usable here and nothing else.
    solveScanMode();

    // **BEFORE THE LINE COUNT, because the line count is a measurement too.**
    // The sync processor counts in ADC clocks, so on the previous mode's
    // divider the PLL sits outside its lock range and the count that comes back
    // is not the source's -- and that count is what the gate below is reading.
    // Applying the reference afterwards puts the fix on the far side of the
    // gate its absence holds shut.
    // docs/investigations/field-rate-measured-downstream.md
    holdReferenceSampling();

    // The cheap gate. Everything below this line measures, and the field rate
    // costs up to 250 ms a vsync pulse. The reference above is what opens it:
    // a count taken through the previous mode's divider is not the source's.
    if (!sampling_.sampleSteady())
        return false;

    // THE measurement of the source for this pass. Everything below derives
    // from it -- the divider, the raster, both windows -- so nothing can end up
    // solved against a rate something else was not.
    if (!sampling_.measureLineRate()) {
        // Deferred, not settled for. The reference above is already a divider
        // the capture window can be measured in, so there is nothing to inherit
        // and the flag is only a note to re-solve.
        samplingPending_ = true;
        return false;
    }

    // A rate is worth sizing a raster from once it has REPEATED. The cross-check
    // inside measureLineRate() bounds the rate against the line count, which
    // catches a settling source off by tens of percent and passes one off by
    // tenths -- and the raster is out by whatever fraction the rate is, for
    // good, because nothing re-solves it.
    if (!sampling_.rateSettled())
        return false;

    if (!solveSampling(modeOversample_))
        return false;

    // Whatever raster is on the chip, taken before the solve that replaces it,
    // so a solve that still refuses leaves the windows sized for something.
    adoptRaster();
    if (!solveRaster(rasterMode_)) {
        // solveRaster() never defers: both its refusals are final, so a retry
        // would pay for a field rate measurement to reach the same answer.
        modePending_ = false;
        FrameBuffer::releaseCapture();
        return false;
    }

    // raster -> clock -> windows. The clock reads the seed the raster just
    // chose, and every window is sized against the raster it lands on.
    displayClock_.reset();
    solveForSource();

    // What this solve ran against, so a source that later differs from it arms
    // the engine without anyone having to say so.
    solvedLines_ = sampling_.sourceLines();
    modePending_ = false;
    FrameBuffer::releaseCapture();
    return true;
}


void Geometry::reset()
{
    framedKey_ = SourceKey();
    // Re-arming the mode change already in force, rather than a second path
    // that would have to keep step with it: the whole sequence -- freeze, wait
    // for the source, measure, sampling, raster, clock, windows -- is what a
    // reset wants. The framing goes with it, which is what makes this a reset
    // rather than a re-solve: solveForSource() would keep it, the source not
    // having moved.
    modeChanged(rasterMode_, modeOversample_);
}

void Geometry::sourceInterrupted()
{
    sourceInterrupted_ = true;
}

void Geometry::enterBypass()
{
    sampling_.forgetSource();

    // No raster is solved here, so the register is the only source of the seed
    // the encoder is already running on.
    displayClock_.adopt();

    // The mode change goes with them: neither bypass switch reaches
    // doPostPresetLoadSteps(), so nothing else would clear it, and a poll
    // landing afterwards writes a scaled raster and a recomputed divider over
    // the setup bypass just chose.
    modePending_ = false;
    FrameBuffer::releaseCapture();

    rasterMode_ = 0;
    rasterLinePx_ = 0;
    rasterFrameLines_ = 0;
    samplingPending_ = false;
    solvedLines_ = 0;

    // Bypass has no solved raster, so it has no porch either -- and a porch left
    // from the last scaled mode would size the next one's picture.
    activeStop_ = 0;
    activeLinesStop_ = 0;
}

bool Geometry::solveForSource()
{
    const SourceKey arriving(sampling_.sourceLines(), sampling_.fieldRateHz());
    if (arriving != framedKey_)
        framing_.reset();
    framedKey_ = arriving;
    return solveWindows();
}



void Geometry::holdReferenceSampling()
{
    const uint16_t reference = SourceMeasurement::referenceDivider(sampling_.lineDoubled());
    const uint32_t estimate = sampling_.estimatedLineRateHz();

    // Unconditional, ahead of the return below. The reference divider is a
    // function of the scan mode alone, so a source that did not move asks for
    // the one already in force -- and a window is not only stranded by a mode
    // change. Nothing else writes these two until a solve succeeds, which is
    // the thing they are stopping.
    InputFormatter::writeReferenceVerticalBlank();

    // The estimate is half of it, not a detail: PLLAD_KS is an octave of CKO,
    // which is the divider TIMES the rate. A count caught mid-transition picks
    // the wrong octave, and the reference divider for a scan mode does not
    // change when the count settles -- so a return keyed on the divider alone
    // leaves KS wrong with PLLAD_MD right, which is a state nothing can measure
    // its way out of.
    if (sampling_.divider() == reference && estimate == referenceRateHz_)
        return;

    referenceRateHz_ = estimate;
    sampling_.holdDivider(reference);
    // The oversampling stays as the mode asks for it: PLLAD_CKOS and the
    // decimators describe one ratio between them, and the IF's units come off
    // the decimated clock. Only the divider is being moved to a known value.
    Adc::applySampleRate(reference, estimate, modeOversample_);
    InputFormatter::writeLineCounter(sampling_.ifLine());
    SyncProcessor::writeRetimeStop(sampling_.retimeStop());
}

// One quantity in three registers, each written by the block that declares it.
// The divider goes first because Adc latches it, and the latch loads KS, CKOS
// and ICP with it -- so anything setting those must already have run.
void Geometry::writeSampling()
{
    if (!sampling_.usable())
        return;

    Adc::applySampleRate(sampling_.divider(), sampling_.lineRateHz(),
                         modeOversample_);
    InputFormatter::writeLineCounter(sampling_.ifLine());
    SyncProcessor::writeRetimeStop(sampling_.retimeStop());
}

// **THIS MUST NOT USE sampling_.sampleSteady().** That call is the solve's own
// steadiness run, and filling it while the engine is idle leaves the next mode
// change's first poll believing a count from the mode before it.
bool Geometry::sourceMoved()
{
    // Bypass has no scaled raster to re-solve, and enterBypass() drops the mode
    // change so a later poll cannot write one over the setup it just chose.
    if (rasterMode_ == 0 || solvedLines_ == 0) {
        sourceInterrupted_ = false;
        return false;
    }

    const uint16_t lines = SourceMeasurement::measureSourceLines();
    if (!SourceMeasurement::countIsSource(lines) || lines != idleLines_) {
        idleLines_ = lines;
        idleRun_ = 0;
        return false;
    }
    if (idleRun_ < SourceMeasurement::SteadySamples) {
        ++idleRun_;
        return false;
    }

    // The interrupt says the source moved where the count cannot: the same
    // number of lines at a different field rate. It waits behind the SAME
    // steadiness run rather than firing on arrival, because a source measured
    // mid-transition yields a rate that passes every check and is tens of
    // percent out -- measured at 18806 Hz against a real 31440, held, with
    // every register self-consistent.
    const bool interrupted = sourceInterrupted_;
    sourceInterrupted_ = false;
    if (!interrupted && lines == solvedLines_)
        return false;

    idleRun_ = 0;
    return true;
}

void Geometry::solveScanMode()
{
    const uint16_t lines =
        SourceMeasurement::measureSourceLinesCorrected(sampling_.divider());
    if (!SourceMeasurement::countIsSource(lines))
        return;

    const bool doubled = SourceMeasurement::lineDoublingFor(lines);
    if (scanModeApplied_ && doubled == sampling_.lineDoubled())
        return;

    sampling_.holdLineDoubling(doubled);
    InputFormatter::applyScanMode(doubled ? InputFormatter::LineDoubled
                                          : InputFormatter::Progressive);
    scanModeApplied_ = true;
}

bool Geometry::solveSampling(uint8_t oversample)
{
    if (!sampling_.solve(sampling_.lineRateHz(), oversample)) {
        samplingPending_ = true;
        return false;
    }
    writeSampling();
    samplingPending_ = false;
    return true;
}

int16_t Geometry::unitsFor(int16_t pixels, const Scale &scale, const Axis &axis)
{
    return pixels == 0 ? 0 : axis.stepUnits(pixels, scale.magnification());
}

bool Geometry::pan(int16_t dxPixels, int16_t dyPixels)
{
    PanAndZoom wanted = framing_;
    wanted.panBy(AxisHorizontal, unitsFor(dxPixels, horizontalScale_, AxisHorizontal),
                 usableHorizontal_);
    wanted.panBy(AxisVertical, unitsFor(dyPixels, verticalScale_, AxisVertical),
                 usableVertical_);
    return step(wanted);
}

bool Geometry::zoom(int16_t dhPixels, int16_t dvPixels)
{
    PanAndZoom wanted = framing_;
    wanted.zoomBy(AxisHorizontal, unitsFor(dhPixels, horizontalScale_, AxisHorizontal),
                  usableHorizontal_);
    wanted.zoomBy(AxisVertical, unitsFor(dvPixels, verticalScale_, AxisVertical),
                  usableVertical_);
    return step(wanted);
}

float Geometry::sourceFieldRateOr50Hz() const
{
    float rate = sampling_.fieldRateHz();
    return (rate > 40.0f && rate < 100.0f) ? rate : 50.0f;
}

bool Geometry::fail()
{
    solvePending_ = true;
    return false;
}

bool Geometry::measureSourceTimings(CaptureWindow &capture)
{
    capture.setRasters(rasterLinePx_, rasterFrameLines_);
    if (!capture.readRasters(sampling_, SourceMeasurement::measureHsyncLow())) {
        // Bypass is not a failure to retry: there is nothing to solve.
        if (!capture.scaling()) {
            solvePending_ = false;
            return false;
        }
        return fail();
    }
    if (!capture.scaling()) {
        solvePending_ = false;
        return false;
    }
    return true;
}

bool Geometry::calculateInputFormatterRegisters(CaptureWindow &capture)
{
    capture.setFraming(framing_, sourceFieldRateOr50Hz());
    framing_ = capture.framing();
    usableHorizontal_ = capture.capturableOn(AxisHorizontal);
    usableVertical_ = capture.capturableOn(AxisVertical);
    return capture.usable() ? true : fail();
}

VideoProcessorTimings Geometry::calculateOutputRaster(const CaptureWindow &capture) const
{
    return VideoProcessorTimings(capture.horizontal().width(), capture.vertical().width(),
                            capture.linePx(), capture.frameLines(),
                            activeStop_, activeLinesStop_);
}

void Geometry::write(const VideoProcessorTimings &solved, const CaptureWindow &capture)
{
    // 1. Far edges OUTWARD only, which can only add headroom. The memory window
    // hugs the picture, so it moves in as well as out; narrowing it here would
    // leave the old, wider display window showing unwritten memory at the far
    // edge for the length of a write. Inward moves wait for step 5b.
    if (solved.memory().horizontal().start() > GBS::VDS_HB_ST::read())
        GBS::VDS_HB_ST::write(solved.memory().horizontal().start());
    if (solved.memory().vertical().start() > GBS::VDS_VB_ST::read())
        GBS::VDS_VB_ST::write(solved.memory().vertical().start());

    // 2. Near edges down, if down is where they are going.
    if (solved.memory().horizontal().stop() < GBS::VDS_HB_SP::read())
        GBS::VDS_HB_SP::write(solved.memory().horizontal().stop());
    if (solved.memory().vertical().stop() < GBS::VDS_VB_SP::read())
        GBS::VDS_VB_SP::write(solved.memory().vertical().stop());

    // 3. The picture. BYPS cleared because an explicit scale was computed.
    //
    // The line double's progressive window spans one whole line, so it is
    // recomputed on every solve. Its start is written rather than read, or a
    // clobbered preset byte would propagate into the stop.
    GBS::IF_LINE_ST::write(CaptureWindow::ProgressiveStart);
    GBS::IF_LINE_SP::write(capture.horizontalLine().progressiveStop(CaptureWindow::ProgressiveStart));
    GBS::IF_HB_SP2::write(capture.horizontal().stop());
    GBS::IF_HB_ST2::write(capture.horizontal().start());
    GBS::IF_VB_SP::write(capture.vertical().stop());
    GBS::IF_VB_ST::write(capture.vertical().start());
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HSCALE::write(solved.horizontalScale().reg());
    GBS::VDS_VSCALE::write(solved.verticalScale().reg());

    // 4. Near edges up, now that the picture they bound is the new one.
    GBS::VDS_HB_SP::write(solved.memory().horizontal().stop());
    GBS::VDS_VB_SP::write(solved.memory().vertical().stop());

    // 5. The aperture, which must hug the picture.
    GBS::VDS_DIS_HB_SP::write(solved.display().horizontal().stop());
    GBS::VDS_DIS_HB_ST::write(solved.display().horizontal().start());
    GBS::VDS_DIS_VB_SP::write(solved.display().vertical().stop());
    GBS::VDS_DIS_VB_ST::write(solved.display().vertical().start());

    // 5b. Far edges INWARD, now that the aperture they bound has closed. The
    // mirror of step 2: a window edge may only cross the display window's in
    // the direction that keeps the picture covered.
    if (solved.memory().horizontal().start() < GBS::VDS_HB_ST::read())
        GBS::VDS_HB_ST::write(solved.memory().horizontal().start());
    if (solved.memory().vertical().start() < GBS::VDS_VB_ST::read())
        GBS::VDS_VB_ST::write(solved.memory().vertical().start());

    // 6. The playback burst, only if it is not already right. Rewriting
    // PB_FETCH_NUM reprograms the playback FIFO while the picture is being read
    // out of it, which flickers even when the value written is identical.
    // docs/investigations/hscale-tearing-characterisation.md
    uint16_t fetch = Memory::fetchFor(capture.horizontal().width());
    uint16_t offset = Memory::offsetFor(capture.horizontalLine().units());
    if (GBS::PB_FETCH_NUM::read() != fetch)
        GBS::PB_FETCH_NUM::write(fetch);
    if (GBS::PB_CAP_OFFSET::read() != offset)
        GBS::PB_CAP_OFFSET::write(offset);
}

bool Geometry::step(const PanAndZoom &wanted)
{
    PanAndZoom before = framing_;
    framing_ = wanted;

    uint16_t horizontalStop = GBS::IF_HB_SP2::read();
    uint16_t horizontalStart = GBS::IF_HB_ST2::read();
    uint16_t verticalStop = GBS::IF_VB_SP::read();
    uint16_t verticalStart = GBS::IF_VB_ST::read();

    if (!resolve()) {
        framing_ = before;
        return false;
    }
    if (GBS::IF_HB_SP2::read() == horizontalStop && GBS::IF_HB_ST2::read() == horizontalStart
        && GBS::IF_VB_SP::read() == verticalStop && GBS::IF_VB_ST::read() == verticalStart)
        framing_ = before;
    return true;
}

}  // namespace Tv5725
