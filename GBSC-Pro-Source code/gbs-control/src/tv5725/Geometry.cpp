#include "Geometry.h"

#include "Memory.h"
#include "MemoryMap.h"
#include "OutputRaster.h"

namespace Tv5725 {

// --- Capture ---------------------------------------------------------

Capture::Capture()
    : horizontalStop_(0), horizontalStart_(0), verticalStop_(0), verticalStart_(0), linePx_(0), frameLines_(0),
      wrapH_(0), wrapV_(0), hlowLen_(0), adcLine_(0) {}

uint16_t Capture::horizontalStop() const { return horizontalStop_; }

uint16_t Capture::horizontalStart() const { return horizontalStart_; }

uint16_t Capture::verticalStop() const { return verticalStop_; }

uint16_t Capture::verticalStart() const { return verticalStart_; }

uint16_t Capture::linePx() const { return linePx_; }

uint16_t Capture::frameLines() const { return frameLines_; }

InputLine Capture::lineH() const
{
    return InputLine::measured(wrapH_, hlowLen_, adcLine_);
}

InputLine Capture::lineV() const { return InputLine(wrapV_); }

uint16_t Capture::captureH() const { return horizontalStart_ - horizontalStop_; }

uint16_t Capture::captureV() const { return verticalStart_ - verticalStop_; }

bool Capture::scaling() const
{
    return linePx_ >= 64 && frameLines_ >= 64;
}

bool Capture::usable() const { return horizontalStart_ > horizontalStop_ && verticalStart_ > verticalStop_; }

bool Capture::readRasters(const Sampling &sampling)
{
    linePx_ = GBS::VDS_HSYNC_RST::read() + 1;
    frameLines_ = GBS::VDS_VSYNC_RST::read() + 1;
    wrapH_ = sampling.ifLine() + 1;

    // How much of the line the hsync pulse takes is a property of the source, so
    // it is measured. Both are in ADC samples, the one space they share -- the
    // denominator is the divider, not STATUS_SYNC_PROC_HTOTAL, which only echoes
    // PLLAD_MD back.
    hlowLen_ = GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
    adcLine_ = sampling.divider();

    uint16_t sourceVerticalTotal = GBS::STATUS_SYNC_PROC_VTOTAL::read();
    if (wrapH_ < 64 || sourceVerticalTotal < SourceVerticalTotalMin
        || sourceVerticalTotal > SourceVerticalTotalMax)
        return false;

    // IF_VB counts half-lines, so it rolls at twice the source frame.
    wrapV_ = 2 * (sourceVerticalTotal + 1);
    return true;
}

void Capture::setWindows(const CaptureWindow &h, const CaptureWindow &v)
{
    horizontalStop_ = h.sp();
    horizontalStart_ = h.st();
    verticalStop_ = v.sp();
    verticalStart_ = v.st();
}

// --- Geometry ----------------------------------------------------------

Geometry::Geometry()
    : rasterPending_(false), samplingPending_(false), solvePending_(false),
      framingRequested_(false), rasterMode_(0), activeStop_(0),
      activeLinesStop_(0) {}

const PanAndZoom &Geometry::framing() const { return framing_; }

void Geometry::requestFraming(const PanAndZoom &wanted)
{
    framing_ = wanted;
    framingRequested_ = true;
}

bool Geometry::apply()
{
    Capture capture;
    if (!readCapture(capture))
        return false;

    RegisterSolution solved(capture.captureH(), capture.captureV(),
                              capture.linePx(), capture.frameLines(),
                              activeStop_, activeLinesStop_);
    if (!solved.usable())
        return fail();

    write(solved, capture);
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
    uint16_t sourceLines = GBS::STATUS_SYNC_PROC_VTOTAL::read();
    if (sourceLines < Capture::SourceVerticalTotalMin || sourceLines > Capture::SourceVerticalTotalMax) {
        // Deferred, not abandoned: the sync processor reads 97 or 98 for a
        // moment after a preset load and this is called in exactly that moment.
        // Giving up leaves the previous raster standing for the session,
        // invisibly -- the picture is fine and FrameSync steers the Si5351 to
        // whatever raster it finds.
        rasterPending_ = true;
        return false;
    }

    float nominal = sourceLines > Capture::PalVerticalTotalMin ? 50.0f : 60.0f;
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

void Geometry::enterBypass()
{
    rasterMode_ = 0;
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

void Geometry::adoptSampling() { sampling_.adopt(); sampling_.write(); }

bool Geometry::solveSampling(uint32_t lineRateHz, uint8_t oversample)
{
    if (sampling_.solve(lineRateHz, oversample)) {
        sampling_.write();
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

bool Geometry::pan(int16_t dx, int16_t dy)
{
    PanAndZoom wanted = framing_;
    wanted.panBy(dx, dy);
    return step(wanted);
}

bool Geometry::zoom(int16_t dh, int16_t dv)
{
    PanAndZoom wanted = framing_;
    wanted.zoomBy(dh, dv);
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

bool Geometry::readCapture(Capture &capture)
{
    if (!capture.readRasters(sampling_)) {
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

    float fieldRate = sourceFieldRateOr50Hz();

    // Store only a framing this source can realise. A press big enough to
    // overshoot an edge still moves the window a unit or two, so step() accepts
    // it and the framing keeps the overshoot; every smaller press back then
    // produces an identical window and step() reverts it, leaving the control
    // dead in that direction. Only the hold ramp presses that far -- measured
    // pv -51 against a limit of -46, ph -144 against -134.
    InputLine h = capture.lineH();
    InputLine v = capture.lineV();
    framing_.clampToLine(h, fieldRate, false, capture.linePx());
    framing_.clampToLine(v, fieldRate, true, capture.frameLines());

    CaptureWindow windowH = framing_.capture(h, fieldRate, false, capture.linePx());
    CaptureWindow windowV = framing_.capture(v, fieldRate, true, capture.frameLines());

    // A pixel costs one 32-bit word and the capture buffer holds 1,703,936 of
    // them; capture width is in ADC samples and PLLAD_MD is 12 bits, so a line
    // wide enough to overrun is reachable. The chip's answer to an overrun is to
    // wrap, putting a wrong address on screen and reporting nothing. Narrowed
    // rather than refused, for the reason clampToLine() clamps: a dead picture
    // with no way back is the worse failure.
    const uint16_t fits = MemoryMap::clampWidth(windowH.width(), windowV.width());
    if (fits < windowH.width())
        windowH = CaptureWindow(windowH.sp(), windowH.sp() + fits);

    capture.setWindows(windowH, windowV);
    return capture.usable() ? true : fail();
}

void Geometry::write(const RegisterSolution &solved, const Capture &capture)
{
    // 1. Far edges OUTWARD only, which can only add headroom. The memory window
    // hugs the picture, so it moves in as well as out; narrowing it here would
    // leave the old, wider display window showing unwritten memory at the far
    // edge for the length of a write. Inward moves wait for step 5b.
    if (solved.h().windowStart() > GBS::VDS_HB_ST::read())
        GBS::VDS_HB_ST::write(solved.h().windowStart());
    if (solved.v().windowStart() > GBS::VDS_VB_ST::read())
        GBS::VDS_VB_ST::write(solved.v().windowStart());

    // 2. Near edges down, if down is where they are going.
    if (solved.h().windowStop() < GBS::VDS_HB_SP::read())
        GBS::VDS_HB_SP::write(solved.h().windowStop());
    if (solved.v().windowStop() < GBS::VDS_VB_SP::read())
        GBS::VDS_VB_SP::write(solved.v().windowStop());

    // 3. The picture. BYPS cleared because an explicit scale was computed.
    //
    // The line double's progressive window spans one whole line, so it is
    // recomputed on every solve. Its start is written rather than read, or a
    // clobbered preset byte would propagate into the stop.
    GBS::IF_LINE_ST::write(Capture::ProgressiveStart);
    GBS::IF_LINE_SP::write(capture.lineH().progressiveStop(Capture::ProgressiveStart));
    GBS::IF_HB_SP2::write(capture.horizontalStop());
    GBS::IF_HB_ST2::write(capture.horizontalStart());
    GBS::IF_VB_SP::write(capture.verticalStop());
    GBS::IF_VB_ST::write(capture.verticalStart());
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HSCALE::write(solved.horizontalScale().reg());
    GBS::VDS_VSCALE::write(solved.verticalScale().reg());

    // 4. Near edges up, now that the picture they bound is the new one.
    GBS::VDS_HB_SP::write(solved.h().windowStop());
    GBS::VDS_VB_SP::write(solved.v().windowStop());

    // 5. The aperture, which must hug the picture.
    GBS::VDS_DIS_HB_SP::write(solved.h().displayStop());
    GBS::VDS_DIS_HB_ST::write(solved.h().displayStart());
    GBS::VDS_DIS_VB_SP::write(solved.v().displayStop());
    GBS::VDS_DIS_VB_ST::write(solved.v().displayStart());

    // 5b. Far edges INWARD, now that the aperture they bound has closed. The
    // mirror of step 2: a window edge may only cross the display window's in
    // the direction that keeps the picture covered.
    if (solved.h().windowStart() < GBS::VDS_HB_ST::read())
        GBS::VDS_HB_ST::write(solved.h().windowStart());
    if (solved.v().windowStart() < GBS::VDS_VB_ST::read())
        GBS::VDS_VB_ST::write(solved.v().windowStart());

    // 6. The playback burst, only if it is not already right. Rewriting
    // PB_FETCH_NUM reprograms the playback FIFO while the picture is being read
    // out of it, which flickers even when the value written is identical.
    // docs/investigations/hscale-tearing-characterisation.md
    uint16_t fetch = Memory::fetchFor(capture.linePx(), capture.captureH());
    uint16_t offset = Memory::offsetFor(capture.lineH().units());
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
