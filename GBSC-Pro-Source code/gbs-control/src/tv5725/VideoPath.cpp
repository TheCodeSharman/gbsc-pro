#include "VideoPath.h"

#include <Arduino.h>

#include "Chip.h"

#include <math.h>
#include <stdio.h>

#include "Adc.h"
#include "CaptureWindow.h"
#include "Deinterlacer.h"
#include "FrameBuffer.h"
#include "InputFormatter.h"
#include "Memory.h"
#include "MemoryMap.h"
#include "OutputMode.h"
#include "ModeDetect.h"
#include "SyncProcessor.h"
#include "SyncMeasurement.h"
#include "VideoProcessor.h"

namespace Tv5725 {

// --- VideoPath ----------------------------------------------------------

VideoPath::VideoPath(DisplayClock &displayClock, SourceMeasurement &sampling,
                     FramingTable &framings)
    : displayClock_(displayClock),
      usableHorizontal_(0), usableVertical_(0),
      sampling_(sampling),
      framings_(framings),
      scanModeApplied_(false), syncTypeProbed_(false), syncProbe_(0),
      solvePending_(false), modePending_(false), modeOversample_(4),
      choice_(), rasterMode_(0),
      rasterLinePx_(0), rasterFrameLines_(0), activeStop_(0),
      activeLinesStop_(0) {}

const PanAndZoom &VideoPath::framing() const { return framing_; }

const SourceKey &VideoPath::framedKey() const { return framedKey_; }

bool VideoPath::changing() const { return modePending_ || solvePending_; }

bool VideoPath::changingMode() const { return modePending_; }

uint16_t VideoPath::capturableOn(const Axis &axis) const
{
    return axis.vertical() ? usableVertical_ : usableHorizontal_;
}

uint16_t VideoPath::originUnitsOn(const Axis &axis) const
{
    return (uint16_t)lrintf(framing_.originOn(axis) * (float)capturableOn(axis));
}

uint16_t VideoPath::extentUnitsOn(const Axis &axis) const
{
    return (uint16_t)lrintf(framing_.extentOn(axis) * (float)capturableOn(axis));
}

// Measure the source, then solve from it. For the two callers that need the
// source read again: the deferred retry, whose previous solve was refused
// against the measurement it already had, and the re-derive command, whose whole
// contract is the source as it reads now. A caller that has only moved the
// framing wants solveWindows(), which costs no vsync sample.
bool VideoPath::resolve()
{
    // The same reference the poll pass takes, and for the same reason: a window
    // solved for a taller mode strands the block the rate is timed off, and a
    // count taken through the previous mode's divider is not the source's.
    // Neither caller here reaches the one in poll().
    sampling_.applyReferenceSampling(modeOversample_);

    if (!sampling_.measureLineRate())
        return fail();

    // The reference is what the measurement was TAKEN through, never what the
    // source is left on: it is sized for the write limit alone, so leaving it
    // in place discards the bound the measurement was taken to compute.
    if (!solveSampling(modeOversample_))
        return fail();
    return solveWindows();
}

bool VideoPath::solveWindows()
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

const OutputMode *VideoPath::outputMode() const { return rasterMode_; }

bool VideoPath::solveRaster()
{
    // The choice is an input, not a read-back. Deriving the mode from
    // VDS_VSYNC_RST would leave the preset table -- the thing this replaces --
    // its only writer. docs/chip-initialisation.md.
    const OutputMode *mode = choice_.resolve();
    rasterMode_ = mode;
    if (mode == 0) {
        // Not a failure, and NOT a fall back to 1080p: the choice names no
        // resolution, and a raster nobody has swept keeps what it had.
        return false;
    }
    if (mode->isBypass()) {
        // Named rather than left to usable(): the output is the source's own
        // timing, so there is no raster to compute and solving one would write
        // zeros that every other register agrees with.
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

    // Solving a raster means the scaler is what the output carries, so the
    // routing bypass moved is claimed back here rather than by a whole-chip
    // bring-up running on the way past.
    Chip::routeToScaler();

    // The porch is not a register, so the next solve cannot read it back.
    activeStop_ = raster.activeStop;
    activeLinesStop_ = raster.activeLinesStop;

    return true;
}

void VideoPath::adoptRaster()
{
    rasterLinePx_ = GBS::VDS_HSYNC_RST::read() + 1;
    rasterFrameLines_ = GBS::VDS_VSYNC_RST::read() + 1;
    displayClock_.adopt();
}

void VideoPath::inputTimingsChanged()
{
    inputTimingsChanged(modeOversample_);
}

void VideoPath::inputTimingsChanged(uint8_t oversample)
{
    // The windows land seconds from now, once the source has settled into the
    // mode; until then the previous mode's geometry is what the new source
    // would be shown through.
    FrameBuffer::freezeCapture();

    modePending_ = true;
    modeOversample_ = oversample;
    scanModeApplied_ = false;
    syncTypeProbed_ = false;

    // Off the held choice, not off an argument. solveRaster() derives it again
    // when the solve runs; this keeps outputMode() answering consistently until
    // then.
    rasterMode_ = choice_.resolve();

    // The line count is about to move, so the steadiness run so far means
    // nothing.
    sampling_.resetSteadiness();

    // The sampling clock, BEFORE anything tries to measure. A load leaves the
    // ADC PLL on the bring-up's crossover row, and the sync processor counts in
    // ADC clocks -- so every measurement is garbage until this runs, the
    // steadiness gate never passes, and the pass that would have fixed the
    // clock never arrives.
    sampling_.applySampling(modeOversample_);
}

bool VideoPath::outputModeChanged(const OutputChoice &choice)
{
    choice_ = choice;
    if (modePending_)
        return false;

    // The frame height the scan mode is judged against, before it is judged.
    // **THE LINE DOUBLER IS A PROPERTY OF THE OUTPUT AS MUCH AS OF THE SOURCE**:
    // what decides it is whether the doubled frame fits the raster, so a shorter
    // raster strands a doubling that fitted the taller one.
    rasterMode_ = choice.resolve();

    const bool wasDoubled = sampling_.lineDoubled();
    solveScanMode();

    // Only where the doubling moved. The divider derives from it and from the
    // line rate already held -- so it is re-DERIVED, never re-measured -- and
    // writing it re-latches the ADC PLL, which is a relock nothing asked for.
    if (sampling_.lineDoubled() != wasDoubled
        && !solveSampling(modeOversample_))
        return false;

    if (!solveRaster())
        return false;

    // raster -> clock -> windows, the same order poll() runs and for the same
    // reason: the clock reads the seed the raster just chose.
    displayClock_.reset();
    return solveWindows();
}

VideoPath::PollOutcome VideoPath::pollDeferred()
{
    if (modePending_ || !solvePending_)
        return PollIdle;

    // A deferred retry, not a mode change: nothing re-reads the source count
    // here, so there is no run for the caller to seed.
    return resolve() ? PollResolved : PollIdle;
}

bool VideoPath::prepareToMeasure()
{
    if (!modePending_)
        return false;

    establishSyncType();
    solveScanMode();
    sampling_.applyReferenceSampling(modeOversample_);
    return true;
}

VideoPath::PollOutcome VideoPath::solveFromMeasurement()
{
    if (!modePending_)
        return PollIdle;

    if (!solveSampling(modeOversample_))
        return PollIdle;

    // Whatever raster is on the chip, taken before the solve that replaces it,
    // so a solve that still refuses leaves the windows sized for something.
    adoptRaster();
    if (!solveRaster()) {
        // solveRaster() never defers: both its refusals are final, so a retry
        // would pay for a field rate measurement to reach the same answer.
        modePending_ = false;
        FrameBuffer::releaseCapture();
        return PollIdle;
    }

    // raster -> clock -> windows. The clock reads the seed the raster just
    // chose, and every window is sized against the raster it lands on.
    displayClock_.reset();
    solveForSource();

    modePending_ = false;
    FrameBuffer::releaseCapture();
    return PollSolved;
}

bool VideoPath::reset()
{
    // The entry goes with the framing. "Back to default" has to mean the table
    // stops answering for this source, or the solve that follows restores
    // exactly what was just discarded and the control does nothing.
    framings_.forget(framedKey_);

    // A framing change like any other. The source has not moved and no load has
    // disturbed the ADC, so the divider, the raster and the clock all re-derive
    // to what they already hold -- and re-arming would freeze capture for
    // seconds to reach them.
    framing_.reset();
    return solveWindows();
}

void VideoPath::enterBypass()
{
    // The measurement is NOT discarded. Bypass does not measure, so what is
    // held is the rate from the mode that preceded it -- which is the fact a
    // caller asking whether the display can show this source wants, and the
    // only place it exists once the standard byte is gone.

    // No raster is solved here, so the register is the only source of the seed
    // the encoder is already running on.
    displayClock_.adopt();

    modePending_ = false;
    FrameBuffer::releaseCapture();

    choice_ = OutputChoice(OutputBypass);
    rasterMode_ = &ModeBypass;
    rasterLinePx_ = 0;
    rasterFrameLines_ = 0;

    // Bypass has no solved raster, so it has no porch either -- and a porch left
    // from the last scaled mode would size the next one's picture.
    activeStop_ = 0;
    activeLinesStop_ = 0;
}

bool VideoPath::solveForSource()
{
    const SourceKey arriving(sampling_.sourceLines(), sampling_.fieldRateHz());
    if (arriving == framedKey_)
        return solveWindows();

    // Leaving one source for another. Nothing is stored here: the table has
    // followed every press already, so what this source was tuned to is in it.
    if (!framings_.find(arriving, &framing_))
        framing_.reset();
    framedKey_ = arriving;
    return solveWindows();
}



void VideoPath::useSyncTypeProbe(bool (*hasOwnVsync)()) { syncProbe_ = hasOwnVsync; }

bool VideoPath::reacquireSyncType()
{
    syncTypeProbed_ = false;
    establishSyncType();
    return SyncMeasurement::isCsync();
}

void VideoPath::establishSyncType()
{
    if (syncTypeProbed_ || syncProbe_ == 0)
        return;
    syncTypeProbed_ = true;

    const bool csync = SyncMeasurement::probe(syncProbe_);
    SyncProcessor::applyForSyncType(csync);
    ModeDetect::applySyncType(csync ? ModeDetect::Csync : ModeDetect::SeparateSync);
    delay(SyncProcessor::PathSettleMs);
}

void VideoPath::solveScanMode()
{
    const uint16_t lines =
        SourceMeasurement::measureSourceLinesCorrected(sampling_.divider());
    if (!SourceMeasurement::countIsSource(lines))
        return;

    // Against the mode ASKED FOR rather than the raster last solved: this runs
    // before solveRaster(), so the held raster is the previous output's, and a
    // mode change would decide the scan mode from the resolution it is leaving.
    // The porch is not known this early either, so the bound is the raster's own
    // edge and a doubling that only just fits is caught by the capture clamp.
    const uint16_t showable =
        rasterMode_ && !rasterMode_->isBypass()
            ? AxisVertical.maximumCapture(rasterMode_->frameLines(), 0) : 0;
    const bool doubled = SourceMeasurement::lineDoublingFor(lines, showable);
    if (scanModeApplied_ && doubled == sampling_.lineDoubled())
        return;

    sampling_.holdLineDoubling(doubled);
    InputFormatter::applyScanMode(doubled ? InputFormatter::LineDoubled
                                          : InputFormatter::Progressive);
    VideoProcessor::applyScanMode(doubled);
    Deinterlacer::applyScanMode(doubled);
    scanModeApplied_ = true;
}

bool VideoPath::solveSampling(uint8_t oversample)
{
    // The duty is a ratio, so whichever divider is on the chip when the pulse
    // is measured gives the same answer as the one about to replace it.
    const uint16_t divider = sampling_.divider();
    const float duty = divider > 0
        ? (float)SourceMeasurement::measureHsyncLow() / (float)divider : 0.0f;
    const uint16_t framable = VideoSourceLine::framableIfLine(
        duty,
        sampling_.lineDoubled() ? 0 : VideoSourceLine::CaptureLagUnits,
        SourceMeasurement::measureHsyncPositive(), sampling_.lineDoubled());

    if (!sampling_.solve(sampling_.lineRateHz(), oversample, framable))
        return false;
    sampling_.applySampling(modeOversample_);
    return true;
}

int16_t VideoPath::unitsFor(int16_t pixels, const Scale &scale, const Axis &axis)
{
    return pixels == 0 ? 0 : axis.stepUnits(pixels, scale.magnification());
}

bool VideoPath::pan(int16_t dxPixels, int16_t dyPixels)
{
    PanAndZoom wanted = framing_;
    wanted.panBy(AxisHorizontal, unitsFor(dxPixels, horizontalScale_, AxisHorizontal),
                 usableHorizontal_);
    wanted.panBy(AxisVertical, unitsFor(dyPixels, verticalScale_, AxisVertical),
                 usableVertical_);
    return step(wanted);
}

bool VideoPath::zoom(int16_t dhPixels, int16_t dvPixels)
{
    PanAndZoom wanted = framing_;
    wanted.zoomBy(AxisHorizontal, unitsFor(dhPixels, horizontalScale_, AxisHorizontal),
                  usableHorizontal_);
    wanted.zoomBy(AxisVertical, unitsFor(dvPixels, verticalScale_, AxisVertical),
                  usableVertical_);
    return step(wanted);
}

bool VideoPath::applyFraming(const PanAndZoom &framing)
{
    return step(framing);
}

bool VideoPath::fail()
{
    solvePending_ = true;
    return false;
}

bool VideoPath::measureSourceTimings(CaptureWindow &capture)
{
    capture.setRasters(rasterLinePx_, rasterFrameLines_, activeStop_,
                       activeLinesStop_);
    if (!capture.readRasters(sampling_, SourceMeasurement::measureHsyncLow(),
                             SourceMeasurement::measureHsyncPositive())) {
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

bool VideoPath::calculateInputFormatterRegisters(CaptureWindow &capture)
{
    capture.setFraming(framing_);
    framing_ = capture.framing();
    usableHorizontal_ = capture.capturableOn(AxisHorizontal);
    usableVertical_ = capture.capturableOn(AxisVertical);
    return capture.usable() ? true : fail();
}

VideoProcessorTimings VideoPath::calculateOutputRaster(const CaptureWindow &capture) const
{
    return VideoProcessorTimings(capture.horizontal().width(), capture.vertical().width(),
                            capture.linePx(), capture.frameLines(),
                            activeStop_, activeLinesStop_);
}

void VideoPath::write(const VideoProcessorTimings &solved, const CaptureWindow &capture)
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

bool VideoPath::step(const PanAndZoom &wanted)
{
    PanAndZoom before = framing_;
    framing_ = wanted;

    uint16_t horizontalStop = GBS::IF_HB_SP2::read();
    uint16_t horizontalStart = GBS::IF_HB_ST2::read();
    uint16_t verticalStop = GBS::IF_VB_SP::read();
    uint16_t verticalStart = GBS::IF_VB_ST::read();

    if (!solveWindows()) {
        framing_ = before;
        return false;
    }
    if (GBS::IF_HB_SP2::read() == horizontalStop && GBS::IF_HB_ST2::read() == horizontalStart
        && GBS::IF_VB_SP::read() == verticalStop && GBS::IF_VB_ST::read() == verticalStart) {
        framing_ = before;
        return true;
    }
    // A press that moved a window is what makes this framing worth a place in
    // the table, and it goes in NOW rather than when the source is left: a unit
    // turned off where it is used would otherwise lose every tuning. Only the
    // flash write is debounced. One that moved nothing stores nothing, which is
    // also what keeps sixteen places from filling with computed defaults.
    framings_.remember(framedKey_, framing_);
    return true;
}

}  // namespace Tv5725
