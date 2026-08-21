#ifndef TV5725_HD_BYPASS_H
#define TV5725_HD_BYPASS_H

#include "Tv5725.h"

namespace Tv5725 {

// The HD bypass block: the path video takes when the scaler is out of circuit,
// carrying its own raster generator and its own blanking straight to the DAC.
//
// init() is the resting state. setOutModeHdBypass() and bypassModeSwitch_RGBHV()
// overwrite the raster and both sync windows for the mode they are entering, so
// what this block establishes alone is the dynamic range, the DVI-mode
// blanking, and the programmed blank level.
//
// s1_56..s1_5f carry no datasheet field and have no writer here.
class HdBypass {
public:

    typedef UReg<0x01, 0x30, 0, 1> HD_IN_DREG_BYPS;                   // Input retiming bypass Use the falling or rising edge of
                                                                      // clock to get the input data. 0: Clock input data on the
                                                                      // falling edge 1: Clock input date on the rising edge

    typedef UReg<0x01, 0x30, 1, 1> HD_MATRIX_BYPS;                    // YUV2RGB conversion bypass control Available only when
                                                                      // input source is YUV source 0: YUV2RGB convert 1: bypass
                                                                      // YUV2RGB function

    typedef UReg<0x01, 0x30, 2, 1> HD_DYN_BYPS;                       // Dynamic range bypass control If the input is YUV data, it
                                                                      // must do dynamic range. 0: input is YUV data, do dynamic
                                                                      // range . 1: input is RGB data, bypass dynamic range

    typedef UReg<0x01, 0x30, 3, 1> HD_SEL_BLK_IN;                     // Blank select Choose the input blank or generated blank
                                                                      // use sync. 0: choose the blank that sync generated


    typedef UReg<0x01, 0x31, 0, 8> HD_Y_GAIN;                         // Dynamic range Y gain value The gain value of Y dynamic
                                                                      // range


    typedef UReg<0x01, 0x32, 0, 8> HD_Y_OFFSET;                       // Dynamic range Y offset value The offset value of Y
                                                                      // dynamic range


    typedef UReg<0x01, 0x33, 0, 8> HD_U_GAIN;                         // Dynamic range U gain value The gain value of U dynamic
                                                                      // range


    typedef UReg<0x01, 0x34, 0, 8> HD_U_OFFSET;                       // Dynamic range U offset value The offset value of U
                                                                      // dynamic range


    typedef UReg<0x01, 0x35, 0, 8> HD_V_GAIN;                         // Dynamic range V gain value The gain value of V dynamic
                                                                      // range


    typedef UReg<0x01, 0x36, 0, 8> HD_V_OFFSET;                       // Dynamic range V offset value The offset value of V
                                                                      // dynamic range


    typedef UReg<0x01, 0x37, 0, 11> HD_HSYNC_RST;                     // Horizontal reset value Horizontal counter reset value
                                                                      // [7:0]


    typedef UReg<0x01, 0x39, 0, 11> HD_INI_ST;                        // Horizontal reset pulse start position Vertical counter
                                                                      // write enable, adjust the distance between hblank and
                                                                      // vblank


    typedef UReg<0x01, 0x3B, 0, 12> HD_HB_ST;                         // Horizontal blank start position Generate horizontal blank
                                                                      // to select programmed data


    typedef UReg<0x01, 0x3D, 0, 12> HD_HB_SP;                         // Horizontal blank stop position Generate horizontal blank
                                                                      // to select programmed data


    typedef UReg<0x01, 0x3F, 0, 12> HD_HS_ST;                         // Horizontal sync start position Output sync to DAC start
                                                                      // position


    typedef UReg<0x01, 0x41, 0, 12> HD_HS_SP;                         // Horizontal sync stop position Output sync to DAC stop
                                                                      // position


    typedef UReg<0x01, 0x43, 0, 12> HD_VB_ST;                         // Vertical blank start position Generate blank to select
                                                                      // program data in blank


    typedef UReg<0x01, 0x45, 0, 12> HD_VB_SP;                         // Vertical blank stop position Generate blank to select
                                                                      // program data in blank


    typedef UReg<0x01, 0x47, 0, 12> HD_VS_ST;                         // Vertical sync start position Output vertical sync to DAC
                                                                      // start position


    typedef UReg<0x01, 0x49, 0, 12> HD_VS_SP;                         // Vertical sync stop position Output vertical sync to DAC
                                                                      // stop position


    typedef UReg<0x01, 0x4B, 0, 12> HD_EXT_VB_ST;                     // DVI mode vertical blank start position Output vertical
                                                                      // blank to DAC for DIV mode start position


    typedef UReg<0x01, 0x4D, 0, 12> HD_EXT_VB_SP;                     // DVI mode vertical blank stop position Output vertical
                                                                      // blank to DAC for DIV mode stop position


    typedef UReg<0x01, 0x4F, 0, 12> HD_EXT_HB_ST;                     // DVI mode horizontal blank start position Output
                                                                      // horizontal blank to DAC for DIV mode start position


    typedef UReg<0x01, 0x51, 0, 12> HD_EXT_HB_SP;                     // DVI mode horizontal blank start position Output
                                                                      // horizontal blank to DAC for DIV mode stop position


    typedef UReg<0x01, 0x53, 0, 8> HD_BLK_GY_DATA;                    // Programmed GY data in horizontal blank Force the blank of
                                                                      // GY data to the defined programmed data


    typedef UReg<0x01, 0x54, 0, 8> HD_BLK_BU_DATA;                    // Programmed BU data in horizontal blank Force the blank of
                                                                      // BU data to the defined programmed data


    typedef UReg<0x01, 0x55, 0, 8> HD_BLK_RV_DATA;

    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_HD_BYPASS_H
