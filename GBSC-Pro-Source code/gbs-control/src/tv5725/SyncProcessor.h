#ifndef TV5725_SYNC_PROCESSOR_H
#define TV5725_SYNC_PROCESSOR_H

#include "Tv5725.h"

namespace Tv5725 {

// The sync processor's static half: polarity inversions, the thresholds it uses
// to decide a sync edge is real, and its stability counters.
//
// IT COUNTS IN ADC SAMPLES, NOT IF UNITS: three of this block's neighbours hold
// values well above the 1277-unit IF line, and HLOW_LEN only matches the
// source's mode file read in ADC -- 181/2553 = 7.09% against AKF50's 36/512 =
// 7.03%, where the IF reading gives twice the mode's sync width.
//
// What is not here is everything that moves per source: updateSpDynamic() owns
// the coast and delta quadruple, updateCoastPosition() SP_H_CST_ST/SP,
// updateClampPosition() the clamp, and Tv5725::SourceMeasurement SP_RT_HS_SP. A static
// write of any of those would fight a per-source decision.
//
// DO NOT POISON SP_RT_HS_SP TO TEST ANYTHING. Set 1110 against a 2553-sample
// line, and again at only 100 low, SP_VTOTAL fell to a steady 97/98 through the
// load and for minutes afterwards, needing /sc?~ to recover.
class SyncProcessor {
public:
    typedef UReg<0x05, 0x20, 0, 1> SP_SOG_SRC_SEL;                    // sog_src_sel Sog signal source select. 0: from ADC. 1:
                                                                      // select hs as sog source

    typedef UReg<0x05, 0x20, 1, 1> SP_SOG_P_ATO;                      // sog_p_ato sog auto correct polarity

    typedef UReg<0x05, 0x20, 2, 1> SP_SOG_P_INV;                      // Sog invert Invert sog

    typedef UReg<0x05, 0x20, 3, 1> SP_EXT_SYNC_SEL;                   // ext_sync_sel Ext 2 set Hs_Hs select

    typedef UReg<0x05, 0x20, 4, 1> SP_JITTER_SYNC;                    // Sync using both rising and falling trigger Use falling
                                                                      // and rising edge to sync input Hsync

    typedef UReg<0x05, 0x21, 0, 8> SP_SYNC_TGL_THD;                   // h active detect control Sync toggle times threshold

    typedef UReg<0x05, 0x22, 0, 8> SP_L_DLT_REG;                      // h active detect control Sync pulse width different
                                                                      // threshold (little than this as equal)

    typedef UReg<0x05, 0x24, 0, 12> SP_T_DLT_REG;                     // H active detect control H total width different threshold

    typedef UReg<0x05, 0x26, 0, 12> SP_SYNC_PD_THD;                   // H active detect control H sync pulse width threshold

    typedef UReg<0x05, 0x2A, 0, 8> SP_PRD_EQ_THD;                     // H active detect control How many continue legal line as
                                                                      // valid

    typedef UReg<0x05, 0x2D, 0, 8> SP_VSYNC_TGL_THD;                  // V active detect control V sync toggle times threshold

    typedef UReg<0x05, 0x2E, 0, 8> SP_SYNC_WIDTH_DTHD;                // V active detect control V sync pulse width threshod

    typedef UReg<0x05, 0x2F, 0, 8> SP_V_PRD_EQ_THD;                   // V active detect control How many continue legal v sync as
                                                                      // valid

    typedef UReg<0x05, 0x31, 0, 8> SP_VT_DLT_REG;                     // v active detect control V total different threshold

    typedef UReg<0x05, 0x32, 0, 1> SP_VSIN_INV_REG;                   // V active detect control Input v sync invert to v active
                                                                      // detect

    typedef UReg<0x05, 0x33, 0, 8> SP_H_TIMER_VAL;                    // Timer value control H timer value for h detect

    typedef UReg<0x05, 0x34, 0, 8> SP_V_TIMER_VAL;                    // Timer value control V timer for V detect

    typedef UReg<0x05, 0x35, 0, 12> SP_DLT_REG;                       // Sync separation control Sync pulse width difference
                                                                      // threshold

    typedef UReg<0x05, 0x37, 0, 8> SP_H_PULSE_IGNOR;                  // Sync separation control H pulse less than this value will
                                                                      // be ignore this counter is start when sync large different

    typedef UReg<0x05, 0x38, 0, 8> SP_PRE_COAST;                      // Sync separation control Set the coast will valid before
                                                                      // vertical sync (line number)

    typedef UReg<0x05, 0x39, 0, 8> SP_POST_COAST;                     // Sync separation control When line cnt reach this value
                                                                      // coast goes down

    typedef UReg<0x05, 0x3A, 0, 8> SP_H_TOTAL_EQ_THD;                 // Sync separation control How many regular line regard it
                                                                      // as legal

    typedef UReg<0x05, 0x3B, 0, 3> SP_SDCS_VSST_REG_H;                // Sync separation control

    typedef UReg<0x05, 0x3B, 4, 3> SP_SDCS_VSSP_REG_H;                // High bit of SD vs. stop position

    typedef UReg<0x05, 0x3E, 0, 8> SP_CS_0x3E;

    typedef UReg<0x05, 0x3E, 0, 1> SP_CS_P_SWAP;                      // Sync separation control cs_p_swap cs edge reference
                                                                      // select default rising edge

    typedef UReg<0x05, 0x3E, 1, 1> SP_HD_MODE;                        // hd_mode 1: HD mode 0: SD mode

    typedef UReg<0x05, 0x3E, 2, 1> SP_H_COAST;                        // h_coast 1: with sub coast out

    typedef UReg<0x05, 0x3E, 3, 1> SP_CS_INV_REG;                     // cs_inv_reg cs input invert

    typedef UReg<0x05, 0x3E, 4, 1> SP_H_PROTECT;                      // H count overflow protect

    typedef UReg<0x05, 0x3E, 5, 1> SP_DIS_SUB_COAST;                  // Disable sub coast

    typedef UReg<0x05, 0x3F, 0, 8> SP_SDCS_VSST_REG_L;                // Sync separation control SD vs. start position

    typedef UReg<0x05, 0x40, 0, 8> SP_SDCS_VSSP_REG_L;                // Sync separation control SD vs. stop position

    typedef UReg<0x05, 0x41, 0, 12> SP_CS_CLP_ST;                     // Sync separation control SOG clamp start position

    typedef UReg<0x05, 0x43, 0, 12> SP_CS_CLP_SP;                     // Sync separation control SOG clamp stop position

    typedef UReg<0x05, 0x45, 0, 12> SP_CS_HS_ST;                      // Sync separation control If the horizontal period number
                                                                      // is equal to the defined value, in XGA modes, It’s XGA
                                                                      // 75Hz mode

    typedef UReg<0x05, 0x47, 0, 12> SP_CS_HS_SP;                      // Sync separation control SOG hs stop position

    typedef UReg<0x05, 0x49, 0, 12> SP_RT_HS_ST;                      // Retiming control Retiming hs start position

    typedef UReg<0x05, 0x4B, 0, 12> SP_RT_HS_SP;                      // Retiming control Retiming hs stop postion

    typedef UReg<0x05, 0x4D, 0, 12> SP_H_CST_ST;                      // Retiming control H coast start position (total-this
                                                                      // value)

    typedef UReg<0x05, 0x4F, 0, 12> SP_H_CST_SP;                      // Retiming control H coast stop position

    typedef UReg<0x05, 0x51, 0, 12> SP_RT_VS_ST;                      // Retiming control Retiming vs start position

    typedef UReg<0x05, 0x53, 0, 12> SP_RT_VS_SP;                      // Retiming control Retiming vs stop position

    typedef UReg<0x05, 0x55, 0, 3> SP_HS_EP_DLY_SEL;                  // Retiming control Hs pulse delay sel for ( sync with vs )

    typedef UReg<0x05, 0x55, 3, 1> SP_HS_INV_REG;                     // Retiming control hs_inv_reg inver hs to retimming module

    typedef UReg<0x05, 0x55, 4, 1> SP_HS_POL_ATO;                     // Retiming control hs auto correct in retiming module

    typedef UReg<0x05, 0x55, 5, 1> SP_VS_INV_REG;                     // Retiming control vs inv_reg invert hs to retiming module

    typedef UReg<0x05, 0x55, 6, 1> SP_VS_POL_ATO;                     // Retiming control vs auto correct in retiming module

    typedef UReg<0x05, 0x55, 7, 1> SP_HCST_AUTO_EN;                   // Retiming control If enable h coast will start at ( V
                                                                      // total - hcst_st)

    typedef UReg<0x05, 0x56, 0, 8> SP_5_56;

    typedef UReg<0x05, 0x56, 0, 1> SP_SOG_MODE;                       // Out control 1: SOG mode; 0: normal mode

    typedef UReg<0x05, 0x56, 1, 1> SP_HS2PLL_INV_REG;                 // Out control When =1, HS to PLL invert

    typedef UReg<0x05, 0x56, 2, 1> SP_CLAMP_MANUAL;                   // Out control 1: clamp turn on off by control by software
                                                                      // (default)

    typedef UReg<0x05, 0x56, 3, 1> SP_CLP_SRC_SEL;                    // 0: for test Out control Clamp source select 1: pixel
                                                                      // clock generate 0: 27Mhz clock generate Out control

    typedef UReg<0x05, 0x56, 4, 1> SP_SYNC_BYPS;                      // External sync bypass to decimator

    typedef UReg<0x05, 0x56, 5, 1> SP_HS_PROC_INV_REG;                // Out control HS to decimator invert

    typedef UReg<0x05, 0x56, 6, 1> SP_VS_PROC_INV_REG;                // Out control VS to decimator invert

    typedef UReg<0x05, 0x56, 7, 1> SP_CLAMP_INV_REG;                  // Out control Clamp to ADC invert

    typedef UReg<0x05, 0x57, 0, 8> SP_5_57;

    typedef UReg<0x05, 0x57, 0, 1> SP_NO_CLAMP_REG;                   // Out control Clamp always be 0

    typedef UReg<0x05, 0x57, 1, 1> SP_COAST_INV_REG;                  // Out control Coast invert

    typedef UReg<0x05, 0x57, 2, 1> SP_NO_COAST_REG;                   // Out control Coast always be REG S5_57[3]

    typedef UReg<0x05, 0x57, 3, 1> SP_COAST_VALUE_REG;                // Out control Coast use 1x clk generate

    typedef UReg<0x05, 0x57, 6, 1> SP_HS_LOOP_SEL;                    // Bypass PLL HS to 57 core

    typedef UReg<0x05, 0x57, 7, 1> SP_HS_REG;                         // Out control When sub_coast enable will select this value

    typedef UReg<0x05, 0x58, 0, 12> SP_HT_DIFF_REG;                   // Auto clamp control H total difference less this value as
                                                                      // valid for auto clamp enable control

    typedef UReg<0x05, 0x5A, 0, 11> SP_VT_DIFF_REG;                   // Auto clamp control V total difference less this value as
                                                                      // valid for auto clamp enable control

    typedef UReg<0x05, 0x5C, 0, 8> SP_STBLE_CNT_REG;                  // Auto clamp control Stable indicate frame threshold for
                                                                      // auto clamp enable control

    typedef UReg<0x05, 0x63, 0, 1> SP_TEST_EN;                        // Test control Test bus enable

    typedef UReg<0x05, 0x63, 1, 3> SP_TEST_MODULE;                    // Test control test module select # 0 none # 1 hs_pol_det
                                                                      // module # 2 hs_act_det module # 3 vs_pol_det module # 4
                                                                      // vs_act_det module # 5 cs_sep module # 6 retiming module #
                                                                      // 7 out proc module

    typedef UReg<0x05, 0x63, 4, 3> SP_TEST_SIGNAL_SEL;                // Test control Test signal select

    // Every static register of this subsystem, in address order.
    static void init();

    // The sync path the source arrives on. Everything here is a consequence of
    // that one choice, and it is made by sourceHasOwnVsync() rather than read
    // off the chip -- docs/sync-type-selection.md, because STATUS_SYNC_PROC_VSACT
    // only reports correctly once the type is already right.
    static void applyForSyncType(bool csync);

    // The SD vertical sync positions, each ONE value across two registers: a
    // low byte and a three-bit high field in a different address. Written as
    // halves they drift -- a path setting only the low byte leaves whatever a
    // previous, larger value put in the high field.
    static void writeSdVsyncStart(uint16_t start);
    static void writeSdVsyncStop(uint16_t stop);

    // Where hsync retiming stops, in ADC samples. SourceMeasurement decides the
    // value off the divider; this block is where the register lives.
    static void writeRetimeStop(uint16_t samples);
};

}  // namespace Tv5725

#endif  // TV5725_SYNC_PROCESSOR_H
