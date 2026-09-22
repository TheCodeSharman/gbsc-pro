#include "VideoSourceLine.h"

#include "Tv5725Log.h"

#include <math.h>
#include <stdio.h>

namespace Tv5725 {

const uint16_t VideoSourceLine::DoubledHeadBlankingUnits;
const uint16_t VideoSourceLine::FirstCapturableUnit;
const float VideoSourceLine::CaptureLagFraction = 0.0539f;
const float VideoSourceLine::FrameLagUnits = -7.0f;

VideoSourceLine::VideoSourceLine(uint16_t units)
    : units_(units), syncUnits_(0), lag_(0.0f), headBlankingUnits_(0),
      syncAtHead_(true) {}

VideoSourceLine::VideoSourceLine(uint16_t units, uint16_t syncUnits)
    : units_(units), syncUnits_(syncUnits), lag_(0.0f), headBlankingUnits_(0),
      syncAtHead_(true) {}

VideoSourceLine::VideoSourceLine(uint16_t units, uint16_t syncUnits,
                                 uint16_t headBlankingUnits, bool syncAtHead)
    : units_(units), syncUnits_(syncUnits),
      lag_(headBlankingUnits ? 0.0f : units * CaptureLagFraction),
      headBlankingUnits_(headBlankingUnits), syncAtHead_(syncAtHead) {}

VideoSourceLine VideoSourceLine::frame(uint16_t units)
{
    VideoSourceLine line(units);
    line.lag_ = FrameLagUnits;
    return line;
}

uint16_t VideoSourceLine::units() const { return units_; }

uint16_t VideoSourceLine::syncUnits() const { return syncUnits_; }

float VideoSourceLine::videoLag() const { return lag_; }

uint16_t VideoSourceLine::progressiveStop(uint16_t start) const
{
    return start + units_;
}

uint16_t VideoSourceLine::firstCapture() const
{
    // Zero is not a capture start. Measured at 640x480@60, whose pulse is
    // behind the origin and so raises the floor off nothing: IF_HB_SP2 at 0
    // doubles and smears the picture, and 1 is clean with every other register
    // identical. The tail keeps two units clear of the wrap for its own
    // reasons; this is the head's equivalent.
    const long floor = lrintf(lag_) + (long)headBlankingUnits_
                     + (syncAtHead_ ? (long)syncUnits_ : 0L);
    return floor < (long)FirstCapturableUnit ? FirstCapturableUnit : (uint16_t)floor;
}

uint16_t VideoSourceLine::maxCaptureWidth() const
{
    return capturable();
}

uint16_t VideoSourceLine::videoAt(float lineFraction) const
{
    long at = lrintf(lineFraction * (float)units_ + lag_)
            - (syncAtHead_ ? 0L : (long)syncUnits_);
    if (at < 0)
        at = 0;
    return at > (long)units_ ? units_ : (uint16_t)at;
}

float VideoSourceLine::fractionAt(uint16_t position) const
{
    if (units_ == 0)
        return 0.0f;
    const float at = (float)position + (syncAtHead_ ? 0.0f : (float)syncUnits_)
                   - lag_;
    return at < 0.0f ? 0.0f : at / (float)units_;
}

uint16_t VideoSourceLine::lastCapture() const
{
    // Neither of the last two units is a capture stop. `units` is the wrap
    // point, and a window written onto it rolls rather than clamping;
    // units - 1 is the line reset value, where the input formatter stops
    // producing pixels at all. docs/scaler-geometry-model.md
    //
    // THE PULSE IS NOT TAKEN OFF THE TAIL. Where the line is counted from the
    // pulse's trailing edge the next line's pulse does occupy the tail, and
    // excluding it costs picture: measured at 640x480@60 the right-hand border
    // goes with it. What arrives there is bounded by the wrap, not by the
    // pulse. docs/known-issues.md
    return units_ < 2 ? 0 : units_ - 2;
}

uint16_t VideoSourceLine::lastReachable() const
{
    // Past lastCapture() where the video runs AHEAD of the counter, which is
    // the frame's case: the video at the counter's last unit came from further
    // down the source, so a framing may name that far. Bounded by the wrap.
    const long at = (long)lastCapture() - lrintf(lag_);
    if (at < 0)
        return 0;
    return at > (long)units_ ? units_ : (uint16_t)at;
}

uint16_t VideoSourceLine::capturable() const
{
    uint16_t first = firstCapture(), last = lastCapture();
    return last > first ? last - first : 0;
}

VideoSourceLine VideoSourceLine::forDuty(uint16_t units, const HsyncPulse &pulse,
                                         bool lineDoubled)
{
    // Round UP, so a pulse that ends part way through a unit leaves that unit
    // outside the capture rather than half in it. HsyncPulse's ceiling keeps it
    // under a sixth of the line, so what is left is always the greater part.
    return VideoSourceLine(units, (uint16_t)ceilf(units * pulse.syncDuty()),
                           lineDoubled ? DoubledHeadBlankingUnits : 0,
                           pulse.syncAtHead());
}

}  // namespace Tv5725
