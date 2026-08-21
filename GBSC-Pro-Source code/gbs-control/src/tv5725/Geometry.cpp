#include "Geometry.h"

#include "Adc.h"
#include "CaptureWindow.h"
#include "InputFormatter.h"
#include "Memory.h"
#include "MemoryMap.h"
#include "OutputRaster.h"
#include "SyncProcessor.h"

namespace Tv5725 {

// --- Geometry ----------------------------------------------------------

Geometry::Geometry()
    : rasterPending_(false), samplingPending_(false), solvePending_(false),
      framingRequested_(false), rasterMode_(0),
      rasterLinePx_(0), rasterFrameLines_(0), activeStop_(0),
      activeLinesStop_(0) {}

const PanAndZoom &Geometry::framing() const { return framing_; }

void Geometry::requestFraming(const PanAndZoom &wanted)
{
    framing_ = wanted;
    framingRequested_ = true;
}

bool Geometry::apply()
{
    CaptureWindow capture;
    if (!measureSourceTimings(capture))
        return false;
    if (!calculateInputFormatterRegisters(capture))
        return false;

    RegisterSolution solved = calculateOutputRaster(capture);
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
        // a mode, and a raster nobody has swept keeps what it had. Nothing may
        // stay pending either -- a deferral outliving the mode it was for is how
        // a stale solve reaches a bypass.
        rasterPending_ = false;
        return false;
    }

    // A raster solved at the wrong rate is out by the ratio of the rates, and
    // this runs during a preset load while the source is still settling -- a
    // plain bounds check passes a transient comfortably. The line count is
    // reliable where the period measurement is not, so it picks the nominal and
    // the measurement only has to agree with it.
    // docs/firmware-geometry-engine.md
    float fieldRate = getSourceFieldRate(0);
    uint16_t sourceLines = SourceMeasurement::measureSourceLines();
    if (sourceLines < CaptureWindow::SourceVerticalTotalMin || sourceLines > CaptureWindow::SourceVerticalTotalMax) {
        // Deferred, not abandoned: the sync processor reads 97 or 98 for a
        // moment after a preset load and this is called in exactly that moment.
        // Giving up leaves the previous raster standing for the session,
        // invisibly -- the picture is fine and FrameSync steers the Si5351 to
        // whatever raster it finds.
        rasterPending_ = true;
        return false;
    }

    float nominal = sourceLines > CaptureWindow::PalVerticalTotalMin ? 50.0f : 60.0f;
    float error = fieldRate > nominal ? fieldRate / nominal : nominal / fieldRate;
    if (!(error < 1.02f)) {
        // Deferred for the same reason: the source settles a second or two
        // later and the retry gets it right.
        rasterPending_ = true;
        return false;
    }

    // EngineCeilingHz, not the higher WorkingCeilingHz the part is measured to
    // run at: a wider raster costs zoom travel. See the constant.
    RasterSolution raster = mode->solve(fieldRate, OutputRaster::EngineCeilingHz);
    if (!raster.usable())
        return false;

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

    // The seed, last of the raster group and first of the clock's: the caller
    // runs externalClockGenResetClock() next, which reads this byte back and
    // steers the Si5351 from it. loop() then stashes it and parks the 0x75
    // external sentinel here, which is why writing a real divider is safe.
    GBS::PLL648_CONTROL_01::write(raster.divider);

    // The porch is not a register, so the next apply() cannot read it back.
    activeStop_ = raster.activeStop;
    activeLinesStop_ = raster.activeLinesStop;

    rasterPending_ = false;
    return true;
}

void Geometry::adoptRaster()
{
    rasterLinePx_ = GBS::VDS_HSYNC_RST::read() + 1;
    rasterFrameLines_ = GBS::VDS_VSYNC_RST::read() + 1;
}

void Geometry::enterBypass()
{
    rasterMode_ = 0;
    rasterLinePx_ = 0;
    rasterFrameLines_ = 0;
    rasterPending_ = false;
    samplingPending_ = false;

    // Bypass has no solved raster, so it has no porch either -- and a porch left
    // from the last scaled mode would size the next one's picture.
    activeStop_ = 0;
    activeLinesStop_ = 0;
}

bool Geometry::rasterPending() const { return rasterPending_; }

bool Geometry::solveFromScratch()
{
    framing_.reset();
    return apply();
}

bool Geometry::solveIfPending()
{
    return solvePending_ ? apply() : false;
}

bool Geometry::applyRequested()
{
    if (!framingRequested_)
        return false;
    framingRequested_ = false;
    bool solved = apply();
    solvePending_ = false;
    return solved;
}

void Geometry::adoptSampling()
{
    sampling_.adopt();
    writeSampling();
}

// One quantity in three registers, each written by the block that declares it.
// The divider goes first because Adc latches it, and the latch loads KS, CKOS
// and ICP with it -- so anything setting those must already have run.
void Geometry::writeSampling()
{
    if (!sampling_.usable())
        return;

    Adc::applySampleRate(sampling_.divider());
    InputFormatter::writeLineCounter(sampling_.ifLine());
    SyncProcessor::writeRetimeStop(sampling_.retimeStop());
}

bool Geometry::solveSampling(uint8_t oversample)
{
    if (sampling_.measureLineRate()
        && sampling_.solve(sampling_.lineRateHz(), oversample)) {
        writeSampling();
        samplingPending_ = false;
        return true;
    }

    // Deferred, not settled for: the line rate could not be measured, so take
    // whatever divider is on the chip and ask again later. Adopting is needed
    // or every window defers forever; the flag is what stops it being
    // permanent, at 1856 -- bypassModeSwitch_RGBHV()'s literal -- every boot.
    samplingPending_ = true;
    if (!sampling_.usable())
        adoptSampling();
    return false;
}

bool Geometry::samplingPending() const { return samplingPending_; }

bool Geometry::recompute() { return apply(); }

int16_t Geometry::unitsFor(int16_t pixels, const Scale &scale, const Axis &axis)
{
    return pixels == 0 ? 0 : axis.stepUnits(pixels, scale.magnification());
}

bool Geometry::pan(int16_t dxPixels, int16_t dyPixels)
{
    PanAndZoom wanted = framing_;
    wanted.panBy(unitsFor(dxPixels, horizontalScale_, AxisHorizontal),
                 unitsFor(dyPixels, verticalScale_, AxisVertical));
    return step(wanted);
}

bool Geometry::zoom(int16_t dhPixels, int16_t dvPixels)
{
    PanAndZoom wanted = framing_;
    wanted.zoomBy(unitsFor(dhPixels, horizontalScale_, AxisHorizontal),
                  unitsFor(dvPixels, verticalScale_, AxisVertical));
    return step(wanted);
}

float Geometry::sourceFieldRateOr50Hz()
{
    float rate = getSourceFieldRate(0);
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
    if (!capture.readRasters(sampling_, getSourceFieldRate(0),
                             SourceMeasurement::measureSourceLines(),
                             SourceMeasurement::measureHsyncLow())) {
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
    return capture.usable() ? true : fail();
}

RegisterSolution Geometry::calculateOutputRaster(const CaptureWindow &capture) const
{
    return RegisterSolution(capture.horizontal().width(), capture.vertical().width(),
                            capture.linePx(), capture.frameLines(),
                            activeStop_, activeLinesStop_);
}

void Geometry::write(const RegisterSolution &solved, const CaptureWindow &capture)
{
    // 1. Far edges OUTWARD only, which can only add headroom. The memory window
    // hugs the picture, so it moves in as well as out; narrowing it here would
    // leave the old, wider display window showing unwritten memory at the far
    // edge for the length of a write. Inward moves wait for step 5b.
    if (solved.horizontal().windowStart() > GBS::VDS_HB_ST::read())
        GBS::VDS_HB_ST::write(solved.horizontal().windowStart());
    if (solved.vertical().windowStart() > GBS::VDS_VB_ST::read())
        GBS::VDS_VB_ST::write(solved.vertical().windowStart());

    // 2. Near edges down, if down is where they are going.
    if (solved.horizontal().windowStop() < GBS::VDS_HB_SP::read())
        GBS::VDS_HB_SP::write(solved.horizontal().windowStop());
    if (solved.vertical().windowStop() < GBS::VDS_VB_SP::read())
        GBS::VDS_VB_SP::write(solved.vertical().windowStop());

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
    GBS::VDS_HB_SP::write(solved.horizontal().windowStop());
    GBS::VDS_VB_SP::write(solved.vertical().windowStop());

    // 5. The aperture, which must hug the picture.
    GBS::VDS_DIS_HB_SP::write(solved.horizontal().displayStop());
    GBS::VDS_DIS_HB_ST::write(solved.horizontal().displayStart());
    GBS::VDS_DIS_VB_SP::write(solved.vertical().displayStop());
    GBS::VDS_DIS_VB_ST::write(solved.vertical().displayStart());

    // 5b. Far edges INWARD, now that the aperture they bound has closed. The
    // mirror of step 2: a window edge may only cross the display window's in
    // the direction that keeps the picture covered.
    if (solved.horizontal().windowStart() < GBS::VDS_HB_ST::read())
        GBS::VDS_HB_ST::write(solved.horizontal().windowStart());
    if (solved.vertical().windowStart() < GBS::VDS_VB_ST::read())
        GBS::VDS_VB_ST::write(solved.vertical().windowStart());

    // 6. The playback burst, only if it is not already right. Rewriting
    // PB_FETCH_NUM reprograms the playback FIFO while the picture is being read
    // out of it, which flickers even when the value written is identical.
    // docs/investigations/hscale-tearing-characterisation.md
    uint16_t fetch = Memory::fetchFor(capture.linePx(), capture.horizontal().width());
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

    if (!apply()) {
        framing_ = before;
        return false;
    }
    if (GBS::IF_HB_SP2::read() == horizontalStop && GBS::IF_HB_ST2::read() == horizontalStart
        && GBS::IF_VB_SP::read() == verticalStop && GBS::IF_VB_ST::read() == verticalStart)
        framing_ = before;
    return true;
}

}  // namespace Tv5725
