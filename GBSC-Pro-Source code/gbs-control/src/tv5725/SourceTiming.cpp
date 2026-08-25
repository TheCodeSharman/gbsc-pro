#include "SourceTiming.h"

#include <math.h>

#include "Axis.h"
#include "SourceKey.h"

namespace Tv5725 {

// VESA DMT and CEA-861, as the standards state them: the total, the sync width,
// where active video starts and how long it runs, per axis. Progressive modes
// only -- an interlaced source arrives as a field, and what its line count reads
// as has not been measured.
const SourceTiming::Raster SourceTiming::Published[] = {
    // frame  rate  total  sync  start  active  vstart  vactive
    {  525,   60,    800,   96,   144,    640,     35,    480},  // 640x480@60
    {  520,   73,    832,   40,   168,    640,     31,    480},  // 640x480@72
    {  500,   75,    840,   64,   184,    640,     19,    480},  // 640x480@75
    {  625,   56,   1024,   72,   200,    800,     24,    600},  // 800x600@56
    {  628,   60,   1056,  128,   216,    800,     27,    600},  // 800x600@60
    {  666,   72,   1040,  120,   184,    800,     29,    600},  // 800x600@72
    {  625,   75,   1056,   80,   240,    800,     24,    600},  // 800x600@75
    {  806,   60,   1344,  136,   296,   1024,     35,    768},  // 1024x768@60
    {  806,   70,   1328,  136,   280,   1024,     35,    768},  // 1024x768@70
    {  800,   75,   1312,   96,   272,   1024,     31,    768},  // 1024x768@75
    { 1066,   60,   1688,  112,   360,   1280,     41,   1024},  // 1280x1024@60
    {  525,   60,    858,   62,   122,    720,     36,    480},  // 720x480p
    {  625,   50,    864,   64,   132,    720,     44,    576},  // 720x576p
};

const uint16_t SourceTiming::PublishedCount =
    sizeof(SourceTiming::Published) / sizeof(SourceTiming::Published[0]);

namespace {

// The bench reads 11.57% where DMT states 12.00%, so the match cannot be exact;
// the standards it has to tell apart are 4.8 points away from each other.
const float SyncDutyTolerance = 0.015f;

}  // namespace

SourceTiming::SourceTiming(float fieldRateHz)
    : fieldRateHz_(fieldRateHz), raster_(0) {}

SourceTiming SourceTiming::matching(uint16_t sourceLines, float fieldRateHz,
                                    float syncDuty)
{
    SourceTiming timing(fieldRateHz);
    timing.raster_ = lookUp(sourceLines, fieldRateHz, syncDuty);
    return timing;
}

const SourceTiming::Raster *SourceTiming::lookUp(uint16_t sourceLines,
                                                 float fieldRateHz,
                                                 float syncDuty)
{
    const SourceKey measured(sourceLines, fieldRateHz);
    if (!measured.valid() || syncDuty <= 0.0f)
        return 0;

    for (uint16_t i = 0; i < PublishedCount; ++i) {
        const Raster &raster = Published[i];
        if (measured.lines() + 1 != raster.totalLines
            || measured.rateBucket() != raster.rateBucket)
            continue;

        const float duty = (float)raster.syncPixels / (float)raster.totalPixels;
        if (fabsf(duty - syncDuty) <= SyncDutyTolerance)
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

}  // namespace Tv5725
