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
    // low time as a fraction of the line, which is what separates two standards
    // sharing a frame and a field rate.
    static SourceTiming matching(const SourceKey &measured);

    float fieldRateHz() const;
    bool published() const;

    // Where active video starts and how far it runs, as a fraction of the whole
    // line on the horizontal axis and of the whole frame on the vertical.
    // Meaningless unless published().
    float activeStart(const Axis &axis) const;
    float activeExtent(const Axis &axis) const;

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

private:
    struct Raster {
        uint16_t totalLines, rateHz;
        uint16_t totalPixels, syncPixels, activeStartPixel, activePixels;
        uint16_t activeStartLine, activeLines;
    };

    static const Raster Published[];
    static const uint16_t PublishedCount;

    static const Raster *lookUp(const SourceKey &measured);

    float fieldRateHz_;
    const Raster *raster_;
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_TIMING_H_
