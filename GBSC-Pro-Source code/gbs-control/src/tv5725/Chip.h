#ifndef TV5725_CHIP_H
#define TV5725_CHIP_H

#include "Tv5725.h"

namespace Tv5725 {

// The chip itself: block resets, analog pads, DAC routing and the output clock
// enables. Segment 0's housekeeping -- the parts that belong to no one video
// block, and so to none of the other subsystem classes.
//
// The six SFTRST_*_RSTZ fields are why this runs first. Written 1 they RELEASE
// the deinterlacer, memory, memory FIFO, general FIFO, OSD and interrupt blocks
// -- the opposite of what the names suggest -- and a block released after it is
// configured discards that configuration. test_bringup.cpp asserts the ordering,
// since nothing inside one class can see a constraint between two.
class Chip {
public:
    typedef UReg<0x00, 0x44, 0, 1> DAC_RGBS_PWDNZ;                    // DAC enable When = 0, DAC (R,G,B,S) in power down mode

    typedef UReg<0x00, 0x44, 1, 1> DAC_RGBS_RPD;                      // When = 1, DAC (R,G,B,S) is enable RPD, RDAC power down
                                                                      // control When = 0, RDAC work normally When = 1, RDAC is in
                                                                      // power down mode

    typedef UReg<0x00, 0x44, 2, 1> DAC_RGBS_R0ENZ;                    // R0ENZ, DAC min output bypass When = 0, RDAC output Min
                                                                      // voltage

    typedef UReg<0x00, 0x44, 3, 1> DAC_RGBS_R1EN;                     // When = 1, RDAC output follow input R data R1EN, RDAC max
                                                                      // output control When = 0, RDAC output follow input R data
                                                                      // When = 1, RDAC output Max voltage

    typedef UReg<0x00, 0x44, 4, 1> DAC_RGBS_GPD;                      // GPD, GDAC power down control When = 0, GDAC work normally

    typedef UReg<0x00, 0x44, 5, 1> DAC_RGBS_G0ENZ;                    // When = 1, GDAC is in power down mode G0ENZ, GDAC min
                                                                      // output bypass When = 0, GDAC output Min voltage When = 1,
                                                                      // GDAC output follow input G data

    typedef UReg<0x00, 0x44, 6, 1> DAC_RGBS_G1EN;                     // G1EN, GDAC max output control When = 0, GDAC output
                                                                      // follow input G data

    typedef UReg<0x00, 0x44, 7, 1> DAC_RGBS_BPD;                      // When = 1, GDAC output Max voltage BPD, BDAC power down
                                                                      // control When = 0, BDAC work normally When = 1, BDAC is in
                                                                      // power down mode

    typedef UReg<0x00, 0x45, 0, 1> DAC_RGBS_B0ENZ;                    // B0ENZ, BDAC min output bypass When = 0, BDAC output Min
                                                                      // voltage

    typedef UReg<0x00, 0x45, 1, 1> DAC_RGBS_B1EN;                     // When = 1, BDAC output follow input B data B1EN, BDAC max
                                                                      // output control When = 0, BDAC output follow input B data
                                                                      // When = 1, BDAC output Max voltage

    typedef UReg<0x00, 0x45, 2, 1> DAC_RGBS_SPD;                      // SPD, SDAC power down control When = 0, GDAC work normally

    typedef UReg<0x00, 0x45, 3, 1> DAC_RGBS_S0ENZ;                    // When = 1, GDAC is in power down mode S0ENZ, SDAC min
                                                                      // output bypass When = 0, SDAC output Min voltage When = 1,
                                                                      // SDAC output follow input S data

    typedef UReg<0x00, 0x45, 4, 1> DAC_RGBS_S1EN;                     // S1EN, SDAC max output control When = 0, SDAC output
                                                                      // follow input S data

    typedef UReg<0x00, 0x46, 0, 1> SFTRST_IF_RSTZ;                    // Input formatter reset control When = 0, input formatter
                                                                      // is in reset status

    typedef UReg<0x00, 0x46, 1, 1> SFTRST_DEINT_RSTZ;                 // When = 1, input formatter work normally Deint_madpt3
                                                                      // reset control When = 0, deint_madpt3 is in reset status
                                                                      // When = 1, deint_madpt3 work normally

    typedef UReg<0x00, 0x46, 2, 1> SFTRST_MEM_FF_RSTZ;                // Mem_ff (wff/rff/playback/capture) reset control When = 0,
                                                                      // mem_ff is in reset status

    typedef UReg<0x00, 0x46, 3, 1> SFTRST_MEM_RSTZ;                   // When = 1, mem_ff work normally Mem controller reset
                                                                      // control When = 0, mem controller is in reset status When
                                                                      // = 1, mem controller work normally

    typedef UReg<0x00, 0x46, 4, 1> SFTRST_FIFO_RSTZ;                  // FIFO reset control When = 0, all FIFO (FF64/FF512) is in
                                                                      // reset status

    typedef UReg<0x00, 0x46, 5, 1> SFTRST_OSD_RSTZ;                   // When = 1, all FIFO work normally OSD reset control When =
                                                                      // 0, OSD generator is in reset status When = 1, OSD
                                                                      // generator work normally

    typedef UReg<0x00, 0x46, 6, 1> SFTRST_VDS_RSTZ;                   // Vds_proc reset control When = 0, vds_proc is in reset
                                                                      // status

    typedef UReg<0x00, 0x47, 0, 1> SFTRST_DEC_RSTZ;                   // Decimation reset control When = 0, decimation is in reset
                                                                      // status

    typedef UReg<0x00, 0x47, 1, 1> SFTRST_MODE_RSTZ;                  // When = 1, decimation work normally Mode detection reset
                                                                      // control When = 0, mode detection is in reset status When
                                                                      // = 1, mode detection work normally

    typedef UReg<0x00, 0x47, 2, 1> SFTRST_SYNC_RSTZ;                  // Sync procesor reset control When = 0, sync processor is
                                                                      // in reset status

    typedef UReg<0x00, 0x47, 3, 1> SFTRST_HDBYPS_RSTZ;                // When = 1, sync processor work normally HD bypass channel
                                                                      // reset control When = 0, HD bypass is in reset status When
                                                                      // = 1, HD bypasswork normally

    typedef UReg<0x00, 0x47, 4, 1> SFTRST_INT_RSTZ;                   // Interrupt generator reset control When = 0, interrupt
                                                                      // generator is in reset status

    typedef UReg<0x00, 0x48, 0, 8> PAD_CONTROL_00_0x48;

    typedef UReg<0x00, 0x48, 0, 1> PAD_BOUT_EN;                       // VB_[7:0] output control When = 0, disable VB_[7:0]
                                                                      // (test_out_[7:0]) output

    typedef UReg<0x00, 0x48, 1, 1> PAD_BIN_ENZ;                       // When = 1, enable VB_[7:0] (test_out_[7:0]) output
                                                                      // VB_[7:0] input control When = 0, enable VB_[7:0] input
                                                                      // When = 1, disable VB_[7:0] input

    typedef UReg<0x00, 0x48, 2, 1> PAD_ROUT_EN;                       // VR_[7:0] output control When = 0, disable VR_[7:0]
                                                                      // (test_out_[15:8]) output

    typedef UReg<0x00, 0x48, 3, 1> PAD_RIN_ENZ;                       // When = 1, enable VR_[7:0] (test_out_[15:8]) output
                                                                      // VR_[7:0] input control When = 0, enable VR_[7:0] input
                                                                      // When = 1, disable VR_[7:0] input

    typedef UReg<0x00, 0x48, 4, 1> PAD_GOUT_EN;                       // VG_[7:0] output control When = 0, disable VG_[7:0]
                                                                      // (test_out_[23:16]) output

    typedef UReg<0x00, 0x48, 5, 1> PAD_GIN_ENZ;                       // When = 1, enable VG_[7:0] (test_out_[23:16]) output
                                                                      // VG_[7:0] input control When = 0, enable VG_[7:0] input
                                                                      // When = 1, disable VG_[7:0] input

    typedef UReg<0x00, 0x48, 6, 1> PAD_SYNC1_IN_ENZ;                  // H/V sync1 input control When = 0, enable H/V sync1 input
                                                                      // filter

    typedef UReg<0x00, 0x48, 7, 1> PAD_SYNC2_IN_ENZ;                  // When = 1, disable H/V sync1 input filter H/V sync2 input
                                                                      // control When = 0, enable H/V sync2 input filter When = 1,
                                                                      // disable H/V sync2 input filter

    typedef UReg<0x00, 0x49, 0, 8> PAD_CONTROL_01_0x49;

    typedef UReg<0x00, 0x49, 0, 1> PAD_CKIN_ENZ;                      // PCLKIN control When = 0, PCLKIN input enable

    typedef UReg<0x00, 0x49, 1, 1> PAD_CKOUT_ENZ;                     // When = 1, PCLKIN input disable CLKOUT control When = 0,
                                                                      // CLKOUT output enable When = 1, CLKOUT output disable

    typedef UReg<0x00, 0x49, 2, 1> PAD_SYNC_OUT_ENZ;                  // HSOUT/VSOUT control When = 0, HSOUT/VSOUT output enable

    typedef UReg<0x00, 0x49, 3, 1> PAD_BLK_OUT_ENZ;                   // When = 1, HSOUT/VSOUT output disable HBOUT/VBOUT control
                                                                      // When = 0, HBOUT/VBOUT output enable When = 1, HBOUT/VBOUT
                                                                      // output disable

    typedef UReg<0x00, 0x49, 4, 1> PAD_TRI_ENZ;                       // Tri-state gate control When = 0, enable output pad’s tri-
                                                                      // state gate

    typedef UReg<0x00, 0x49, 5, 1> PAD_PLDN_ENZ;                      // When = 1, disable output pad’s tri-state gate Pull-down
                                                                      // control When = 0, enable pad’s pull-down transistor When
                                                                      // = 1, disable pad’s pull-down transistor

    typedef UReg<0x00, 0x49, 6, 1> PAD_PLUP_ENZ;                      // Pull-up control When = 0, enable pad’s pull-up transistor

    typedef UReg<0x00, 0x4A, 0, 3> PAD_OSC_CNTRL;                     // OSC pad C2/C1/C0 control OSC pad C2/C1/C0 control

    typedef UReg<0x00, 0x4A, 3, 1> PAD_XTOUT_TTL;                     // OSC pad output control When = 0, enable OSC pad output by
                                                                      // schmitt

    typedef UReg<0x00, 0x4B, 0, 1> DAC_RGBS_BYPS_IREG;                // DAC input DFF control When = 0, DAC input DFF is falling
                                                                      // edge D-flipflop

    typedef UReg<0x00, 0x4B, 1, 1> DAC_RGBS_BYPS2DAC;                 // When = 1, bypass falling edge D-flipflop HD bypass to DAC
                                                                      // control When = 0, disable HD bypass channel to DAC When =
                                                                      // 1, enable HD bypass channel to DAC directly

    typedef UReg<0x00, 0x4B, 2, 1> DAC_RGBS_ADC2DAC;                  // ADC to DAC control When = 0, disable ADC (with
                                                                      // decimation) to DAC

    typedef UReg<0x00, 0x4F, 0, 1> DAC_RGBS_V4CLK_INVT;               // V4CLK invert control When = 0, V4CLK to DAC directly

    // Every static segment-0 register, in address order. Called from the
    // bring-up before any other subsystem, and before the engine solves the
    // raster and the windows.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_CHIP_H
