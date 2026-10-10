#ifndef TV5725_SOURCE_TIMING_H_
#define TV5725_SOURCE_TIMING_H_

// The published raster a source is emitting, where it is emitting one.
//
// Nothing on this chip can measure where active video starts -- a border is
// black active video, electrically identical to back porch -- so an untuned
// source is placed from an assumption. For a source running a standard mode the
// assumption can be exact instead: the standard states the whole raster, and
// the key identifies which one it is.
// docs/investigations/vesa-modes-are-clipped-by-default.md

#include <stdint.h>

#include "Aspect.h"
#include "SourceKey.h"

namespace Tv5725 {

class Axis;

class SourceTiming {
public:
    // A source running nothing the standards state, where its rate is all that
    // is known about it.
    SourceTiming(float fieldRateHz);

    // `sourceLines` is STATUS_SYNC_PROC_VTOTAL, which counts from zero and so
    // reads one short of the frame the standards state. `syncDuty` is the hsync
    // low time as a fraction of the line, which with the polarity pair is what
    // separates two standards sharing a frame and a field rate.
    static SourceTiming matching(const SourceKey &measured);

    float fieldRateHz() const;
    bool published() const;

    // Where the picture starts and how far it runs, as a fraction of the whole
    // line on the horizontal axis and of the whole frame on the vertical, both
    // counted from the sync pulse's leading edge. Where the COUNTER's origin
    // sits relative to that edge belongs to VideoSourceLine, which is what
    // holds the counter. Meaningless unless published().
    float activeStart(const Axis &axis) const;
    float activeExtent(const Axis &axis) const;

    // The shape the picture is to be shown in. Stated per row rather than
    // derived from the active counts, because 720x480p is 3:2 counted in pixels
    // and 4:3 on the screen. A source matching no raster is shown as 4:3, which
    // is what an unrecognised computer mode almost always is.
    Aspect aspect() const;

    // How many pixels across the raster states as picture. Zero where no
    // raster matched: nothing on the chip can measure it, so a source running
    // no standard mode has no pixel count at all.
    uint16_t activePixels() const;

    // How far the horizontal sync pulse runs, as a fraction of the whole line.
    // The one part of a published raster a source matching it cannot have spent
    // differently: the match is ON the sync width. Meaningless unless
    // published().
    float hsyncExtent() const;

    // The same vertical answer in lines, for a caller with a frame to count
    // against and no scale to apply -- pass-through plays the source's raster
    // out untouched, so the only thing it can blank correctly is what the
    // raster says is not picture. Zero where no raster matched.
    uint16_t activeStartLine(uint16_t frameLines) const;

    // Where active video stops, in the same units. A window opened at
    // activeStartLine and never closed runs to the end of the frame, which
    // plays the source's own end-of-frame blanking out as picture.
    uint16_t activeStopLine(uint16_t frameLines) const;

private:
    struct Raster {
        uint16_t totalLines, rateHz;
        uint16_t totalPixels, syncPixels, activeStartPixel, activePixels;
        uint16_t vsyncLines, activeStartLine, activeLines;
        SourceKey::Polarity hsync, vsync;
        uint16_t aspect;
    };

    // One array per authority, searched in that order: a source matching rows
    // in two of them is emitting the STANDARD, and the mode file is one
    // machine's description of it. Expressed as separate arrays rather than as
    // where a row sits in one, so a row added in the wrong place cannot change
    // which authority answers.
    static const Raster Cea[];
    static const uint16_t CeaCount;
    static const Raster Dmt[];
    static const uint16_t DmtCount;
    static const Raster Acorn[];
    static const uint16_t AcornCount;

    static const Raster *lookUp(const SourceKey &measured);
    static const Raster *firstMatch(const SourceKey &measured, bool onPolarity);
    static const Raster *lookUpIn(const Raster *rasters, uint16_t count,
                                  const SourceKey &measured, bool onPolarity);

    uint16_t lineFor(uint16_t statedLine, uint16_t frameLines) const;

    float fieldRateHz_;
    const Raster *raster_;
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_TIMING_H_
