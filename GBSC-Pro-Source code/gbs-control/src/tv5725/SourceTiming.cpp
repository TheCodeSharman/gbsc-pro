#include "SourceTiming.h"

#include <math.h>

#include "Axis.h"
#include "SourceKey.h"

namespace Tv5725 {

// The rasters a source may be emitting, one array per authority: CEA-861 for
// broadcast timings, VESA DMT for computer ones, and the AKF50 monitor
// definition for the Acorn modes no standard states. Each row is the total, the
// sync width, where the PICTURE starts and how long it runs, per axis.
// Progressive modes only -- an interlaced source arrives as a field, and what
// its line count reads as has not been measured.
//
// BOTH START COLUMNS ARE COUNTED FROM THE SYNC PULSE'S LEADING EDGE. Where a
// counter's origin sits relative to that edge is the counter's business rather
// than the raster's, and the two blocks that read this table do not agree on it
// -- so each subtracts its own, and `vsync` is what the pulse takes for the one
// that ends there.
//
// **THE PICTURE, NOT THE ACTIVE REGION.** The two are the same thing in DMT and
// CEA, which state no border. An Acorn mode file states `sync, back porch, left
// border, display, right border, front porch`, and the border is black ACTIVE
// video -- so a window opened on the active region shows it as a black bar the
// source drew. The start is sync plus back porch plus border, and a source
// whose layout differs from the row that matched it overruns rather than
// underfills. ../../../docs/capture-window-default-tiers.md
//
// **THE ORDER OF THE ARRAYS IS THE PRIORITY**, and lookUp() applies it: a
// source matching a standard and a mode file is EMITTING the standard, so the
// mode file only answers for a raster no standard states.

namespace {

const SourceKey::Polarity P = SourceKey::Positive;
const SourceKey::Polarity N = SourceKey::Negative;

}  // namespace

const SourceTiming::Raster SourceTiming::Cea[] = {
    // frame  rate  total  sync  start  active  vsync  vstart  vactive  hpol vpol
    {  525,   60,     858,   62,    122,    720,    6,      36,     480, N, N},  // 720x480p
    {  625,   50,     864,   64,    132,    720,    5,      44,     576, N, N},  // 720x576p
};

const uint16_t SourceTiming::CeaCount = sizeof(Cea) / sizeof(Cea[0]);

const SourceTiming::Raster SourceTiming::Dmt[] = {
    // frame  rate  total  sync  start  active  vsync  vstart  vactive  hpol vpol
    {  525,   60,     800,   96,    144,    640,    2,      35,     480, N, N},  // 640x480@60
    {  520,   73,     832,   40,    168,    640,    3,      31,     480, N, N},  // 640x480@72
    {  500,   75,     840,   64,    184,    640,    3,      19,     480, N, N},  // 640x480@75
    {  625,   56,    1024,   72,    200,    800,    2,      24,     600, P, P},  // 800x600@56
    {  628,   60,    1056,  128,    216,    800,    4,      27,     600, P, P},  // 800x600@60
    {  666,   72,    1040,  120,    184,    800,    6,      29,     600, P, P},  // 800x600@72
    {  625,   75,    1056,   80,    240,    800,    3,      24,     600, P, P},  // 800x600@75
    {  806,   60,    1344,  136,    296,   1024,    6,      35,     768, N, N},  // 1024x768@60
    {  806,   70,    1328,  136,    280,   1024,    6,      35,     768, N, N},  // 1024x768@70
    {  800,   75,    1312,   96,    272,   1024,    3,      31,     768, P, P},  // 1024x768@75
    { 1066,   60,    1688,  112,    360,   1280,    3,      41,    1024, P, P},  // 1280x1024@60
    {  445,   85,     832,   64,    160,    640,    3,      63,     350, P, N},  // 640x350@85
    {  445,   85,     832,   64,    160,    640,    3,      44,     400, N, P},  // 640x400@85
};

const uint16_t SourceTiming::DmtCount = sizeof(Dmt) / sizeof(Dmt[0]);

// Every AKF50 mode, transcribed from the monitor definition.
//
// **MOST OF THEM SHARE A KEY WITH AN EARLIER ROW AND CANNOT BE REACHED.** The
// match is on the frame, the field rate, the sync duty and the polarity pair;
// the monitor definition gives every family one polarity, and nothing on this
// chip can measure a source's pixel clock -- so 320x256 and 1056x256 are one
// key, and so are the seven 15.6 kHz PAL modes together. The rows stay because
// the table is the mode file rather than a list of what the matcher can tell
// apart, and each says which earlier row answers for it.
//
// **WHICH MEANS THE ORDER INSIDE THIS ARRAY IS A CHOICE.** 320x256 leads
// because it is the one measured flush on the bench; the rest follow by line
// rate. Moving a row above it reframes every 15.6 kHz PAL source.
const SourceTiming::Raster SourceTiming::Acorn[] = {
    // frame  rate  total  sync  start  active  vsync  vstart  vactive  hpol vpol
    {  312,   50,     512,   36,    110,    320,    3,      36,     256, P, P},  // 320x256@50 15.6 kHz
    {  312,   50,     512,   36,    110,    320,    3,      39,     250, P, P},  // 320x250@50 15.6 kHz  -- one key with 320x256
    {  312,   50,    1024,   72,    222,    640,    3,      39,     250, P, P},  // 640x250@50 15.6 kHz  -- one key with 320x256
    {  312,   50,    1024,   72,    222,    640,    3,      36,     256, P, P},  // 640x256@50 15.6 kHz  -- one key with 320x256
    {  312,   50,    1024,   76,    158,    768,    3,      22,     288, P, P},  // 768x288@50 15.6 kHz  -- one key with 320x256
    {  312,   50,    1536,  108,    286,   1056,    3,      39,     250, P, P},  // 1056x250@50 15.6 kHz  -- one key with 320x256
    {  312,   50,    1536,  108,    286,   1056,    3,      36,     256, P, P},  // 1056x256@50 15.6 kHz  -- one key with 320x256
    {  262,   60,    1020,   72,    234,    640,    3,      37,     200, P, P},  // 640x200@60 15.7 kHz
    {  364,   60,    1100,  118,    176,    896,    3,      12,     352, P, N},  // 896x352@60 21.8 kHz
    {  364,   60,     768,   76,    112,    640,    3,      12,     352, P, N},  // 640x352@60 21.9 kHz  -- one key with 896x352
    {  534,   50,     896,   56,    168,    640,    3,      21,     512, P, P},  // 640x512@50 26.8 kHz
    {  449,   70,     300,   20,     44,    240,    2,      60,     352, P, N},  // 240x352@70 31.5 kHz
    {  525,   60,     400,   42,     68,    320,    2,      34,     480, N, N},  // 320x480@60 31.5 kHz
    {  449,   70,     600,   68,    150,    384,    2,      92,     288, P, N},  // 384x288@70 31.5 kHz
    {  449,   70,     600,   68,    102,    480,    2,      60,     352, P, N},  // 480x352@70 31.5 kHz  -- one key with 384x288
    {  525,   60,     800,   94,    138,    640,    2,      34,     480, N, N},  // 640x480@60 31.5 kHz  -- one key with 320x480
    {  525,   60,    1600,  188,    276,   1280,    2,      34,     480, N, N},  // 1280x480@60 31.5 kHz  -- one key with 320x480
    {  525,   60,     532,   64,    126,    360,    2,      34,     480, N, N},  // 360x480@60 31.5 kHz  -- one key with 640x480
    {  625,   56,    1024,   72,    190,    800,    2,      24,     600, P, P},  // 800x600@56 35.2 kHz
    {  625,   56,    2048,  144,    380,   1600,    2,      24,     600, P, P},  // 1600x600@56 35.2 kHz  -- one key with 800x600
    {  500,   75,     420,   24,     84,    320,    3,      19,     480, N, N},  // 320x480@75 37.5 kHz
    {  500,   75,     840,   64,    170,    640,    3,      19,     480, N, N},  // 640x480@75 37.5 kHz  -- one key with 320x480
    {  500,   75,    1680,  128,    342,   1280,    3,      19,     480, N, N},  // 1280x480@75 37.5 kHz  -- one key with 320x480
    {  520,   73,     416,   24,     80,    320,    3,      31,     480, N, N},  // 320x480@73 37.9 kHz
    {  520,   73,     832,   48,    162,    640,    3,      31,     480, N, N},  // 640x480@73 37.9 kHz  -- one key with 320x480
    {  520,   73,    1664,   96,    326,   1280,    3,      31,     480, N, N},  // 1280x480@73 37.9 kHz  -- one key with 320x480
    {  628,   60,    1056,  128,    216,    800,    4,      27,     600, P, P},  // 800x600@60 37.9 kHz
    {  628,   60,    2112,  256,    432,   1600,    4,      27,     600, P, P},  // 1600x600@60 37.9 kHz  -- one key with 800x600
};

const uint16_t SourceTiming::AcornCount = sizeof(Acorn) / sizeof(Acorn[0]);

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
const uint16_t StandardRateDeviationPerThousand = 50;

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

    // The polarity pair first, because two standards can share a frame, a rate
    // and a sync width and differ in nothing else. A key stating no polarity,
    // or one no row carries, still takes the first raster on the key.
    const Raster *found = firstMatch(measured, true);
    return found != 0 ? found : firstMatch(measured, false);
}

const SourceTiming::Raster *SourceTiming::firstMatch(const SourceKey &measured,
                                                     bool onPolarity)
{
    const Raster *found = lookUpIn(Cea, CeaCount, measured, onPolarity);
    if (found == 0)
        found = lookUpIn(Dmt, DmtCount, measured, onPolarity);
    if (found == 0)
        found = lookUpIn(Acorn, AcornCount, measured, onPolarity);
    return found;
}

const SourceTiming::Raster *SourceTiming::lookUpIn(const Raster *rasters,
                                                   uint16_t count,
                                                   const SourceKey &measured,
                                                   bool onPolarity)
{
    for (uint16_t i = 0; i < count; ++i) {
        const Raster &raster = rasters[i];
        if (measured.lines() + 1 != raster.totalLines
            || (fabsf(measured.rateHz() - (float)raster.rateHz) * 1000.0f
                > (float)StandardRateDeviationPerThousand * (float)raster.rateHz))
            continue;

        const float duty = (float)raster.syncPixels / (float)raster.totalPixels;
        if (fabsf(duty - measured.syncWidth()) > SyncDutyTolerance)
            continue;

        if (onPolarity
            && (raster.hsync != measured.hsyncPolarity()
                || raster.vsync != measured.vsyncPolarity()))
            continue;

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
