#include "VideoProcessor.h"

namespace Tv5725 {

void VideoProcessor::setLineFilter(bool wanted)
{
    VDS_D_RAM_BYPS::write(wanted ? 0 : 1);
}

void VideoProcessor::setPeaking(bool wanted)
{
    VDS_PK_Y_H_BYPS::write(wanted ? 0 : 1);
}

void VideoProcessor::setStepResponse(bool wanted)
{
    VDS_UV_STEP_BYPS::write(wanted ? 0 : 1);
}

void VideoProcessor::setSixTapFilter(bool wanted)
{
    VDS_TAP6_BYPS::write(wanted ? 0 : 1);
}

void VideoProcessor::init()
{
    VDS_FIELDAB_EN::write(0x1);                  // s3_00[1:1]
    VDS_DFIELD_EN::write(0x0);                   // s3_00[2:2]
    VDS_FIELD_FLIP::write(0x0);                  // s3_00[3:3]
    VDS_HALF_EN::write(0x0);                     // s3_00[6:6]
    VDS_SRESET::write(0x0);                      // s3_00[7:7]
    VDS_FREERUN_FID::write(0x0);                 // s3_1a[5:5]
    VDS_FID_AA_DLY::write(0x0);                  // s3_1a[6:6]
    VDS_FID_RST::write(0x0);                     // s3_1a[7:7]
    VDS_DIF_FR_SEL_EN::write(0x0);               // s3_1f[4:4]
    VDS_EN_FR_NUM_RST::write(0x0);               // s3_1f[5:5]
    VDS_UV_FLIP::write(0x0);                     // s3_24[0:0]
    VDS_U_DELAY::write(0x0);                     // s3_24[1:1]

    // Only the progressive standards write this, so without a value here a
    // source leaving one of them keeps theirs.
    VDS_V_DELAY::write(0x0);                     // s3_24[2:2]

    // Nothing else writes 2: SourceStandard writes 3, for YPbPr and the
    // progressive standards, and still overrides this -- a standard is applied
    // during the load and a bring-up only at an arm.
    VDS_Y_DELAY::write(0x2);                     // s3_24[5:4]
    VDS_WEN_DELAY::write(0x2);                   // s3_24[7:6]
    VDS_D_SP::write(0x3);                        // s3_25[9:0]
    VDS_BLEV_AUTO_EN::write(0x0);                // s3_26[7:7]
    VDS_USER_MIN::write(0xF);                    // s3_27[3:0]
    VDS_USER_MAX::write(0xC);                    // s3_27[7:4]
    VDS_BLEV_LEVEL::write(0x26);                 // s3_28[7:0]
    VDS_BLEV_GAIN::write(0x7);                   // s3_29[7:0]
    VDS_BLEV_BYPS::write(0x1);                   // s3_2a[0:0]
    VDS_STEP_DLY_CNTRL::write(0x1);              // s3_2a[5:4]
    VDS_STEP_CLIP::write(0x1);                   // s3_2b[6:4]
    VDS_SK_U_CENTER::write(0xE0);                // s3_2c[7:0]
    VDS_SK_V_CENTER::write(0x2F);                // s3_2d[7:0]
    VDS_SK_Y_LOW_TH::write(0x20);                // s3_2e[7:0]
    VDS_SK_Y_HIGH_TH::write(0xF0);               // s3_2f[7:0]
    VDS_SK_RANGE::write(0x40);                   // s3_30[7:0]
    VDS_SK_GAIN::write(0xA);                     // s3_31[3:0]
    VDS_SK_Y_EN::write(0x1);                     // s3_31[4:4]
    VDS_SK_BYPS::write(0x1);                     // s3_31[5:5]
    VDS_SVM_BPF_CNTRL::write(0x0);               // s3_32[1:0]
    VDS_SVM_POL_FLIP::write(0x0);                // s3_32[2:2]
    VDS_SVM_2ND_BYPS::write(0x1);                // s3_32[3:3]
    VDS_SVM_VCLK_DELAY::write(0x0);              // s3_32[6:4]
    VDS_SVM_SIGMOID_BYPS::write(0x1);            // s3_32[7:7]
    VDS_SVM_GAIN::write(0x0);                    // s3_33[7:0]
    VDS_SVM_OFFSET::write(0x0);                  // s3_34[7:0]
    VDS_USIN_GAIN::write(0x0);                   // s3_38[7:0]
    VDS_VSIN_GAIN::write(0x0);                   // s3_39[7:0]
    VDS_SYNC_LEV::write(0x0);                    // s3_3d[8:0]
    VDS_CONVT_BYPS::write(0x0);                  // s3_3e[3:3]
    VDS_DYN_BYPS::write(0x0);                    // s3_3e[4:4]
    VDS_BLK_BF_EN::write(0x1);                   // s3_3e[7:7]
    VDS_UV_BLK_VAL::write(0x0);                  // s3_3f[7:0]
    VDS_1ST_INT_BYPS::write(0x1);                // s3_40[0:0]
    VDS_2ND_INT_BYPS::write(0x1);                // s3_40[1:1]
    VDS_SVM_V4CLK_DELAY::write(0x0);             // s3_40[5:4]
    VDS_PK_LINE_BUF_SP::write(0x3);              // s3_41[9:0]
    VDS_PK_RAM_BYPS::write(0x1);                 // s3_42[6:6]
    VDS_PK_VH_HL_SEL::write(0x1);                // s3_43[2:2]
    VDS_PK_VH_HH_SEL::write(0x1);                // s3_43[3:3]
    // The low band's shape, beside the high band's above. The GAINS are not
    // here: the scanlines and peaking controls write VDS_PK_LB_GAIN and
    // VDS_PK_LH_GAIN too, so a value set here would be whichever of them ran
    // last rather than this one.
    VDS_PK_VL_HL_SEL::write(0x0);                // s3_43[0:0]
    VDS_PK_VL_HH_SEL::write(0x0);                // s3_43[1:1]
    VDS_PK_LB_CORE::write(0x0);                  // s3_44[2:0]
    VDS_PK_LH_CORE::write(0x0);                  // s3_46[2:0]
    VDS_STEP_GAIN::write(0x1);                   // s3_2b[3:0]
    VDS_PK_LB_CMP::write(0x1F);                  // s3_44[7:3]
    VDS_PK_LH_CMP::write(0x1F);                  // s3_46[7:3]
    VDS_PK_HL_CORE::write(0x1);                  // s3_48[2:0]
    VDS_PK_HL_CMP::write(0x1F);                  // s3_48[7:3]
    VDS_PK_HL_GAIN::write(0x10);                 // s3_49[5:0]
    VDS_PK_HB_CORE::write(0x1);                  // s3_4a[2:0]
    VDS_PK_HB_CMP::write(0x1F);                  // s3_4a[7:3]
    VDS_PK_HB_GAIN::write(0x20);                 // s3_4b[5:0]
    VDS_PK_HH_CORE::write(0x1);                  // s3_4c[2:0]
    VDS_PK_HH_CMP::write(0x1F);                  // s3_4c[7:3]
    VDS_PK_HH_GAIN::write(0xA);                  // s3_4d[5:0]
    VDS_PK_Y_V_BYPS::write(0x1);                 // s3_4e[1:1]
    VDS_C_VPK_BYPS::write(0x1);                  // s3_4e[3:3]
    VDS_C_VPK_CORE::write(0x1);                  // s3_4e[6:4]
    VDS_C_VPK_GAIN::write(0x1E);                 // s3_4f[5:0]
    VDS_DO_UV_DEC_BYPS::write(0x1);              // s3_50[5:5]
    VDS_DO_UVSEL_FLIP::write(0x0);               // s3_50[6:6]
    VDS_DO_16B_EN::write(0x0);                   // s3_50[7:7]
    VDS_GLB_NOISE::write(0x0);                   // s3_51[10:0]
    VDS_NR_Y_BYPASS::write(0x1);                 // s3_52[4:4]
    VDS_NR_C_BYPASS::write(0x1);                 // s3_52[5:5]
    VDS_NR_DIF_LPF5_BYPS::write(0x1);            // s3_52[6:6]
    VDS_NR_MI_TH_EN::write(0x0);                 // s3_52[7:7]
    VDS_NR_MI_OFFSET::write(0x8);                // s3_53[6:0]
    VDS_NR_MIG_USER_EN::write(0x0);              // s3_53[7:7]
    VDS_NR_MI_GAIN::write(0x4);                  // s3_54[3:0]
    VDS_NR_STILL_GAIN::write(0x2);               // s3_54[7:4]
    VDS_NR_MI_THRESH::write(0xA);                // s3_55[3:0]
    VDS_NR_EN_H_NOISY::write(0x0);               // s3_55[4:4]
    VDS_NR_EN_GLB_STILL::write(0x0);             // s3_55[6:6]
    VDS_NR_GLB_STILL_MENU::write(0x0);           // s3_55[7:7]
    VDS_NR_NOISY_OFFSET::write(0xB);             // s3_56[6:0]

    // White level expansion, bypassed. Only the scanline toggles own these, so a
    // unit that never had scanlines on ran the expansion on whatever survived
    // from the mode before. The gain is inert while bypassed but is written
    // anyway: a pair left disagreeing traps whoever switches it on next.
    VDS_W_LEV_BYPS::write(0x1);                  // s3_56[7:7]
    VDS_W_LEV::write(0x0);                       // s3_57[7:0]
    VDS_WLEV_GAIN::write(0x1A);                  // s3_58[7:0]
    VDS_NS_U_CENTER::write(0x0);                 // s3_59[7:0]
    VDS_NS_V_CENTER::write(0x0);                 // s3_5a[7:0]
    VDS_NS_U_GAIN::write(0x1A);                  // s3_5b[6:0]
    VDS_NS_SQUARE_RAD::write(0x800);             // s3_5b[21:7]
    VDS_NS_Y_HIGH_TH::write(0xFF);               // s3_5d[13:6]
    VDS_NS_V_GAIN::write(0x10);                  // s3_5e[12:6]
    VDS_NS_Y_LOW_TH::write(0x0);                 // s3_5f[9:5]
    VDS_NS_BYPS::write(0x1);                     // s3_60[2:2]
    VDS_NS_Y_ACTIVE_EN::write(0x0);              // s3_60[3:3]
    VDS_C1_TAG_LOW_SLOPE::write(0x1B0);          // s3_60[13:4]
    VDS_C1_TAG_HIGH_SLOPE::write(0x202);         // s3_61[15:6]
    VDS_C1_GAIN::write(0x9);                     // s3_63[3:0]
    VDS_C1_U_LOW::write(0x90);                   // s3_63[11:4]
    VDS_C1_U_HIGH::write(0xFE);                  // s3_64[11:4]
    VDS_C1_BYPS::write(0x1);                     // s3_65[4:4]
    VDS_C1_Y_THRESH::write(0xFF);                // s3_65[12:5]
    VDS_C2_TAG_LOW_SLOPE::write(0x203);          // s3_66[14:5]
    VDS_C2_TAG_HIGH_SLOPE::write(0x3A4);         // s3_67[16:7]
    VDS_C2_GAIN::write(0x6);                     // s3_69[4:1]
    VDS_C2_U_LOW::write(0xC0);                   // s3_69[12:5]
    VDS_C2_U_HIGH::write(0xFE);                  // s3_6a[12:5]
    VDS_C2_BYPS::write(0x1);                     // s3_6b[5:5]
    VDS_C2_Y_THRESH::write(0xFF);                // s3_6b[13:6]
    VDS_SYNC_IN_SEL::write(0x0);                 // s3_72[7:7]
    VDS_BLUE_RANGE::write(0x4);                  // s3_73[2:0]
    VDS_BLUE_BYPS::write(0x1);                   // s3_73[3:3]
    VDS_BLUE_UGAIN::write(0xB);                  // s3_73[7:4]
    VDS_BLUE_VGAIN::write(0x5);                  // s3_74[3:0]
    VDS_BLUE_Y_LEV::write(0x0);                  // s3_74[7:4]
}

void VideoProcessor::applyFreeRunTiming()
{
    VDS_SYNC_EN::write(0);
    VDS_FLOCK_EN::write(0);
}

void VideoProcessor::applyFrameSequencing()
{
    VDS_FRAME_RST::write(4);
    VDS_FRAME_NO::write(1);
    VDS_FR_SELECT::write(1);
}

void VideoProcessor::clockInputOnFallingEdge()
{
    VDS_IN_DREG_BYPS::write(0);
}

}  // namespace Tv5725
