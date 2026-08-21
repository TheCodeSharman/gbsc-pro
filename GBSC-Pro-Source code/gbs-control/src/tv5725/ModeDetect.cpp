#include "ModeDetect.h"

namespace Tv5725 {

void ModeDetect::init()
{
    MD_HPERIOD_LOCK_VALUE::write(22);           // s1_60[4:0]
    MD_HPERIOD_UNLOCK_VALUE::write(5);          // s1_60[7:5]
    MD_VPERIOD_LOCK_VALUE::write(4);            // s1_61[4:0]
    MD_VPERIOD_UNLOCK_VALUE::write(4);          // s1_61[7:5]
    MD_NTSC_INT_CNTRL::write(32);               // s1_62[5:0]
    MD_WEN_CNTRL::write(1);                     // s1_62[7:6]
    MD_PAL_INT_CNTRL::write(38);                // s1_63[5:0]
    MD_HS_FLIP::write(0);                       // s1_63[6:6]
    MD_VS_FLIP::write(0);                       // s1_63[7:7]
    MD_NTSC_PRG_CNTRL::write(65);               // s1_64[6:0]
    MD_VGA_CNTRL::write(62);                    // s1_65[6:0]
    MD_SEL_VGA60::write(0);                     // s1_65[7:7]
    MD_VGA_75HZ_CNTRL::write(178);              // s1_66[7:0]
    MD_VGA_85HZ_CNTRL::write(154);              // s1_67[7:0]
    MD_V1250_VCNTRL::write(78);                 // s1_68[6:0]
    MD_V1250_HCNTRL::write(214);                // s1_69[7:0]
    MD_SVGA_60HZ_CNTRL::write(177);             // s1_6a[7:0]
    MD_SVGA_75HZ_CNTRL::write(142);             // s1_6b[7:0]
    MD_SVGA_85HZ_CNTRL::write(124);             // s1_6c[7:0]
    MD_XGA_CNTRL::write(99);                    // s1_6d[6:0]
    MD_XGA_60HZ_CNTRL::write(139);              // s1_6e[7:0]
    MD_XGA_70HZ_CNTRL::write(118);              // s1_6f[6:0]
    MD_XGA_75HZ_CNTRL::write(112);              // s1_70[6:0]
    MD_XGA_85HZ_CNTRL::write(98);               // s1_71[6:0]
    MD_SXGA_CNTRL::write(133);                  // s1_72[7:0]
    MD_SXGA_60HZ_CNTRL::write(105);             // s1_73[6:0]
    MD_SXGA_75HZ_CNTRL::write(83);              // s1_74[6:0]
    MD_SXGA_85HZ_CNTRL::write(72);              // s1_75[6:0]
    MD_HD720P_CNTRL::write(93);                 // s1_76[6:0]
    MD_HD720P_60HZ_CNTRL::write(148);           // s1_77[7:0]
    MD_HD720P_50HZ_CNTRL::write(178);           // s1_78[7:0]
    MD_HD1125I_CNTRL::write(70);                // s1_79[6:0]
    MD_HD2200_1125I_CNTRL::write(198);          // s1_7a[7:0]
    MD_HD2640_1125I_CNTRL::write(238);          // s1_7b[7:0]
    MD_HD1125P_CNTRL::write(140);               // s1_7c[7:0]
    MD_HD2200_1125P_CNTRL::write(98);           // s1_7d[6:0]
    MD_HD2640_1125P_CNTRL::write(118);          // s1_7e[6:0]
    MD_HD1250P_CNTRL::write(44);                // s1_7f[7:0]
    MD_USER_DEF_VCNTRL::write(255);             // s1_80[7:0]
    MD_USER_DEF_HCNTRL::write(255);             // s1_81[7:0]
    MD_NOSYNC_DET_EN::write(1);                 // s1_82[0:0]
    MD_NOSYNC_USER_ID::write(0);                // s1_82[1:1]
    MD_SW_DET_EN::write(1);                     // s1_82[2:2]
    MD_SW_USER_ID::write(0);                    // s1_82[3:3]
    MD_TIMER_DET_EN_H::write(0);                // s1_82[4:4]
    MD_TIMER_DET_EN_V::write(0);                // s1_82[5:5]
    MD_DET_BYPS_H::write(0);                    // s1_82[6:6]
    MD_H_USER_ID::write(0);                     // s1_82[7:7]
    MD_DET_BYPS_V::write(0);                    // s1_83[0:0]
    MD_V_USER_ID::write(0);                     // s1_83[1:1]
    MD_UNSTABLE_LOCK_VALUE::write(3);           // s1_83[5:2]
}

void ModeDetect::applySyncType(SyncType type)
{
    MD_SEL_VGA60::write(type == Csync ? 0 : 1);
}

void ModeDetect::applyMedResLineCount(uint8_t lines)
{
    MD_HD1250P_CNTRL::write(lines);
}

}  // namespace Tv5725
