#include "Geometry.h"

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

bool Capture::readRasters()
{
    linePx_ = GBS::VDS_HSYNC_RST::read() + 1;
    frameLines_ = GBS::VDS_VSYNC_RST::read() + 1;
    wrapH_ = GBS::IF_HSYNC_RST::read() + 1;

    // How much of the line the hsync pulse takes is a property of the source, so
    // it is measured. Both are in ADC samples, the one space they share -- the
    // denominator is the divider, not STATUS_SYNC_PROC_HTOTAL, which only echoes
    // PLLAD_MD back.
    hlowLen_ = GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
    adcLine_ = GBS::PLLAD_MD::read();

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

Geometry::Geometry() : solvePending_(false), framingRequested_(false) {}

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
                              capture.linePx(), capture.frameLines());
    if (!solved.usable())
        return fail();

    write(solved, capture);
    solvePending_ = false;
    return true;
}

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
    if (!capture.readRasters()) {
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

    capture.setWindows(framing_.capture(h, fieldRate, false, capture.linePx()),
                       framing_.capture(v, fieldRate, true, capture.frameLines()));
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
    uint16_t offset = Memory::offsetFor(capture.linePx());
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
