#include "SourceTiming.h"

#include <math.h>

#include "Axis.h"
#include "SourceKey.h"

namespace Tv5725 {

// VESA DMT and CEA-861: the total, the sync width, where active video starts and
// how long it runs, per axis. Progressive modes only -- an interlaced source
// arrives as a field, and what its line count reads as has not been measured.
//
// **THE TWO START COLUMNS ARE COUNTED FROM DIFFERENT EDGES, because the two
// counters are.** The horizontal counter zeroes on the hsync pulse's LEADING
// edge, so `start` is the standard's sync plus back porch, as stated. The
// vertical counter zeroes on the vsync pulse's TRAILING edge, so `vstart` is the
// BACK PORCH ALONE -- carrying the standard's figure there places the window a
// sync width into the picture, which is a whole-frame downward shift the
// registers cannot show.
// docs/investigations/the-capture-tail-was-one-unit-short.md
const SourceTiming::Raster SourceTiming::Published[] = {
    // frame  rate  total  sync  start  active  vstart  vactive
    {  525,   60,    800,   96,   144,    640,     33,    480},  // 640x480@60
    {  520,   73,    832,   40,   168,    640,     28,    480},  // 640x480@72
    {  500,   75,    840,   64,   184,    640,     16,    480},  // 640x480@75
    {  625,   56,   1024,   72,   200,    800,     22,    600},  // 800x600@56
    {  628,   60,   1056,  128,   216,    800,     23,    600},  // 800x600@60
    {  666,   72,   1040,  120,   184,    800,     23,    600},  // 800x600@72
    {  625,   75,   1056,   80,   240,    800,     21,    600},  // 800x600@75
    {  806,   60,   1344,  136,   296,   1024,     29,    768},  // 1024x768@60
    {  806,   70,   1328,  136,   280,   1024,     29,    768},  // 1024x768@70
    {  800,   75,   1312,   96,   272,   1024,     28,    768},  // 1024x768@75
    { 1066,   60,   1688,  112,   360,   1280,     38,   1024},  // 1280x1024@60
    {  525,   60,    858,   62,   122,    720,     30,    480},  // 720x480p
    {  625,   50,    864,   64,   132,    720,     39,    576},  // 720x576p
};

const uint16_t SourceTiming::PublishedCount =
    sizeof(SourceTiming::Published) / sizeof(SourceTiming::Published[0]);

namespace {

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
    return axis.vertical()
        ? (float)raster_->activeStartLine / (float)raster_->totalLines
        : (float)raster_->activeStartPixel / (float)raster_->totalPixels;
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

    return (uint16_t)(activeStart(AxisVertical) * frameLines + 0.5f);
}

uint16_t SourceTiming::activeStopLine(uint16_t frameLines) const
{
    if (!published())
        return 0;

    return (uint16_t)((activeStart(AxisVertical) + activeExtent(AxisVertical))
                          * frameLines + 0.5f);
}

}  // namespace Tv5725
