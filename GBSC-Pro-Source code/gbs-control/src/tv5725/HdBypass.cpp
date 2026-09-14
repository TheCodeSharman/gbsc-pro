#include "HdBypass.h"

#include "Adc.h"
#include "Chip.h"
#include "ColourSpace.h"
#include "ModeDetect.h"
#include "SourceMeasurement.h"
#include "SyncProcessor.h"
#include "SyncMeasurement.h"

namespace Tv5725 {

namespace {

// The narrowest of RD-5725-1.1's four corners for ADC_FLTR.
const uint8_t AnalogFilter40MHz = 3;

// The two line counts at which an RGBHV source's ADC PLL changes crossover row.
const uint16_t RgbhvShortLines = 532;
const uint16_t RgbhvTallLines = 810;

// The played-out line as a function of the line the CHANNEL sees.
// docs/investigations/one-bypass-route-carries-rgbhv.md
const uint16_t RasterGuardSamples = 8;
const float ActiveFraction = 0.945f;
const uint16_t BlankEndSamples = 0x90;

// How far the sample lags the sync the block emits beside it, in channel
// clocks. The sync generator does not account for the delay the channel adds to
// the sample beside it.
// Measured at 800x600@60 and 640x480@60: the same 40 either way, so it is the
// channel's delay rather than any source's back porch.
const uint16_t ChannelSyncDelay = 40;
const uint16_t SyncPulseWidth = 124;

// The vertical sync the block emits, in lines of the frame it plays out. Every
// arm that carried one placed a pulse of five or six lines within ten of the
// frame's start, so there is no raster property behind the differences between
// them.
const uint16_t ChannelVsyncStart = 2;
const uint16_t ChannelVsyncStop = 7;

}  // namespace

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

void HdBypass::applyForStandard(uint8_t standard, uint16_t divider,
                                uint32_t lineRateHz,
                                uint16_t activeStartLine,
                                void (*applyRgbPatches)())
{
    // Everything with no arm of its own is sampled from the divider the engine
    // holds, 0 included: it is "nothing recognised" rather than a standard.
    if (standard == 1 || standard == 2)
        applySd(standard);
    else if (standard == 3 || standard == 4)
        applyProgressive(standard, divider, lineRateHz);
    else if (standard == 13)
        applyComponent(applyRgbPatches);
    else {
        applyPassThroughSampling(divider, lineRateHz);
        SyncProcessor::applySdVsyncPosition();
    }

    applyVerticalBlanking(activeStartLine);
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

    // At the end of the line, not at a fraction of it. Pass-through plays out
    // whatever the source sends and the source's own porches are already black,
    // so blanking earlier only takes picture off the right -- measured, 0.945
    // of the line cost the last 3% of the panel. It still has to sit below
    // HD_HSYNC_RST or the generator never opens at all.
    HD_HB_ST::write(channelLine);
    HD_HB_SP::write(BlankEndSamples);
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

void HdBypass::applySd(uint8_t standard)
{
    SyncProcessor::SP_HS2PLL_INV_REG::write(1);
    SyncProcessor::SP_CS_P_SWAP::write(1);
    SyncProcessor::SP_HS_PROC_INV_REG::write(1);

    ModeDetect::MD_HS_FLIP::write(1);
    ModeDetect::MD_VS_FLIP::write(1);
    Chip::OUT_SYNC_SEL::write(2);
    SyncProcessor::SP_HS_LOOP_SEL::write(0);
    Adc::ADC_FLTR::write(AnalogFilter40MHz);

    HD_HSYNC_RST::write((Adc::PLLAD_MD::read() / 2) + RasterGuardSamples);
    HD_HB_ST::write(Adc::PLLAD_MD::read() * ActiveFraction);
    HD_HB_SP::write(BlankEndSamples);
    holdHsyncPulse(0x80, 0x00);

    SyncProcessor::SP_CS_HS_ST::write(0xA0);
    SyncProcessor::SP_CS_HS_SP::write(0x00);

    if (standard == 1) {
        SyncProcessor::writeSdVsyncStart(250);
        SyncProcessor::writeSdVsyncStop(1);
        holdVsyncPulse(3, 522);
    }
    if (standard == 2) {
        SyncProcessor::writeSdVsyncStart(301);
        SyncProcessor::writeSdVsyncStop(5);
        holdVsyncPulse(1, 621);
    }
}

void HdBypass::applyProgressive(uint8_t standard, uint16_t divider,
                                uint32_t lineRateHz)
{
    applyPassThroughSampling(divider, lineRateHz);

    holdVsyncPulse(0x06, 0x00);
    if (standard == 3) {
        SyncProcessor::writeSdVsyncStart(525 - 5);
        SyncProcessor::writeSdVsyncStop(525 - 3);
    }
    if (standard == 4) {
        SyncProcessor::writeSdVsyncStart(48);
        SyncProcessor::writeSdVsyncStop(46);
    }
}

void HdBypass::applyComponent(void (*applyRgbPatches)())
{
    applyRgbPatches();
    SyncMeasurement::set(true);
    SyncProcessor::SP_PRE_COAST::write(4);
    SyncProcessor::SP_POST_COAST::write(4);
    SyncProcessor::SP_DLT_REG::write(0x70);
    SyncProcessor::SP_VS_PROC_INV_REG::write(0);

    Adc::PLLAD_KS::write(0);
    Adc::applyOversample(0, 1);
    Adc::PLLAD_MD::write(512);

    applyRgbhvPll(SourceMeasurement::measureSourceLines());
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

void HdBypass::applyColourPath(bool inputIsYpBpR)
{
    ColourSpace::DEC_MATRIX_BYPS::write(1);
    HD_MATRIX_BYPS::write(inputIsYpBpR ? 0 : 1);
    HD_DYN_BYPS::write(inputIsYpBpR ? 0 : 1);
}

void HdBypass::applyRgbhvPll(uint16_t sourceLines)
{
    if (sourceLines < RgbhvShortLines) {
        Adc::PLLAD_KS::write(3);
        Adc::PLLAD_FS::write(1);
    } else if (sourceLines < RgbhvTallLines) {
        Adc::PLLAD_FS::write(0);
        Adc::PLLAD_KS::write(2);
    } else {
        Adc::PLLAD_KS::write(2);
        Adc::PLLAD_FS::write(1);
    }
}

}  // namespace Tv5725
