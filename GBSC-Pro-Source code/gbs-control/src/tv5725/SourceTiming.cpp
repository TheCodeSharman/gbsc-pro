#include "SourceTiming.h"

#include <math.h>

#include "Axis.h"
#include "SourceKey.h"

namespace Tv5725 {

// VESA DMT and CEA-861: the total, the sync width, where active video starts and
// how long it runs, per axis. Progressive modes only -- an interlaced source
// arrives as a field, and what its line count reads as has not been measured.
//
// BOTH START COLUMNS ARE THE STANDARD'S OWN, counted from the sync pulse's
// leading edge: sync plus back porch, as stated. Where a counter's origin sits
// relative to that edge is the counter's business rather than the raster's, and
// the two blocks that read this table do not agree on it -- so each subtracts
// its own, and `vsync` is what the pulse takes for the one that ends there.
const SourceTiming::Raster SourceTiming::Published[] = {
    // frame  rate  total  sync  start  active  vsync  vstart  vactive
    {  525,   60,    800,   96,   144,    640,    2,      35,    480},  // 640x480@60
    {  520,   73,    832,   40,   168,    640,    3,      31,    480},  // 640x480@72
    {  500,   75,    840,   64,   184,    640,    3,      19,    480},  // 640x480@75
    {  625,   56,   1024,   72,   200,    800,    2,      24,    600},  // 800x600@56
    {  628,   60,   1056,  128,   216,    800,    4,      27,    600},  // 800x600@60
    {  666,   72,   1040,  120,   184,    800,    6,      29,    600},  // 800x600@72
    {  625,   75,   1056,   80,   240,    800,    3,      24,    600},  // 800x600@75
    {  806,   60,   1344,  136,   296,   1024,    6,      35,    768},  // 1024x768@60
    {  806,   70,   1328,  136,   280,   1024,    6,      35,    768},  // 1024x768@70
    {  800,   75,   1312,   96,   272,   1024,    3,      31,    768},  // 1024x768@75
    { 1066,   60,   1688,  112,   360,   1280,    3,      41,   1024},  // 1280x1024@60
    {  525,   60,    858,   62,   122,    720,    6,      36,    480},  // 720x480p
    {  625,   50,    864,   64,   132,    720,    5,      44,    576},  // 720x576p
};

const uint16_t SourceTiming::PublishedCount =
    sizeof(SourceTiming::Published) / sizeof(SourceTiming::Published[0]);

namespace {

// WHERE THE VERTICAL COUNTER'S ORIGIN SITS, in lines after the vsync pulse's
// LEADING edge. It is a property of the chip's vsync detection rather than of
// the source: measured across four pulse widths -- 2, 3, 4 and 6 lines -- the
// distance from the leading edge to the source's first active line is the
// standard's own figure less 7.3 to 7.9 lines, whatever the pulse.
//
// Taking the back porch alone instead treats the origin AS the trailing edge,
// which is right only where the pulse is 7 lines wide and is five lines late at
// 640x480@60 -- the top of the picture cut and the source's own blanking shown
// at the bottom in its place, with every register self-consistent.
//
// IT IS THE INPUT FORMATTER'S COUNTER AND NOTHING SAYS IT IS THE HD CHANNEL'S.
// Pass-through blanks against a counter in another block, which nothing here
// has measured, so activeStartLine() keeps taking the pulse's end as its origin
// -- which is where it has always put it.
// ../../../docs/investigations/the-vertical-capture-window-is-placed-late.md
const uint16_t VerticalOriginLines = 7;

// The bench reads 11.57% where DMT states 12.00%, so the match cannot be exact;
// the standards it has to tell apart are 4.8 points away from each other.
const float SyncDutyTolerance = 0.015f;

// How far a real source may sit from the rate the standard states. NOT a
// measurement tolerance -- the instrument is exact to better than 0.002%, and
// what this covers is the source itself: the bench RISC PC runs its nominal
// 50 Hz mode at 50.081, and its 800x600@60 at DMT's 60.317 exactly. A source
// that names a rate is not obliged to run it.
// ../../../docs/investigations/the-rate-tolerance-answered-five-questions.md
const uint16_t StandardRateDeviationPerMille = 50;

}  // namespace

SourceTiming::SourceTiming(float fieldRateHz)
    : fieldRateHz_(fieldRateHz), raster_(0) {}

SourceTiming SourceTiming::matching(const SourceKey &measured)
{
    SourceTiming timing(measured.rateHz());
    timing.raster_ = lookUp(measured);
    return timing;
}

const SourceTiming::Raster *SourceTiming::lookUp(const SourceKey &measured)
{
    if (!measured.valid() || measured.syncWidth() <= 0.0f)
        return 0;

    for (uint16_t i = 0; i < PublishedCount; ++i) {
        const Raster &raster = Published[i];
        if (measured.lines() + 1 != raster.totalLines
            || (fabsf(measured.rateHz() - (float)raster.rateHz) * 1000.0f
                > (float)StandardRateDeviationPerMille * (float)raster.rateHz))
            continue;

        const float duty = (float)raster.syncPixels / (float)raster.totalPixels;
        if (fabsf(duty - measured.syncWidth()) <= SyncDutyTolerance)
            return &raster;
    }
    return 0;
}

float SourceTiming::fieldRateHz() const { return fieldRateHz_; }

bool SourceTiming::published() const { return raster_ != 0; }

float SourceTiming::activeStart(const Axis &axis) const
{
    if (!published())
        return 0.0f;
    if (!axis.vertical())
        return (float)raster_->activeStartPixel / (float)raster_->totalPixels;

    const uint16_t start = raster_->activeStartLine > VerticalOriginLines
                         ? (uint16_t)(raster_->activeStartLine - VerticalOriginLines)
                         : 0;
    return (float)start / (float)raster_->totalLines;
}

float SourceTiming::activeExtent(const Axis &axis) const
{
    if (!published())
        return 0.0f;
    return axis.vertical()
        ? (float)raster_->activeLines / (float)raster_->totalLines
        : (float)raster_->activePixels / (float)raster_->totalPixels;
}

float SourceTiming::hsyncExtent() const
{
    if (!published())
        return 0.0f;
    return (float)raster_->syncPixels / (float)raster_->totalPixels;
}

uint16_t SourceTiming::activeStartLine(uint16_t frameLines) const
{
    if (!published())
        return 0;

    return lineFor(raster_->activeStartLine - raster_->vsyncLines, frameLines);
}

uint16_t SourceTiming::activeStopLine(uint16_t frameLines) const
{
    if (!published())
        return 0;

    return lineFor((uint16_t)(raster_->activeStartLine - raster_->vsyncLines
                              + raster_->activeLines), frameLines);
}

uint16_t SourceTiming::lineFor(uint16_t statedLine, uint16_t frameLines) const
{
    return (uint16_t)((float)statedLine / (float)raster_->totalLines
                          * (float)frameLines + 0.5f);
}

}  // namespace Tv5725
