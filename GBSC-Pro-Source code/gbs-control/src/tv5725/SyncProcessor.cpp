#include "SyncProcessor.h"

#include "Adc.h"

namespace Tv5725 {

void SyncProcessor::writeSdVsyncStart(uint16_t start)
{
    SP_SDCS_VSST_REG_H::write(start >> 8);
    SP_SDCS_VSST_REG_L::write(start & 0xff);
}

void SyncProcessor::writeSdVsyncStop(uint16_t stop)
{
    SP_SDCS_VSSP_REG_H::write(stop >> 8);
    SP_SDCS_VSSP_REG_L::write(stop & 0xff);
}

void SyncProcessor::holdClamp()
{
    SP_NO_CLAMP_REG::write(1);
}

void SyncProcessor::releaseClamp()
{
    SP_NO_CLAMP_REG::write(0);
}

bool SyncProcessor::clampHeld()
{
    return SP_NO_CLAMP_REG::read() == 1;
}

void SyncProcessor::applyDefaultClampWindow()
{
    SP_CS_CLP_ST::write(32);
    SP_CS_CLP_SP::write(48);
}

void SyncProcessor::applyForSyncType(bool csync)
{
    // No ordering constraint between these fields is established, so the two
    // branches keep their own write order.
    if (csync) {
        SP_SOG_SRC_SEL::write(0);
        SP_EXT_SYNC_SEL::write(1);
        Adc::ADC_SOGEN::write(1);
        SP_SOG_MODE::write(1);
        SP_NO_COAST_REG::write(0);
        SP_PRE_COAST::write(4);
        SP_POST_COAST::write(7);
        SP_SYNC_BYPS::write(0);
        SP_HS_LOOP_SEL::write(1);
        SP_H_PROTECT::write(1);
    } else {
        SP_SOG_SRC_SEL::write(0);
        Adc::ADC_SOGEN::write(1);
        SP_EXT_SYNC_SEL::write(0);
        SP_SOG_MODE::write(0);
        SP_NO_COAST_REG::write(1);
        SP_PRE_COAST::write(0);
        SP_POST_COAST::write(0);
        SP_H_PULSE_IGNOR::write(0xff);
        SP_SYNC_BYPS::write(0);
        SP_HS_POL_ATO::write(1);
        SP_VS_POL_ATO::write(1);
        SP_HS_LOOP_SEL::write(1);
        SP_H_PROTECT::write(0);
    }
}

void SyncProcessor::clampFromReferenceClock()
{
    SP_CLP_SRC_SEL::write(0);
}

void SyncProcessor::setHsyncOverflowProtect(bool wanted)
{
    SP_H_PROTECT::write(wanted ? 1 : 0);
}

bool SyncProcessor::hsyncOverflowProtect()
{
    return SP_H_PROTECT::read() == 1;
}

void SyncProcessor::setCoastInvert(bool wanted)
{
    SP_COAST_INV_REG::write(wanted ? 1 : 0);
}

void SyncProcessor::setSubCoast(bool wanted)
{
    SP_DIS_SUB_COAST::write(wanted ? 0 : 1);
}

void SyncProcessor::selectExternalSync(uint8_t sel)
{
    SP_EXT_SYNC_SEL::write(sel);
}

void SyncProcessor::init()
{
    SP_SOG_P_INV::write(0x0);                    // s5_20[2:2]
    // The retime window's START. Its partner SP_RT_HS_SP is 93% of PLLAD_MD and
    // belongs to SourceMeasurement, which holds all three of that quantity's
    // registers off one divider.
    SP_RT_HS_ST::write(0x0);                     // s5_4a[11:0]
    SP_SYNC_TGL_THD::write(0x18);                // s5_21[7:0]
    SP_L_DLT_REG::write(0xF);                    // s5_22[7:0]
    SP_T_DLT_REG::write(0x40);                   // s5_24[11:0]
    SP_SYNC_PD_THD::write(0x4);                  // s5_26[11:0]
    SP_PRD_EQ_THD::write(0xF);                   // s5_2a[7:0]
    SP_VSYNC_TGL_THD::write(0x3);                // s5_2d[7:0]
    SP_SYNC_WIDTH_DTHD::write(0x0);              // s5_2e[7:0]
    SP_V_PRD_EQ_THD::write(0x2);                 // s5_2f[7:0]
    SP_VT_DLT_REG::write(0x2F);                  // s5_31[7:0]
    SP_VSIN_INV_REG::write(0x0);                 // s5_32[0:0]
    SP_V_TIMER_VAL::write(0x6);                  // s5_34[7:0]
    SP_CS_P_SWAP::write(0x0);                    // s5_3e[0:0]
    SP_HD_MODE::write(0x0);                      // s5_3e[1:1]
    SP_CS_INV_REG::write(0x0);                   // s5_3e[3:3]
    SP_RT_VS_ST::write(0x2);                     // s5_51[11:0]
    SP_RT_VS_SP::write(0x0);                     // s5_53[11:0]
    SP_HS_EP_DLY_SEL::write(0x0);                // s5_55[2:0]
    SP_HS_INV_REG::write(0x0);                   // s5_55[3:3]
    SP_VS_INV_REG::write(0x0);                   // s5_55[5:5]

    // The retiming module's auto-polarity, which only the separate-sync path
    // ever set -- so a csync source had nobody writing it, and the module went
    // on correcting polarity for the previous mode. 0 is the resting state, and
    // the separate-sync branch still wins because this runs first.
    SP_HS_POL_ATO::write(0x0);                   // s5_55[4:4]
    SP_VS_POL_ATO::write(0x0);                   // s5_55[6:6]
    SP_CLAMP_INV_REG::write(0x0);                // s5_56[7:7]
    SP_COAST_VALUE_REG::write(0x0);              // s5_57[3:3]
    SP_HT_DIFF_REG::write(0x5);                  // s5_58[11:0]
    SP_VT_DIFF_REG::write(0x1);                  // s5_5a[10:0]
    SP_STBLE_CNT_REG::write(0x3);                // s5_5c[7:0]
    SP_TEST_EN::write(0x1);                      // s5_63[0:0]
    SP_TEST_MODULE::write(0x7);                  // s5_63[3:1]
    SP_TEST_SIGNAL_SEL::write(0x0);              // s5_63[6:4]
}

void SyncProcessor::writeRetimeStop(uint16_t samples)
{
    SP_RT_HS_SP::write(samples);
}

}  // namespace Tv5725
