#include "HdBypass.h"

#include "Adc.h"
#include "Chip.h"
#include "ColourSpace.h"
#include "ModeDetect.h"
#include "SourceMeasurement.h"
#include "SyncProcessor.h"
#include "SyncType.h"

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
    HD_MATRIX_BYPS::write(0);                    // s1_30[1:1]
    HD_DYN_BYPS::write(0);                       // s1_30[2:2]
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

void HdBypass::applyForStandard(uint8_t standard, void (*applyRgbPatches)())
{
    if (standard <= 2)
        applySd(standard);
    else if (standard == 3 || standard == 4)
        applyProgressive(standard);
    else if (standard <= 7 || standard == 13)
        applyHd(standard, applyRgbPatches);

    if (standard == 13)
        applyRgbhvPll(SourceMeasurement::measureSourceLines());
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

    HD_HSYNC_RST::write((Adc::PLLAD_MD::read() / 2) + 8);
    HD_HB_ST::write(Adc::PLLAD_MD::read() * 0.945f);
    HD_HB_SP::write(0x90);
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
        SyncType::set(true);
        ColourSpace::DEC_MATRIX_BYPS::write(1);
        SyncProcessor::SP_PRE_COAST::write(4);
        SyncProcessor::SP_POST_COAST::write(4);
        SyncProcessor::SP_DLT_REG::write(0x70);
        HD_MATRIX_BYPS::write(1);
        HD_DYN_BYPS::write(1);
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
