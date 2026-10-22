#include "SourceTiming.h"

#include <math.h>

#include "Aspect.h"
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

const uint16_t F43 = Aspect::FourThree;
const uint16_t F54 = Aspect::FiveFour;

}  // namespace

const SourceTiming::Raster SourceTiming::Cea[] = {
    // frame  rate  total  sync  start  active  vsync  vstart  vactive  hpol vpol shape
    {  525,   60,     858,   62,    122,    720,    6,      36,     480, N, N, F43},  // 720x480p
    {  625,   50,     864,   64,    132,    720,    5,      44,     576, N, N, F43},  // 720x576p
};

const uint16_t SourceTiming::CeaCount = sizeof(Cea) / sizeof(Cea[0]);

const SourceTiming::Raster SourceTiming::Dmt[] = {
    // frame  rate  total  sync  start  active  vsync  vstart  vactive  hpol vpol shape
    {  525,   60,     800,   96,    144,    640,    2,      35,     480, N, N, F43},  // 640x480@60
    {  520,   73,     832,   40,    168,    640,    3,      31,     480, N, N, F43},  // 640x480@72
    {  500,   75,     840,   64,    184,    640,    3,      19,     480, N, N, F43},  // 640x480@75
    {  625,   56,    1024,   72,    200,    800,    2,      24,     600, P, P, F43},  // 800x600@56
    {  628,   60,    1056,  128,    216,    800,    4,      27,     600, P, P, F43},  // 800x600@60
    {  666,   72,    1040,  120,    184,    800,    6,      29,     600, P, P, F43},  // 800x600@72
    {  625,   75,    1056,   80,    240,    800,    3,      24,     600, P, P, F43},  // 800x600@75
    {  806,   60,    1344,  136,    296,   1024,    6,      35,     768, N, N, F43},  // 1024x768@60
    {  806,   70,    1328,  136,    280,   1024,    6,      35,     768, N, N, F43},  // 1024x768@70
    {  800,   75,    1312,   96,    272,   1024,    3,      31,     768, P, P, F43},  // 1024x768@75
    { 1066,   60,    1688,  112,    360,   1280,    3,      41,    1024, P, P, F54},  // 1280x1024@60
    {  445,   85,     832,   64,    160,    640,    3,      63,     350, P, N, F43},  // 640x350@85
    {  445,   85,     832,   64,    160,    640,    3,      44,     400, N, P, F43},  // 640x400@85
};

const uint16_t SourceTiming::DmtCount = sizeof(Dmt) / sizeof(Dmt[0]);

// The Acorn monitor definitions, transcribed: every AKF50 mode, then what the
// faster definitions add. A RISC PC emits these and not the standard's own
// timings for the same resolution, so this is the authority for that machine
// rather than a fallback for rasters no standard states.
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
    // frame  rate  total  sync  start  active  vsync  vstart  vactive  hpol vpol shape
    {  312,   50,     512,   36,    110,    320,    3,      36,     256, P, P, F43},  // 320x256@50 15.6 kHz
    {  312,   50,     512,   36,    110,    320,    3,      39,     250, P, P, F43},  // 320x250@50 15.6 kHz  -- one key with 320x256
    {  312,   50,    1024,   72,    222,    640,    3,      39,     250, P, P, F43},  // 640x250@50 15.6 kHz  -- one key with 320x256
    {  312,   50,    1024,   72,    222,    640,    3,      36,     256, P, P, F43},  // 640x256@50 15.6 kHz  -- one key with 320x256
    {  312,   50,    1024,   76,    158,    768,    3,      22,     288, P, P, F43},  // 768x288@50 15.6 kHz  -- one key with 320x256
    {  312,   50,    1536,  108,    286,   1056,    3,      39,     250, P, P, F43},  // 1056x250@50 15.6 kHz  -- one key with 320x256
    {  312,   50,    1536,  108,    286,   1056,    3,      36,     256, P, P, F43},  // 1056x256@50 15.6 kHz  -- one key with 320x256
    {  262,   60,    1020,   72,    234,    640,    3,      37,     200, P, P, F43},  // 640x200@60 15.7 kHz
    {  364,   60,    1100,  118,    176,    896,    3,      12,     352, P, N, F43},  // 896x352@60 21.8 kHz
    {  364,   60,     768,   76,    112,    640,    3,      12,     352, P, N, F43},  // 640x352@60 21.9 kHz  -- one key with 896x352
    {  534,   50,     896,   56,    168,    640,    3,      21,     512, P, P, F43},  // 640x512@50 26.8 kHz
    {  449,   70,     300,   20,     44,    240,    2,      60,     352, P, N, F43},  // 240x352@70 31.5 kHz
    {  525,   60,     400,   42,     68,    320,    2,      34,     480, N, N, F43},  // 320x480@60 31.5 kHz
    {  449,   70,     600,   68,    150,    384,    2,      92,     288, P, N, F43},  // 384x288@70 31.5 kHz
    {  449,   70,     600,   68,    102,    480,    2,      60,     352, P, N, F43},  // 480x352@70 31.5 kHz  -- one key with 384x288
    {  525,   60,     800,   94,    138,    640,    2,      34,     480, N, N, F43},  // 640x480@60 31.5 kHz  -- one key with 320x480
    {  525,   60,    1600,  188,    276,   1280,    2,      34,     480, N, N, F43},  // 1280x480@60 31.5 kHz  -- one key with 320x480
    {  525,   60,     532,   64,    126,    360,    2,      34,     480, N, N, F43},  // 360x480@60 31.5 kHz  -- one key with 640x480
    {  625,   56,    1024,   72,    190,    800,    2,      24,     600, P, P, F43},  // 800x600@56 35.2 kHz
    {  625,   56,    2048,  144,    380,   1600,    2,      24,     600, P, P, F43},  // 1600x600@56 35.2 kHz  -- one key with 800x600
    {  500,   75,     420,   24,     84,    320,    3,      19,     480, N, N, F43},  // 320x480@75 37.5 kHz
    {  500,   75,     840,   64,    170,    640,    3,      19,     480, N, N, F43},  // 640x480@75 37.5 kHz  -- one key with 320x480
    {  500,   75,    1680,  128,    342,   1280,    3,      19,     480, N, N, F43},  // 1280x480@75 37.5 kHz  -- one key with 320x480
    {  520,   73,     416,   24,     80,    320,    3,      31,     480, N, N, F43},  // 320x480@73 37.9 kHz
    {  520,   73,     832,   48,    162,    640,    3,      31,     480, N, N, F43},  // 640x480@73 37.9 kHz  -- one key with 320x480
    {  520,   73,    1664,   96,    326,   1280,    3,      31,     480, N, N, F43},  // 1280x480@73 37.9 kHz  -- one key with 320x480
    {  628,   60,    1056,  128,    216,    800,    4,      27,     600, P, P, F43},  // 800x600@60 37.9 kHz
    {  628,   60,    2112,  256,    432,   1600,    4,      27,     600, P, P, F43},  // 1600x600@60 37.9 kHz  -- one key with 800x600

    // --- What the faster monitor definitions add, AKF60 through AKF92.
    //
    // AKF50 stops at 37.9 kHz, so without these a machine on a faster
    // definition emits a raster this table does not hold and falls through to
    // whichever standard shares its frame and rate -- which is a DIFFERENT
    // raster, not a looser description of the same one. AKF60's 1024x768@60 is
    // the worst of them: one total, one clock, one frame with DMT 16, and 71
    // pixels between the two picture starts.
    //
    // Ordered by line rate as the block above is. The order decides nothing
    // but an exact tie now that the duty picks the row.
    {  449,   60,     312,   20,     56,    240,    2,     60,     352, P, N, F43},  // 240x352@60 AKF91 26.9 kHz
    {  449,   69,     608,   20,     92,    480,    2,     60,     352, P, N, F43},  // 480x352@69 AKF70 30.9 kHz
    {  449,   70,     400,   36,     62,    320,    2,    111,     250, P, N, F43},  // 320x250@70 AKF60 31.5 kHz
    {  449,   70,     400,   36,     62,    320,    2,    108,     256, P, N, F43},  // 320x256@70 AKF60 31.5 kHz  -- one key with 320x250
    {  449,   70,     800,   88,    132,    640,    2,    136,     200, P, N, F43},  // 640x200@70 AKF60 31.5 kHz
    {  449,   70,     800,   88,    132,    640,    2,    111,     250, P, N, F43},  // 640x250@70 AKF60 31.5 kHz  -- one key with 640x200
    {  449,   70,     800,   88,    132,    640,    2,    108,     256, P, N, F43},  // 640x256@70 AKF60 31.5 kHz  -- one key with 640x200
    {  449,   70,     800,   88,    132,    640,    2,     60,     352, P, N, F43},  // 640x352@70 AKF60 31.5 kHz  -- one key with 640x200
    {  525,   61,    1580,  188,    256,   1280,    2,     34,     480, N, N, F43},  // 1280x480@61 AKF91 31.9 kHz
    {  597,   56,     400,   36,     62,    320,    2,     54,     512, N, N, F43},  // 320x512@56 AKF91 33.4 kHz
    {  520,   70,     448,   24,     80,    320,    3,     31,     480, N, N, F43},  // 320x480@70 AKF91 36.2 kHz
    {  615,   60,     870,   64,    170,    640,    3,     59,     512, N, N, F43},  // 640x512@60 AKF91 36.8 kHz
    {  525,   70,     472,   50,     96,    360,    2,     34,     480, P, P, F43},  // 360x480@70 AKF70 36.9 kHz
    {  540,   69,    1712,   96,    300,   1280,    3,     41,     480, N, N, F43},  // 1280x480@69 AKF91 37.4 kHz
    {  525,   71,     800,   94,    138,    640,    2,     34,     480, N, N, F43},  // 640x480@71 AKF91 37.5 kHz
    {  615,   70,     870,   64,    170,    640,    3,     59,     512, N, N, F43},  // 640x512@70 AKF91 43.1 kHz
    {  625,   75,    2112,  160,    336,   1600,    3,     24,     600, P, P, F43},  // 1600x600@75 AKF60 46.9 kHz
    {  625,   75,    1056,   80,    168,    800,    3,     24,     600, P, P, F43},  // 800x600@75 AKF60 46.9 kHz  -- one key with 1600x600, and an exact tie with DMT 11, which answers
    {  666,   72,    2080,  176,    328,   1600,    6,     29,     600, P, P, F43},  // 1600x600@72 AKF60 48.1 kHz
    {  666,   72,    1040,   88,    164,    800,    6,     29,     600, P, P, F43},  // 800x600@72 AKF60 48.1 kHz  -- one key with 1600x600
    {  806,   60,    1344,  128,    224,   1024,    6,     35,     768, P, P, F43},  // 1024x768@60 AKF60 48.4 kHz
    {  806,   70,    1328,  124,    232,   1024,    6,     35,     768, P, P, F43},  // 1024x768@70 AKF80 56.5 kHz
    {  800,   75,    1338,  122,    238,   1024,    3,     31,     768, P, P, F43},  // 1024x768@75 AKF80 60.0 kHz
    { 1163,   54,    1736,  166,    396,   1280,    3,     85,    1024, P, P, F54},  // 1280x1024@54 AKF91 63.4 kHz
    { 1062,   60,    1728,  166,    352,   1280,    3,     35,    1024, P, P, F54},  // 1280x1024@60 AKF80 63.7 kHz
};

const uint16_t SourceTiming::AcornCount = sizeof(Acorn) / sizeof(Acorn[0]);

namespace {

// How far the DUTY MEASUREMENT may be wrong, which is the whole of what this
// bounds: the nearest row is what gets picked, so this only refuses a row that
// is not the source's raster at all. Measured worst case is 0.30 points on
// separate sync and the separator adds about 0.23 more on composite.
// ../../../docs/investigations/full-screen-framing-on-the-vesa-modes.md
const float SyncDutyTolerance = 0.008f;

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

    // The polarity pair first, because it is a BIT rather than a measurement:
    // where it separates two rows it is stronger evidence than a nearer duty.
    // A key stating no polarity, or one no row carries, falls back to the duty
    // alone.
    const Raster *found = nearestMatch(measured, true);
    return found != 0 ? found : nearestMatch(measured, false);
}

// THE NEAREST ROW, NOT THE FIRST AUTHORITY THAT MATCHES. Two authorities
// describe the same resolution with different porches, and the duty is the only
// thing on the chip that can tell them apart -- so the authority an entry came
// from says nothing about which one a source is running, and taking the
// standard's row for a machine running its own monitor definition places the
// window where the source is not.
// ../../../docs/investigations/full-screen-framing-on-the-vesa-modes.md
const SourceTiming::Raster *SourceTiming::nearestMatch(const SourceKey &measured,
                                                       bool onPolarity)
{
    const Raster *found = 0;
    float nearest = 0.0f;
    nearestIn(Cea, CeaCount, measured, onPolarity, &found, &nearest);
    nearestIn(Dmt, DmtCount, measured, onPolarity, &found, &nearest);
    nearestIn(Acorn, AcornCount, measured, onPolarity, &found, &nearest);
    return found;
}

// The array order breaks a tie, which is what leaves a standard answering for
// a mode file that states the same raster.
void SourceTiming::nearestIn(const Raster *rasters, uint16_t count,
                             const SourceKey &measured, bool onPolarity,
                             const Raster **found, float *nearest)
{
    for (uint16_t i = 0; i < count; ++i) {
        const Raster &raster = rasters[i];
        if (measured.lines() != raster.totalLines
            || (fabsf(measured.rateHz() - (float)raster.rateHz) * 1000.0f
                > (float)StandardRateDeviationPerThousand * (float)raster.rateHz))
            continue;

        if (onPolarity
            && (raster.hsync != measured.hsyncPolarity()
                || raster.vsync != measured.vsyncPolarity()))
            continue;

        const float duty = (float)raster.syncPixels / (float)raster.totalPixels;
        const float apart = fabsf(duty - measured.syncWidth());
        if (apart > SyncDutyTolerance || (*found != 0 && apart >= *nearest))
            continue;

        *found = &raster;
        *nearest = apart;
    }
}

float SourceTiming::fieldRateHz() const { return fieldRateHz_; }

bool SourceTiming::published() const { return raster_ != 0; }

Aspect SourceTiming::aspect() const
{
    return Aspect(published() ? raster_->aspect : Aspect::FourThree);
}

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

float SourceTiming::lineStep() const
{
    return published() ? 1.0f / (float)raster_->totalLines : 0.0f;
}

uint16_t SourceTiming::activePixels() const
{
    return published() ? raster_->activePixels : 0;
}

float SourceTiming::pixelSamples(uint16_t divider) const
{
    if (!published() || divider == 0 || raster_->totalPixels == 0)
        return 0.0f;
    return (float)divider / (float)raster_->totalPixels;
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
