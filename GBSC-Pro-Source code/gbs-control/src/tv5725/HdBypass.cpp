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

// RD-5725-1.1's four corners for ADC_FLTR.
const uint8_t AnalogFilter150MHz = 0;
const uint8_t AnalogFilter110MHz = 1;
const uint8_t AnalogFilter70MHz = 2;
const uint8_t AnalogFilter40MHz = 3;

// The two line counts at which an RGBHV source's ADC PLL changes crossover row.
const uint16_t RgbhvShortLines = 532;
const uint16_t RgbhvTallLines = 810;

// The played-out line as a function of the line the CHANNEL sees, which is the
// divider over the oversampling ratio rather than the divider itself.
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

// Undecimated, and it cannot usefully be anything else.
//
// Oversampling on this part is bought from the SAME crossover ladder, not from
// a second clock: Adc::applyOversample() takes a faster tap of the one VCO, so
// each doubling costs a step of PLLAD_KS headroom. Asking for two therefore
// caps the ADC clock at the top row's 80 MHz, which halves what reaches the
// channel -- measured on an 800x600 source, the played-out line falls to 928
// samples for 800 active pixels and the gratings stop resolving.
//
// The band where it would cost nothing is below about 19.6 kHz, and that is
// under the rate a bypassed source needs to reach the sink at all.
// test_hd_bypass.cpp pins both halves.
const uint8_t BypassOversample = 1;

}  // namespace

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
    HD_INI_ST::write(1046);                      // s1_39[10:0]
    HD_HB_ST::write(3976);                       // s1_3b[11:0]
    HD_HB_SP::write(208);                        // s1_3d[11:0]
    HD_HS_ST::write(0);                          // s1_3f[11:0]
    HD_HS_SP::write(124);                        // s1_41[11:0]
    HD_VB_ST::write(0);                          // s1_43[11:0]
    HD_VB_SP::write(20);                         // s1_45[11:0]
    HD_VS_ST::write(2);                          // s1_47[11:0]
    HD_VS_SP::write(7);                          // s1_49[11:0]

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
                                void (*applyRgbPatches)())
{
    if (standard <= 2)
        applySd(standard);
    else if (standard == 3 || standard == 4)
        applyProgressive(standard);
    else if (standard <= 7 || standard == 13)
        applyHd(standard, applyRgbPatches);
    else
        applyRgbhv(divider, lineRateHz);

    if (standard == 13)
        applyRgbhvPll(SourceMeasurement::measureSourceLines());
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

void HdBypass::applyRgbhv(uint16_t divider, uint32_t lineRateHz)
{
    if (divider == 0)
        return;

    // The charge pump, the VCO gain and the widest analog corner. It writes its
    // own divider, so the caller's goes in after it.
    Adc::applyForBypassRgbhv();

    // Divider, crossover row, clock tap and decimators in one call, because
    // PLLAD_LAT loads them together. The row follows the clock the divider and
    // the line rate make between them: frozen, it takes the PLL out of lock the
    // moment either moves far enough.
    const uint8_t ratio =
        Adc::applySampleRate(divider, lineRateHz, BypassOversample);

    applyHorizontalFromChannelLine(divider / (ratio < 1 ? 1 : ratio));

    HD_HS_ST::write(ChannelSyncDelay);
    HD_HS_SP::write(ChannelSyncDelay + SyncPulseWidth);
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
    HD_HS_ST::write(0x80);
    HD_HS_SP::write(0x00);

    SyncProcessor::SP_CS_HS_ST::write(0xA0);
    SyncProcessor::SP_CS_HS_SP::write(0x00);

    if (standard == 1) {
        SyncProcessor::writeSdVsyncStart(250);
        SyncProcessor::writeSdVsyncStop(1);
        HD_VB_ST::write(500);
        HD_VS_ST::write(3);
        HD_VS_SP::write(522);
        HD_VB_SP::write(16);
    }
    if (standard == 2) {
        SyncProcessor::writeSdVsyncStart(301);
        SyncProcessor::writeSdVsyncStop(5);
        HD_VB_ST::write(605);
        HD_VS_ST::write(1);
        HD_VS_SP::write(621);
        HD_VB_SP::write(16);
    }
}

void HdBypass::applyProgressive(uint8_t standard)
{
    Adc::ADC_FLTR::write(AnalogFilter70MHz);
    Adc::PLLAD_KS::write(1);
    Adc::PLLAD_CKOS::write(0);

    HD_HB_ST::write(0x864);

    HD_HB_SP::write(0xa0);
    HD_VB_ST::write(0x00);
    HD_VB_SP::write(0x40);
    if (standard == 3) {
        HD_HS_ST::write(0x54);
        HD_HS_SP::write(0x864);
        HD_VS_ST::write(0x06);
        HD_VS_SP::write(0x00);
        SyncProcessor::writeSdVsyncStart(525 - 5);
        SyncProcessor::writeSdVsyncStop(525 - 3);
    }
    if (standard == 4) {
        HD_HS_ST::write(0x10);
        HD_HS_SP::write(0x880);
        HD_VS_ST::write(0x06);
        HD_VS_SP::write(0x00);
        SyncProcessor::writeSdVsyncStart(48);
        SyncProcessor::writeSdVsyncStop(46);
    }
}

void HdBypass::applyHd(uint8_t standard, void (*applyRgbPatches)())
{
    if (standard == 5) {
        Adc::PLLAD_MD::write(2474);
        HD_HSYNC_RST::write(550);

        Adc::PLLAD_KS::write(0);
        Adc::PLLAD_CKOS::write(0);
        Adc::ADC_FLTR::write(AnalogFilter150MHz);
        Adc::ADC_CLK_ICLK1X::write(0);
        Adc::DEC2_BYPS::write(1);
        Adc::PLLAD_ICP::write(6);
        Adc::PLLAD_FS::write(1);
        HD_HB_ST::write(0);
        HD_HB_SP::write(0x140);
        HD_HS_ST::write(0x20);
        HD_HS_SP::write(0x80);
        HD_VB_ST::write(0x00);
        HD_VB_SP::write(0x6c);
        HD_VS_ST::write(0x00);
        HD_VS_SP::write(0x05);
        SyncProcessor::writeSdVsyncStart(2);
        SyncProcessor::writeSdVsyncStop(0);
    }
    if (standard == 6) {
        HD_HSYNC_RST::write(0x710);

        Adc::PLLAD_KS::write(1);
        Adc::PLLAD_CKOS::write(0);
        Adc::ADC_FLTR::write(AnalogFilter110MHz);
        HD_HB_ST::write(0);
        HD_HB_SP::write(0xb8);
        HD_HS_ST::write(0x04);
        HD_HS_SP::write(0x50);
        HD_VB_ST::write(0x00);
        HD_VB_SP::write(0x1e);
        HD_VS_ST::write(0x04);
        HD_VS_SP::write(0x09);
        SyncProcessor::writeSdVsyncStart(8);
        SyncProcessor::writeSdVsyncStop(6);
    }
    if (standard == 7) {
        Adc::PLLAD_MD::write(2749);
        HD_HSYNC_RST::write(0x710);

        Adc::PLLAD_KS::write(0);
        Adc::PLLAD_CKOS::write(0);
        Adc::ADC_FLTR::write(AnalogFilter150MHz);
        Adc::ADC_CLK_ICLK1X::write(0);
        Adc::DEC2_BYPS::write(1);
        Adc::PLLAD_ICP::write(6);
        Adc::PLLAD_FS::write(1);
        HD_HB_ST::write(0x00);
        HD_HB_SP::write(0xb0);
        HD_HS_ST::write(0x20);
        HD_HS_SP::write(0x70);
        HD_VB_ST::write(0x00);
        HD_VB_SP::write(0x2f);
        HD_VS_ST::write(0x04);
        HD_VS_SP::write(0x0A);
    }
    if (standard == 13) {
        applyRgbPatches();
        SyncMeasurement::set(true);
        SyncProcessor::SP_PRE_COAST::write(4);
        SyncProcessor::SP_POST_COAST::write(4);
        SyncProcessor::SP_DLT_REG::write(0x70);
        SyncProcessor::SP_VS_PROC_INV_REG::write(0);

        Adc::PLLAD_KS::write(0);
        Adc::PLLAD_CKOS::write(0);
        Adc::ADC_CLK_ICLK1X::write(0);
        Adc::ADC_CLK_ICLK2X::write(0);
        Adc::DEC1_BYPS::write(1);
        Adc::DEC2_BYPS::write(1);
        Adc::PLLAD_MD::write(512);
    }
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
