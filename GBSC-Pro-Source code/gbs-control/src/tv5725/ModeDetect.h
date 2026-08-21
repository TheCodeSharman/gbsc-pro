#ifndef TV5725_MODE_DETECT_H
#define TV5725_MODE_DETECT_H

#include "Tv5725.h"

#include <stdint.h>

namespace Tv5725 {

// Mode Detect: the block that measures the incoming line and vertical periods
// and names a video standard from them, reported in STATUS_00.
//
// Almost all of it is a threshold table -- one or more comparison values per
// standard the chip can recognise. The values have no derivation here and are
// carried as constants, per docs/chip-initialisation.md.
//
// Two of the fields init() establishes have runtime writers that override them:
// MD_SEL_VGA60 follows the sync type and MD_HD1250P_CNTRL the medium-resolution
// line count. getVideoMode() rewrites eleven of the thresholds too, dithered by
// random(-2,2) around a static it captures on its first read -- so what init()
// writes is the centre those wander around.
class ModeDetect {
public:

    typedef UReg<0x01, 0x60, 0, 5> MD_HPERIOD_LOCK_VALUE;             // Mode Detect Horizontal Period Lock Value
                                                                      // MD_HPERIOD_LOCK_VALU If the continuous stabled line
                                                                      // number is equal to the defined value, the E

    typedef UReg<0x01, 0x60, 5, 3> MD_HPERIOD_UNLOCK_VALUE;           // horizontal stable indicator will be high Mode Detect
                                                                      // Horizontal Period Unlock Value If the continuous unstable
                                                                      // line number is equal to the defined value, the horizontal
                                                                      // stable indicator will be low


    typedef UReg<0x01, 0x61, 0, 5> MD_VPERIOD_LOCK_VALUE;             // Mode Detect Vertical Period Lock Value
                                                                      // MD_VPERIOD_LOCK_VALU If the continuous stabled frame
                                                                      // number is equal to the defined value, the E

    typedef UReg<0x01, 0x61, 5, 3> MD_VPERIOD_UNLOCK_VALUE;           // vertical stable indicator will be high Mode Detect
                                                                      // Vertical Period Unlock Value If the continuous unstable
                                                                      // frame number is equal to the defined value, the vertical
                                                                      // stable indicator will be low


    typedef UReg<0x01, 0x62, 0, 6> MD_NTSC_INT_CNTRL;                 // NTSC Interlace Mode Detect Value If the vertical period
                                                                      // number is equal to the defined value, This mode is NTSC

    typedef UReg<0x01, 0x62, 6, 2> MD_WEN_CNTRL;                      // Interlace mode Horizontal Stable Estimation Error Range
                                                                      // Control The continuous line is stable in the defined
                                                                      // error range. Range Table: MD_WEN_CNTRL [1:0] Error Range
                                                                      // 0 0 1 0 1 2 1 0 3 1 1 4


    typedef UReg<0x01, 0x63, 0, 6> MD_PAL_INT_CNTRL;                  // PAL Interlace Mode Detect Value If the vertical period
                                                                      // number is equal to the defined value, This mode is PAL

    typedef UReg<0x01, 0x63, 6, 1> MD_HS_FLIP;                        // interlace mode Input Horizontal sync polarity Control
                                                                      // When set it to 1, the input horizontal sync will be
                                                                      // inverted. Input Vertical sync polarity Control

    typedef UReg<0x01, 0x63, 7, 1> MD_VS_FLIP;                        // When set it to 1, the input vertical sync will be
                                                                      // inverted


    typedef UReg<0x01, 0x64, 0, 7> MD_NTSC_PRG_CNTRL;                 // NTSC Progressive Mode Detect Value If the vertical period
                                                                      // number is equal to the defined value, This mode is NTSC


    typedef UReg<0x01, 0x65, 0, 7> MD_VGA_CNTRL;                      // VGA Mode Vertical Detect Value If the vertical period
                                                                      // number is equal to the defined value, this mode is VGA

    typedef UReg<0x01, 0x65, 7, 1> MD_SEL_VGA60;                      // mode, except VGA 60HZ mode. Select VGA 60HZ mode Program
                                                                      // this bit to distinguish between VGA 60Hz mode and NTSC
                                                                      // progressive mode; When set to 1, select VGA 60Hz mode
                                                                      // When set to 0, select NTSC progressive mode


    typedef UReg<0x01, 0x66, 0, 8> MD_VGA_75HZ_CNTRL;                 // VGA 75Hz Horizontal Detect Value If the horizontal period
                                                                      // number is equal to the defined value, in VGA mode, this
                                                                      // mode Is VGA 75Hz mode


    typedef UReg<0x01, 0x67, 0, 8> MD_VGA_85HZ_CNTRL;                 // VGA 85Hz Horizontal Detect Value If the horizontal period
                                                                      // number is equal to the defined value, in VGA mode, this
                                                                      // mode Is VGA 85Hz mode


    typedef UReg<0x01, 0x68, 0, 7> MD_V1250_VCNTRL;                   // Vertical 1250 Line Mode Vertical Detect Value


    typedef UReg<0x01, 0x69, 0, 8> MD_V1250_HCNTRL;                   // Vertical 1250 Line Mode Horizontal Detect Value Vertical
                                                                      // 1250 lines, horizontal 866 pixels mode detect value


    typedef UReg<0x01, 0x6A, 0, 8> MD_SVGA_60HZ_CNTRL;                // SVGA 60HZ Mode Horizontal Detect Value If the horizontal
                                                                      // period number is equal to the defined value, in SVGA
                                                                      // mode, it’s SVGA 60Hz mode


    typedef UReg<0x01, 0x6B, 0, 8> MD_SVGA_75HZ_CNTRL;                // SVGA 75HZ Mode Horizontal Detect Value If the horizontal
                                                                      // period number is equal to the defined value, in SVGA
                                                                      // mode, it’s SVGA 75Hz mode


    typedef UReg<0x01, 0x6C, 0, 8> MD_SVGA_85HZ_CNTRL;                // SVGA 85HZ Mode Horizontal Detect Value If the horizontal
                                                                      // period number is equal to the defined value, in SVGA
                                                                      // mode, it’s SVGA 85Hz mode


    typedef UReg<0x01, 0x6D, 0, 7> MD_XGA_CNTRL;                      // XGA Mode Vertical Detect Value


    typedef UReg<0x01, 0x6E, 0, 8> MD_XGA_60HZ_CNTRL;                 // XGA 60Hz Mode Horizontal Detect Value If the horizontal
                                                                      // period number is equal to the defined value, in XGA
                                                                      // modes, It’s XGA 60Hz mode


    typedef UReg<0x01, 0x6F, 0, 7> MD_XGA_70HZ_CNTRL;                 // XGA 70Hz Mode Horizontal Detect Value If the horizontal
                                                                      // period number is equal to the defined value, in XGA modes


    typedef UReg<0x01, 0x70, 0, 7> MD_XGA_75HZ_CNTRL;                 // XGA 75Hz Mode Horizontal Detect Value If the horizontal
                                                                      // period number is equal to the defined value, in XGA modes


    typedef UReg<0x01, 0x71, 0, 7> MD_XGA_85HZ_CNTRL;                 // XGA 85Hz Mode Horizontal Detect Value If the horizontal
                                                                      // period number is equal to the defined value, in XGA modes


    typedef UReg<0x01, 0x72, 0, 8> MD_SXGA_CNTRL;                     // SXGA Mode Vertical Detect Value If the vertical period
                                                                      // number is equal to the defined value, It’s SXGA mode


    typedef UReg<0x01, 0x73, 0, 7> MD_SXGA_60HZ_CNTRL;                // SXGA 60Hz Mode Horizontal Detect Value If the horizontal
                                                                      // period number is equal to the defined value, in SXGA
                                                                      // modes


    typedef UReg<0x01, 0x74, 0, 7> MD_SXGA_75HZ_CNTRL;                // SXGA 75Hz Mode Horizontal Detect Value If the horizontal
                                                                      // period number is equal to the defined value, in SXGA
                                                                      // modes


    typedef UReg<0x01, 0x75, 0, 7> MD_SXGA_85HZ_CNTRL;                // SXGA 85Hz Mode Horizontal Detect Value If the horizontal
                                                                      // period number is equal to the defined value, in SXGA
                                                                      // modes


    typedef UReg<0x01, 0x76, 0, 7> MD_HD720P_CNTRL;                   // HD720P Vertical Detect Value


    typedef UReg<0x01, 0x77, 0, 8> MD_HD720P_60HZ_CNTRL;              // HD720P 60Hz Mode Horizontal Detect Value If the
                                                                      // horizontal period number is equal to the defined value,
                                                                      // in HD720P mode. It is HD720P 60Hz mode


    typedef UReg<0x01, 0x78, 0, 8> MD_HD720P_50HZ_CNTRL;              // HD720P 50Hz Mode Horizontal Detect Value If the
                                                                      // horizontal period number is equal to the defined value,
                                                                      // in HD720P mode. It is HD720P 50Hz mode


    typedef UReg<0x01, 0x79, 0, 7> MD_HD1125I_CNTRL;                  // 1080I Mode 1125 Line Vertical Detect Value


    typedef UReg<0x01, 0x7A, 0, 8> MD_HD2200_1125I_CNTRL;             // 1080I Mode 2200x1125I Horizontal Detect Value If the
                                                                      // horizontal period number is equal to the defined value,
                                                                      // in 1080I mode. It is HD2200x1125I mode


    typedef UReg<0x01, 0x7B, 0, 8> MD_HD2640_1125I_CNTRL;             // 1080I Mode 2640x1125I Horizontal Detect Value If the
                                                                      // horizontal period number is equal to the defined value,
                                                                      // in 1080I mode. It is HD2640x1125I mode


    typedef UReg<0x01, 0x7C, 0, 8> MD_HD1125P_CNTRL;                  // 1080P Mode 1125 Line Vertical Detect Value If the
                                                                      // vertical period number is equal to the defined value, It
                                                                      // is HD1125P mode


    typedef UReg<0x01, 0x7D, 0, 7> MD_HD2200_1125P_CNTRL;             // 1080P Mode 2200x1125P Horizontal Detect Value If the
                                                                      // horizontal period number is equal to the defined value,
                                                                      // in 1080P mode


    typedef UReg<0x01, 0x7E, 0, 7> MD_HD2640_1125P_CNTRL;             // 1080P Mode 2640x1125P Horizontal Detect Value If the
                                                                      // horizontal period number is equal to the defined value,
                                                                      // in 1080P mode


    typedef UReg<0x01, 0x7F, 0, 8> MD_HD1250P_CNTRL;                  // 1080P Mode 2376x1250P Vertical Detect Value If the
                                                                      // vertical period number is equal to the defined value, It
                                                                      // is HD2376x1250P mode


    typedef UReg<0x01, 0x80, 0, 8> MD_USER_DEF_VCNTRL;                // User Defined Mode Vertical Detect Value If the vertical
                                                                      // period number is equal to the defined value, It is user-
                                                                      // defined mode


    typedef UReg<0x01, 0x81, 0, 8> MD_USER_DEF_HCNTRL;                // User Defined Mode Horizontal Detect Value If the
                                                                      // horizontal period number is equal to the defined value,
                                                                      // It is user-defined mode


    typedef UReg<0x01, 0x82, 0, 1> MD_NOSYNC_DET_EN;                  // Sync Connection Detect Enable Detect the horizontal sync
                                                                      // signal if connect or not. 0: user mode 1: auto detect

    typedef UReg<0x01, 0x82, 1, 1> MD_NOSYNC_USER_ID;                 // Sync Connection Detect User Defined ID User defined
                                                                      // indicator in user mode. 0: sync connected. 1: no sync
                                                                      // connected

    typedef UReg<0x01, 0x82, 2, 1> MD_SW_DET_EN;                      // Mode Switch Detect Enable Enable bit of auto detect if
                                                                      // the mode changed or not. 0: user mode 1: auto detect

    typedef UReg<0x01, 0x82, 3, 1> MD_SW_USER_ID;                     // Mode Switch Detect User Defined ID User defined indicator
                                                                      // in user mode. 0->1: mode changed. 1->0: mode changed

    typedef UReg<0x01, 0x82, 4, 1> MD_TIMER_DET_EN_H;                 // Horizontal Unstable Estimation Timer Detect Enable Enable
                                                                      // the timer detect result in horizontal unstable
                                                                      // estimation. 0: use the hstable indicator in hperiod
                                                                      // detect. 1: use the timer detected unstable indicator

    typedef UReg<0x01, 0x82, 5, 1> MD_TIMER_DET_EN_V;                 // Vertical Unstable Estimation Timer Detect Enable Enable
                                                                      // the timer detect result in vertical unstable estimation.
                                                                      // 0: use the vstable indicator in vperiod detect. 1: use
                                                                      // the timer detected unstable indicator

    typedef UReg<0x01, 0x82, 6, 1> MD_DET_BYPS_H;                     // Horizontal Unstable Estimation Bypass Control Bypass the
                                                                      // horizontal unstable estimation 0: auto mode 1: user mode

    typedef UReg<0x01, 0x82, 7, 1> MD_H_USER_ID;                      // Horizontal Unstable Estimation User Defined ID User
                                                                      // defined indicator in user mode. 0: stable 1: unstable


    typedef UReg<0x01, 0x83, 0, 1> MD_DET_BYPS_V;                     // Vertical Unstable Estimation Bypass Control Bypass the
                                                                      // vertical unstable estimation auto detect 0: auto mode 1:
                                                                      // user mode

    typedef UReg<0x01, 0x83, 1, 1> MD_V_USER_ID;                      // Vertical Unstable Estimation User Defined ID User defined
                                                                      // indicator in user mode. 0: stable 1: unstable

    typedef UReg<0x01, 0x83, 2, 4> MD_UNSTABLE_LOCK_VALUE;            // Unstable Estimation Lock Value If the internal counter
                                                                      // equals the defined value, the unstable indicator will be
                                                                      // high. Horizontal and vertical estimation shared this
                                                                      // value

    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_MODE_DETECT_H
