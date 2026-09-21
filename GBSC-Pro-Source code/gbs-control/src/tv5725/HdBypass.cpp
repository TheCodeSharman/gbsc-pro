#include "HdBypass.h"

#include <math.h>

#include "Axis.h"

#include "InputFormatter.h"
#include "VideoSignal.h"

#include "Adc.h"
#include "Chip.h"
#include "ColourSpace.h"
#include "ModeDetect.h"
#include "SamplingClock.h"
#include "SyncProcessor.h"
#include "SyncMeasurement.h"

namespace Tv5725 {

bool HdBypass::suitsLineRate(uint32_t lineRateHz)
{
    return lineRateHz >= MinLineRateHz;
}

bool HdBypass::suitsSource(uint16_t sourceLines, float fieldRateHz)
{
    if (sourceLines == 0)
        return false;
    if (InputFormatter::shouldDoubleLine(sourceLines))
        return false;

    if (!VideoSignal::fieldRateIsSource(fieldRateHz))
        return false;

    return suitsLineRate((uint32_t)((float)sourceLines * fieldRateHz));
}

namespace {

// The played-out line as a function of the line the CHANNEL sees.
// docs/investigations/one-bypass-route-carries-rgbhv.md
const uint16_t RasterGuardSamples = 8;

// How far the sample lags the sync the block emits beside it, in channel
// clocks. The sync generator does not account for the delay the channel adds to
// the sample beside it.
// Measured at 800x600@60 and 640x480@60: the same 40 either way, so it is the
// channel's delay rather than any source's back porch.
const uint16_t ChannelSyncDelay = 40;

// Where active video starts and stops on the line, as a fraction of it.
//
// Pass-through plays the source's raster out untouched, so what a STATED
// raster calls not-picture is the only thing this can blank correctly -- and
// it is what hides a mode file's border, which is black active video and so
// invisible to every measurement the chip can take. Where nothing matched,
// the envelope the scaling path captures across, which is deliberately early
// and leaves an unrecognised source its border: this path has no framing
// control to give a cropped picture back with.
// docs/investigations/vesa-modes-are-clipped-by-default.md
float activeStart(const SourceTiming &timing)
{
    return timing.published() ? timing.activeStart(AxisHorizontal)
                              : AxisHorizontal.activeStart();
}

float activeStop(const SourceTiming &timing)
{
    return timing.activeStart(AxisHorizontal) + timing.activeExtent(AxisHorizontal);
}

uint16_t onChannelLine(float fraction, uint16_t channelLine)
{
    return (uint16_t)lrintf(fraction * (float)channelLine);
}

// The end of the line where no raster stated one. It has to stay below
// HD_HSYNC_RST or the generator never opens at all.
uint16_t blankStart(const SourceTiming &timing, uint16_t channelLine)
{
    return timing.published() ? onChannelLine(activeStop(timing), channelLine)
                              : channelLine;
}

const uint16_t SyncPulseWidth = 124;

// The vertical sync the block emits, in lines of the frame it plays out. Every
// arm that carried one placed a pulse of five or six lines within ten of the
// frame's start, so there is no raster property behind the differences between
// them.
const uint16_t ChannelVsyncStart = 2;
const uint16_t ChannelVsyncStop = 7;

// What a component source wants forced into the channel's horizontal blanking
// on luma. RD-5725-1.1 gives no scale for it; 5 is what every table shipped.
const uint8_t ComponentBlankLuma = 5;

}  // namespace

SourceTiming HdBypass::timing_(0.0f);
uint16_t HdBypass::hsyncLow_ = 0;
uint16_t HdBypass::hsyncHigh_ = SyncPulseWidth;
uint16_t HdBypass::vsyncLow_ = ChannelVsyncStart;
uint16_t HdBypass::vsyncHigh_ = ChannelVsyncStop;

void HdBypass::init()
{
    hold();
}

void HdBypass::hold()
{
    SFTRST_HDBYPS_RSTZ::write(0);                // s0_47[3:3]
}

void HdBypass::release()
{
    SFTRST_HDBYPS_RSTZ::write(1);                // s0_47[3:3]
}

bool HdBypass::enabled()
{
    return SFTRST_HDBYPS_RSTZ::read() == 1;
}

void HdBypass::enable()
{
    release();

    HD_IN_DREG_BYPS::write(0);                   // s1_30[0:0]
    HD_SEL_BLK_IN::write(0);                     // s1_30[3:3]

    // RD-5725-1.1 documents these as a gain and an offset and gives no scale
    // for either, so 128/0 is carried without a derivation.
    HD_Y_GAIN::write(128);                       // s1_31[7:0]
    HD_Y_OFFSET::write(0);                       // s1_32[7:0]
    HD_U_GAIN::write(128);                       // s1_33[7:0]
    HD_U_OFFSET::write(0);                       // s1_34[7:0]
    HD_V_GAIN::write(128);                       // s1_35[7:0]
    HD_V_OFFSET::write(0);                       // s1_36[7:0]

    HD_HSYNC_RST::write(1023);                   // s1_37[10:0]

    // 0, where RD-5725-1.1's own resting value is 1046. Both bypass entries
    // overwrote it to 0 immediately, so the larger value never survived a
    // single line of video and was a resting value in name only.
    HD_INI_ST::write(0);                         // s1_39[10:0]
    HD_HB_ST::write(3976);                       // s1_3b[11:0]
    HD_HB_SP::write(208);                        // s1_3d[11:0]
    holdHsyncPulse(0, SyncPulseWidth);           // s1_3f, s1_41
    HD_VB_ST::write(0);                          // s1_43[11:0]
    HD_VB_SP::write(20);                         // s1_45[11:0]
    holdVsyncPulse(ChannelVsyncStart,            // s1_47, s1_49
                   ChannelVsyncStop);

    HD_EXT_VB_ST::write(0);                      // s1_4b[11:0]
    HD_EXT_VB_SP::write(6);                      // s1_4d[11:0]
    HD_EXT_HB_ST::write(0);                      // s1_4f[11:0]
    HD_EXT_HB_SP::write(6);                      // s1_51[11:0]

    HD_BLK_GY_DATA::write(0);                    // s1_53[7:0]
    HD_BLK_BU_DATA::write(0);                    // s1_54[7:0]
    HD_BLK_RV_DATA::write(0);                    // s1_55[7:0]
}

void HdBypass::applyForSource(uint16_t divider, uint32_t lineRateHz,
                              const SourceTiming &timing, uint16_t frameLines)
{
    timing_ = timing;
    applyPassThroughSampling(divider, lineRateHz);
    SyncProcessor::applySdVsyncPosition();
    applyVerticalBlanking(timing.activeStartLine(frameLines));
}

void HdBypass::applyVerticalBlanking(uint16_t activeStartLine)
{
    if (activeStartLine == 0)
        return;

    HD_VB_ST::write(0);
    HD_VB_SP::write(activeStartLine);
}

void HdBypass::applyHorizontalFromChannelLine(uint16_t channelLine)
{
    HD_HSYNC_RST::write(channelLine + RasterGuardSamples);

    HD_HB_ST::write(blankStart(timing_, channelLine));
    HD_HB_SP::write(onChannelLine(activeStart(timing_), channelLine));
}

uint16_t HdBypass::dividerFor(uint32_t lineRateHz)
{
    if (lineRateHz == 0)
        return 0;

    const uint32_t channelBound = MaxChannelLine - RasterGuardSamples;
    const uint32_t clockBound = MaxSampleClockHz / lineRateHz;
    return (uint16_t)(clockBound < channelBound ? clockBound : channelBound);
}

void HdBypass::applyPassThroughSampling(uint16_t divider, uint32_t lineRateHz,
                                        uint8_t oversample)
{
    if (divider == 0)
        return;

    // The charge pump, the VCO gain and the widest analog corner. It writes its
    // own divider, so the caller's goes in after it.
    Adc::applyForBypassRgbhv();

    // Divider, crossover row, VCO gain, clock tap and decimators in one call,
    // because PLLAD_LAT loads them together. The row and the gain follow the
    // clock the divider and the line rate make between them: frozen, they take
    // the PLL out of lock the moment either moves far enough.
    Adc::applySampleRate(divider, lineRateHz, oversample);

    applyHorizontalFromChannelLine(divider);

    holdHsyncPulse(ChannelSyncDelay, ChannelSyncDelay + SyncPulseWidth);
    holdVsyncPulse(ChannelVsyncStart, ChannelVsyncStop);
}

void HdBypass::holdHsyncPulse(uint16_t a, uint16_t b)
{
    hsyncLow_ = a < b ? a : b;
    hsyncHigh_ = a < b ? b : a;
    HD_HS_ST::write(a);
    HD_HS_SP::write(b);
}

void HdBypass::holdVsyncPulse(uint16_t a, uint16_t b)
{
    vsyncLow_ = a < b ? a : b;
    vsyncHigh_ = a < b ? b : a;
    HD_VS_ST::write(a);
    HD_VS_SP::write(b);
}

HdBypass::SourceSyncEdges HdBypass::readSourceSyncEdges()
{
    SourceSyncEdges edges;
    edges.hsyncFound = Tv5725::STATUS_SYNC_PROC_HSACT::read() == 1;
    edges.hsyncPositive = Tv5725::STATUS_SYNC_PROC_HSPOL::read() == 1;
    edges.vsyncFound = Tv5725::STATUS_SYNC_PROC_VSACT::read() == 1;
    edges.vsyncPositive = Tv5725::STATUS_SYNC_PROC_VSPOL::read() == 1;
    return edges;
}

void HdBypass::applyChannelSyncEdges(const SourceSyncEdges &edges)
{
    if (edges.hsyncFound) {
        holdHsyncPulse(edges.hsyncPositive ? hsyncLow_ : hsyncHigh_,
                       edges.hsyncPositive ? hsyncHigh_ : hsyncLow_);
        SyncProcessor::SP_HS2PLL_INV_REG::write(edges.hsyncPositive ? 0 : 1);
    }

    if (edges.vsyncFound)
        holdVsyncPulse(edges.vsyncPositive ? vsyncLow_ : vsyncHigh_,
                       edges.vsyncPositive ? vsyncHigh_ : vsyncLow_);
}

void HdBypass::applyBlankLevel(bool component)
{
    HD_BLK_GY_DATA::write(component ? ComponentBlankLuma : 0);
    HD_BLK_BU_DATA::write(0);
    HD_BLK_RV_DATA::write(0);
}

void HdBypass::applyColourPath(bool inputIsYpBpR)
{
    ColourSpace::DEC_MATRIX_BYPS::write(1);
    HD_MATRIX_BYPS::write(inputIsYpBpR ? 0 : 1);
    HD_DYN_BYPS::write(inputIsYpBpR ? 0 : 1);
}


}  // namespace Tv5725
