#ifndef TV5725_DEINTERLACER_H
#define TV5725_DEINTERLACER_H

#include "Tv5725.h"

#include <stdint.h>

namespace Tv5725 {

// The motion-adaptive deinterlacer and the diagonal bob before it: the whole of
// segment 2. Almost all of it is filter coefficients, motion and noise
// thresholds and pull-down detection, carried as constants with no derivation,
// per docs/chip-initialisation.md.
//
// Four of its fields are bypass bits -- MADPT_PD_RAM_BYPS, MADPT_VIIR_BYPS,
// MADPT_Y_WOUT_BYPS and DIAG_BOB_PLDY_RAM_BYPS -- and left at 0 with every
// coefficient at 0 they route video THROUGH the deinterlacer RAM, filling the
// screen with random colour pixels while all 608 config registers still read
// correct. That is why this block has to run whether or not a table loaded.
class Deinterlacer {
public:

    // RD-5725-1.1 names s2_0d and s2_0e both MADPT_MI_THRESHOLD, for two
    // different functions -- the second is the motion index's fixed value. Its
    // name here comes from that function description, because a duplicate is
    // not a name.
    typedef UReg<0x02, 0x0E, 0, 7> MADPT_MI_FIXED_VALUE;               // Motion index fixed value


    typedef UReg<0x02, 0x00, 0, 8> DEINT_00;

    typedef UReg<0x02, 0x00, 0, 1> DIAG_BOB_MIN_BYPS;                 // Diagonal Function Bypass Control When set to 1, bypass
                                                                      // diagonal min selection for Y. No diagonal detection, just

    typedef UReg<0x02, 0x00, 1, 1> DIAG_BOB_COEF_SEL;                 // vertically two pixels average. Diagonal Bob Low pass
                                                                      // Filter Coefficient Selection Select coefficients for
                                                                      // pixel difference low pass filter DIAG_BOB_COEF_SEL
                                                                      // Internal Selected Coefficient 1 15/16 0 14/16

    typedef UReg<0x02, 0x00, 2, 1> DIAG_BOB_WEAVE_BYPS;               // Weave Function Bypass Control When set to 1, weave
                                                                      // function will bypass. Just repeat original data

    typedef UReg<0x02, 0x00, 3, 2> DIAG_BOB_DET_BYPS;                 // Diagonal Bob Deinterlacer Angle Detect Bypass Control
                                                                      // When set to 1, bypass the detection of angle arctan (1/4)

    typedef UReg<0x02, 0x00, 5, 1> DIAG_BOB_YTAP3_BYPS;               // Diagonal Bob Deinterlacer Y Tap3 Filter Bypass control
                                                                      // When set to 1, bypass the tap3 filter for Y

    typedef UReg<0x02, 0x00, 6, 1> DIAG_BOB_MIN_CBYPS;                // Diagonal Bob Min Control For UV When set to 1, bypass
                                                                      // diagonal min select for UV. No diagonal detection, just

    typedef UReg<0x02, 0x00, 7, 1> DIAG_BOB_PLDY_RAM_BYPS;            // vertically two pixels average. Bypass Control For
                                                                      // Pdelayer FIFO When set to 1, bypass FIFO for pdelayer


    typedef UReg<0x02, 0x01, 0, 9> DIAG_BOB_PLDY_SP;                  // The Distance Control of Pdelayer Reset [7:0] In pdelayer,
                                                                      // adjust the delay between read reset and write reset


    typedef UReg<0x02, 0x02, 5, 1> MADPT_SEL_22;                      // When set to 1, enable 2:2 pull-down detection

    typedef UReg<0x02, 0x02, 6, 1> MADPT_Y_VSCALE_BYPS;               // When set to 0, enable 3:2 pull-down detection. Bypass Y
                                                                      // phase adjustment in vertical scaling down When set to 1,
                                                                      // Y phase adjustment in vertical scaling down will be
                                                                      // bypass Bypass UV phase adjustment in vertical scaling
                                                                      // down

    typedef UReg<0x02, 0x02, 7, 1> MADPT_UV_VSCALE_BYPS;              // When set to 1, UV phase adjustment in vertical scaling
                                                                      // down will be bypass


    typedef UReg<0x02, 0x03, 0, 1> MADPT_NOISE_DET_SEL;               // Noise detection selection When set to 1, noise detection
                                                                      // is in video active period

    typedef UReg<0x02, 0x03, 1, 2> MADPT_NOISE_DET_SHIFT;             // When set to 0, noise detection is in video blanking
                                                                      // period. Noise detection shift When set to 3, noise
                                                                      // detection drop 15bits When set to 2, noise detection drop
                                                                      // 16bits When set to 1, noise detection drop 17bits When
                                                                      // set to 0, noise detection drop 18bits

    typedef UReg<0x02, 0x03, 3, 2> MADPT_NOISE_DET_RST;               // Noise detection time reset value When set to 3, time
                                                                      // counter reset at 1023. When set to 2, time counter reset
                                                                      // at 511. When set to 1, time counter reset at 255


    typedef UReg<0x02, 0x04, 0, 7> MADPT_NOISE_THRESHOLD_NOUT;        // Auto noise detect threshold for NOUT Threshold for NOUT
                                                                      // signal


    typedef UReg<0x02, 0x05, 0, 7> MADPT_NOISE_THRESHOLD_VDS;         // Auto noise detect threshold for nout_vds_proc Threshold
                                                                      // for nout_vds_proc signal


    typedef UReg<0x02, 0x06, 0, 8> MADPT_GM_NOISE_VALUE;              // In global motion noise auto-detect mode, global motion
                                                                      // noise’s offset bit [3:0] Global noise high/global noise
                                                                      // auto detect offset high In global motion noise manual
                                                                      // mode, global motion detection noise bit [7:4] In global
                                                                      // motion noise auto-detect mode, global motion noise’s
                                                                      // offset bit [7:4]


    typedef UReg<0x02, 0x07, 0, 8> MADPT_STILL_NOISE_VALUE;           // Global still control value MADPT_STILL_NOISE_VA In manual
                                                                      // mode, still-noise value bit. LUE In auto-detect mode,
                                                                      // still-noise’s offset bit


    typedef UReg<0x02, 0x08, 0, 8> MADPT_LESS_NOISE_VALUE;            // Less-still noise value User defined less still noise
                                                                      // value


    typedef UReg<0x02, 0x09, 0, 4> MADPT_NOISE_EST_GAIN;              // Global motion noise gain (in auto-detect mode) Global
                                                                      // motion noise gain in noise auto-detect mode

    typedef UReg<0x02, 0x09, 4, 4> MADPT_STILL_NOISE_EST_GAIN;        // Still-noise gain (in auto-detect mode)


    typedef UReg<0x02, 0x0A, 4, 1> MADPT_NOISE_EST_EN;                // Global noise auto detection enable When set to 1,global
                                                                      // noise detection is in auto mode

    typedef UReg<0x02, 0x0A, 5, 1> MADPT_STILL_NOISE_EST_EN;          // When set to 0,global noise detection is in manual mode.
                                                                      // Still-noise auto detection enable When set to 1, still-
                                                                      // noise is in auto detection; When set to 0, still-noise is
                                                                      // in manual mode

    typedef UReg<0x02, 0x0A, 7, 1> MADPT_Y_MI_DET_BYPS;               // Y motion index generation bypass When set to 1, Y motion
                                                                      // index generation is in manual mode


    typedef UReg<0x02, 0x0B, 0, 7> MADPT_Y_MI_OFFSET;                 // Y motion index offset In auto mode, Y motion index’s
                                                                      // offset


    typedef UReg<0x02, 0x0C, 0, 4> MADPT_Y_MI_GAIN;                   // Y motion index gain Motion index feedback-bit bypass

    typedef UReg<0x02, 0x0C, 4, 1> MADPT_MI_1BIT_BYPS;                // When set to 1, motion index feedback-bit function will be
                                                                      // bypass

    typedef UReg<0x02, 0x0C, 5, 1> MADPT_MI_1BIT_FRAME2_EN;           // Enable Frame-two feedback-bit When set to 1, enable
                                                                      // frame-two feedback-bit


    typedef UReg<0x02, 0x0D, 0, 7> MADPT_MI_THRESHOLD;                // Motion index feedback-bit generation’s threshold bit


    typedef UReg<0x02, 0x0F, 0, 1> MADPT_STILL_DET_EN;                // Still detection enable When set to 1, still detection is
                                                                      // in auto mode

    typedef UReg<0x02, 0x0F, 1, 1> MADPT_STILL_ID;                    // When set to 0, still detection is in manual mode. Still
                                                                      // indicator defined by user (in manual mode only) Still
                                                                      // indicator defined by user, only useful in STILL_DET_EN
                                                                      // =0. Still detection’s auto unlock value

    typedef UReg<0x02, 0x0F, 2, 2> MADPT_STILL_UNLOCK;                // When unlock counter equals unlock value, “still” will go
                                                                      // inactive

    typedef UReg<0x02, 0x0F, 4, 4> MADPT_STILL_LOCK;                  // Still detection’s auto lock value When lock counter
                                                                      // equals lock value, “still” will go active


    typedef UReg<0x02, 0x10, 0, 1> MADPT_LESS_STILL_DET_EN;           // Less still detection enable When set to 1, less-still
                                                                      // detection is in auto mode. When set to 0, less-still
                                                                      // detection is in manual mode. Less still indicator defined
                                                                      // by user (in manual mode only)

    typedef UReg<0x02, 0x10, 1, 1> MADPT_LESS_STILL_ID;               // Less-still indicator defined by user, only useful in
                                                                      // LESS_STILL_DET_EN =0

    typedef UReg<0x02, 0x10, 2, 2> MADPT_LESS_STILL_UNLOCK;           // Less still detection’s auto unlock value When unlock
                                                                      // counter equals unlock value, “less-still” will go
                                                                      // inactive

    typedef UReg<0x02, 0x10, 4, 4> MADPT_LESS_STILL_LOCK;             // Less still detection’s auto lock value When lock counter
                                                                      // equals lock value, “less-still” will go active


    typedef UReg<0x02, 0x11, 3, 1> MADPT_EN_PULLDOWN32;               // 3:2 pull-down detection enable When set to 1, 3:2 pull-
                                                                      // down detection is in auto mode

    typedef UReg<0x02, 0x11, 4, 1> MADPT_PULLDOWN32_ID;               // When set to 0, 3:2 pull-down detection is in manual mode.
                                                                      // 3:2 pull-down indicator defined by user (in manual mode)
                                                                      // 3:2 pull-down indicator by user, only useful in
                                                                      // 32PULLDOWN_EN =0 3:2 pull-down sequence offset

    typedef UReg<0x02, 0x11, 5, 3> MADPT_PULLDOWN32_OFFSET;           // 3:2 pull-down sequence offset


    typedef UReg<0x02, 0x12, 0, 7> MADPT_PULLDOWN32_LOCK_RST;         // 3:2 pull-down auto lock value bit When lock counter
                                                                      // equals lock value, 3:2 pull-down is in active


    typedef UReg<0x02, 0x13, 2, 1> MADPT_PULLDOWN22_OFFSET;           // 2:2 pull-down detection enable When set to 1, 2:2 pull-
                                                                      // down detection is in auto mode. When set to 0, 2:2 pull-
                                                                      // down detection is in manual mode. 2:2 pull-down indicator
                                                                      // defined by user (in manual mode) 2:2 pull-down indicator
                                                                      // by user, only useful in 22PULLDOWN_EN =0 2:2 pull-down
                                                                      // sequence offset 2:2 pull-down sequence offset

    typedef UReg<0x02, 0x13, 4, 3> MADPT_PULLDOWN22_DET_CNTRL;        // 2:2 pull-down detection control bit 2:2 pull-down
                                                                      // accumulation result control


    typedef UReg<0x02, 0x14, 0, 18> MADPT_PULLDOWN22_THRESHOLD;       // 2:2 pull-down detection threshold bit [17:16] 2:2 pull-
                                                                      // down detection threshold bit [17:16]


    typedef UReg<0x02, 0x16, 4, 1> MADPT_MO_ADP_Y_EN;                 // Enable pull-down in Y motion adaptive When set to 1,
                                                                      // enable pull-down for Y data motion adaptive

    typedef UReg<0x02, 0x16, 5, 1> MADPT_MO_ADP_UV_EN;                // Enable pull-down in UV motion adaptive When set to 1,
                                                                      // enable pull-down for UV data motion adaptive

    typedef UReg<0x02, 0x16, 6, 1> MADPT_VT_FILTER_CNTRL;             // Vertical Temporal Filter Control When set to 1, do motion
                                                                      // adaptive in interpolated line only. When set to 0, do
                                                                      // motion adaptive in every line. Select original data in
                                                                      // progressive mode in VT filter

    typedef UReg<0x02, 0x16, 7, 1> MAPDT_VT_SEL_PRGV;                 // If the input is progressive mode or graphic mode, this
                                                                      // bit must be set to 1


    typedef UReg<0x02, 0x17, 0, 8> MADPT_Y_DELAY_UV_DELAY;

    typedef UReg<0x02, 0x17, 0, 4> MADPT_Y_DELAY;                     // Y delay pipe control MADPT_Y_DELAY Y data delay pipes
                                                                      // 0000 1 0001 2 0010 3 0011 4 0100 5 0101 6 0110 7 0111 8
                                                                      // 1000 9 1001 10 1010 11 1011 12 1100 13 1101 14 1110 15
                                                                      // 1111 16

    typedef UReg<0x02, 0x17, 4, 4> MADPT_UV_DELAY;                    // UV delay pipe control MADPT_UV_DELAY UV data delay pipes
                                                                      // 0000 1 0001 2 0010 3 0011 4 0100 5 0101 6 0110 7 0111 8
                                                                      // 1000 9 1001 10 1010 11 1011 12 1100 13 1101 14 1110 15
                                                                      // 1111 16


    typedef UReg<0x02, 0x18, 0, 1> MADPT_DIVID_BYPS;                  // Motion index divide bypass When = 1, motion index no
                                                                      // divide

    typedef UReg<0x02, 0x18, 1, 1> MADPT_DIVID_SEL;                   // When = 0, motion index will by divided by 2 or 4. Motion
                                                                      // index divide selection When = 1, motion index will be
                                                                      // divided by 2 in still

    typedef UReg<0x02, 0x18, 3, 1> MADPT_HTAP_BYPS;                   // Motion index horizontal filter bypass When =1, motion
                                                                      // index horizontal filter will be bypass

    typedef UReg<0x02, 0x18, 4, 4> MADPT_HTAP_COEFF;                  // Motion index horizontal filter coefficient Motion index
                                                                      // horizontal filter coefficient


    typedef UReg<0x02, 0x19, 0, 1> MADPT_BIT_STILL_EN;                // Enable pixel base still

    typedef UReg<0x02, 0x19, 2, 1> MADPT_VTAP2_BYPS;                  // When = 1, motion index’s vertical filter will be bypass

    typedef UReg<0x02, 0x19, 3, 1> MADPT_VTAP2_ROUND_SEL;             // Motion index vertical filter round selection When set to
                                                                      // 1, the input data will be divided by 2

    typedef UReg<0x02, 0x19, 4, 4> MADPT_VTAP2_COEFF;                 // Motion index vertical filter coefficient


    typedef UReg<0x02, 0x1A, 0, 8> MADPT_PIXEL_STILL_THRESHOLD_1;     // Pixel base still threshold level one


    typedef UReg<0x02, 0x1B, 0, 8> MADPT_PIXEL_STILL_THRESHOLD_2;     // Pixel base still threshold level two


    typedef UReg<0x02, 0x1F, 0, 8> MADPT_HFREQ_NOISE;                 // High-frequency detection noise value The noise value for
                                                                      // high-frequency detection


    typedef UReg<0x02, 0x20, 0, 1> MADPT_HFREQ_DET_EN;                // High-frequency detection enable When set to 1, high-
                                                                      // frequency detection is in auto mode

    typedef UReg<0x02, 0x20, 1, 1> MADPT_HFREQ_ID;                    // When set to 0, high-frequency detection is in manual
                                                                      // mode. High-frequency indicator by user (in manual mode)
                                                                      // High-frequency indicator by user, only useful in
                                                                      // HFREQ_DET_EN =0

    typedef UReg<0x02, 0x20, 4, 4> MADPT_HFREQ_LOCK;                  // High-frequency auto lock value When high-frequency lock
                                                                      // counter equals lock value, high-frequency will be active


    typedef UReg<0x02, 0x21, 0, 3> MADPT_HFREQ_UNLOCK;                // High-frequency auto unlock value When high-frequency
                                                                      // unlock counter equals unlock value, high-frequency will
                                                                      // be

    typedef UReg<0x02, 0x21, 4, 1> MADPT_EN_NOUT_FOR_STILL;           // Enable NOUT for still detection

    typedef UReg<0x02, 0x21, 5, 1> MADPT_EN_NOUT_FOR_LESS_STILL;      // Enable NOUT for less-still detection


    typedef UReg<0x02, 0x22, 0, 5> MADPT_PD_SP;                       // Scaling down line buffer WRSTZ position adjustment bits
                                                                      // Adjust the position of write reset in vertical IIR filter
                                                                      // line buffer, and phase


    typedef UReg<0x02, 0x23, 0, 5> MADPT_PD_ST;                       // Scaling down line buffer RRSTZ position adjustment Adjust
                                                                      // the position of read reset in vertical IIR filter line
                                                                      // buffer, and phase


    typedef UReg<0x02, 0x24, 2, 1> MADPT_PD_RAM_BYPS;                 // Bypass scaling down’s line buffer When set to 1, scaling
                                                                      // down’s line buffer will be bypass


    typedef UReg<0x02, 0x26, 6, 1> MADPT_VIIR_BYPS;                   // Bypass V-IIR filter in vertical scaling down When set to
                                                                      // 1, V-IIR filter in vertical scaling down will be bypass

    typedef UReg<0x02, 0x26, 7, 1> MADPT_VIIR_ROUND_SEL;              // V-IIR filter in vertical scaling down round selection
                                                                      // When set to 1, the input data will be divided by 2


    typedef UReg<0x02, 0x27, 0, 7> MADPT_VIIR_COEF;                   // V-IIR filter coefficient


    typedef UReg<0x02, 0x28, 4, 4> MADPT_VSCALE_RATE_LOW;             // Vertical non-linear scale down DDA increment shared low
                                                                      // 4-bit All the segment DDA increment share low 4bit


    typedef UReg<0x02, 0x29, 0, 8> MADPT_VSCALE_RATE_SEG0;            // st Vertical non-linear scale down 1 segment DDA increment
                                                                      // value The actual DDA increment is vscale={vscale0,
                                                                      // vscale_low}. Assume the scale factor is n/m, then vscale=
                                                                      // 4095x(m-n)/n


    typedef UReg<0x02, 0x2A, 0, 8> MADPT_VSCALE_RATE_SEG1;            // nd Vertical non-linear scale down 2 segment DDA increment
                                                                      // value The actual DDA increment is vscale={vscale1,
                                                                      // vscale_low}


    typedef UReg<0x02, 0x2B, 0, 8> MADPT_VSCALE_RATE_SEG2;            // rd Vertical non-linear scale down 3 segment DDA increment
                                                                      // value The actual DDA increment is vscale={vscale2,
                                                                      // vscale_low}


    typedef UReg<0x02, 0x2C, 0, 8> MADPT_VSCALE_RATE_SEG3;            // th Vertical non-linear scale down 4 segment DDA increment
                                                                      // value The actual DDA increment is vscale={vscale3,
                                                                      // vscale_low}


    typedef UReg<0x02, 0x2D, 0, 8> MADPT_VSCALE_RATE_SEG4;            // th Vertical non-linear scale down 5 segment DDA increment
                                                                      // value The actual DDA increment is vscale={vscale4,
                                                                      // vscale_low}


    typedef UReg<0x02, 0x2E, 0, 8> MADPT_VSCALE_RATE_SEG5;            // th Vertical non-linear scale down 6 segment DDA increment
                                                                      // value The actual DDA increment is vscale={vscale5,
                                                                      // vscale_low}


    typedef UReg<0x02, 0x2F, 0, 8> MADPT_VSCALE_RATE_SEG6;            // th Vertical non-linear scale down 7 segment DDA increment
                                                                      // value The actual DDA increment is vscale={vscale6,
                                                                      // vscale_low}


    typedef UReg<0x02, 0x30, 0, 8> MADPT_VSCALE_RATE_SEG7;            // th Vertical non-linear scale down 8 segment DDA increment
                                                                      // value The actual DDA increment is vscale={vscale7,
                                                                      // vscale_low}


    typedef UReg<0x02, 0x31, 0, 2> MADPT_VSCALE_DEC_FACTOR;

    typedef UReg<0x02, 0x31, 2, 1> MADPT_SEL_PHASE_INI;               // Vertical non-linear scaling-down factor select If the
                                                                      // scaling ratio is less than ½, use it and DDA to generate
                                                                      // the we and phase MADPT_VSCALE_DEC_FA CTOR 01: scaling-
                                                                      // ratio is less than ½. 10: scaling-ratio is less than ¼.
                                                                      // Vertical scaling down initial phase selection Test bus
                                                                      // output enable

    typedef UReg<0x02, 0x31, 3, 1> MADPT_TEST_EN;                     // Internal hardware debugging use only

    typedef UReg<0x02, 0x31, 4, 4> MADPT_TEST_SEL;                    // Test bus select Internal hardware debugging use only


    typedef UReg<0x02, 0x32, 0, 4> MADPT_Y_HTAP_CNTRL;                // Y horizontal filter control for background reduction
                                                                      // Y_HTAP_CNTRL[3:0] could bypass four tap3 FIR filter

    typedef UReg<0x02, 0x32, 4, 3> MADPT_Y_VTAP_CNTRL;                // Y vertical filter control for background reduction
                                                                      // Y_VTAP_CNTRL[0]: when set to1, bypass vertical filter
                                                                      // Y_VTAP_CNTRL[1]: when set to 1, enable FIR filter
                                                                      // Y_VTAP_CNTRL[2]: when set to 1, bypass IIR filter

    typedef UReg<0x02, 0x32, 7, 1> MADPT_NRD_SEL;                     // Background reduction selection control Only set it to 1
                                                                      // in huge noise condition


    typedef UReg<0x02, 0x33, 0, 4> MADPT_M_HTAP_CNTRL;                // Background noise reduction H filter control in huge noise
                                                                      // condition M_HTAP_CNTRL[3:0] could bypass four tap3 FIR
                                                                      // filter

    typedef UReg<0x02, 0x33, 4, 3> MADPT_M_VTAP_CNTRL;                // Background noise reduction V filter control in huge noise
                                                                      // condition M_VTAP_CNTRL[0]: when set to1, bypass vertical
                                                                      // filter M_VTAP_CNTRL[1]: when set to 1, enable FIR filter


    typedef UReg<0x02, 0x34, 0, 1> MADPT_Y_WOUT_BYPS;                 // Bypass Y WOUT

    typedef UReg<0x02, 0x34, 1, 3> MADPT_Y_WOUT;                      // Coefficient for Y noise reduction

    typedef UReg<0x02, 0x34, 4, 1> MADPT_UV_WOUT_BYPS;                // Bypass UV WOUT

    typedef UReg<0x02, 0x34, 5, 3> MADPT_UV_WOUT;                     // Coefficient for UV noise reduction


    typedef UReg<0x02, 0x35, 0, 1> MADPT_Y_NRD_ENABLE;                // Enable background noise reduction in Y domain When set to
                                                                      // 1, enable background noise reduction in Y domain

    typedef UReg<0x02, 0x35, 1, 1> MADPT_UV_NRD_ENABLE;               // Enable background noise reduction in UV domain When set
                                                                      // to 1, enable background noise reduction in UV domain

    typedef UReg<0x02, 0x35, 2, 1> MADPT_NRD_OUT_SEL;                 // NRD output selection Only set it to 1 in huge noise
                                                                      // condition

    typedef UReg<0x02, 0x35, 3, 1> MADPT_DD0_SEL;                     // DD0 select control Set it to 1 when background noise
                                                                      // reduction enable

    typedef UReg<0x02, 0x35, 4, 1> MADPT_NRD_VIIR_PD_BYPS;            // Set it to 0 when background noise reduction disable
                                                                      // Bypass NRD VIIR line buffer

    typedef UReg<0x02, 0x35, 5, 1> MADPT_UVDLY_PD_BYPS;               // Bypass UV delay line buffer Motion compare enable

    typedef UReg<0x02, 0x35, 6, 1> MADPT_CMP_EN;                      // When set to 1, enable motion compare

    typedef UReg<0x02, 0x35, 7, 1> MADPT_CMP_USER_ID;                 // When set to 0, motion compare is in manual mode Motion
                                                                      // compare result defined by user (in manual mode) Motion
                                                                      // compare result defined by user when CMP_EN = 0


    typedef UReg<0x02, 0x36, 0, 8> MADPT_CMP_LOW_THRESHOLD;           // Motion compare low level threshold


    typedef UReg<0x02, 0x37, 0, 8> MADPT_CMP_HIGH_THRESHOLD;          // Motion compare high level threshold


    typedef UReg<0x02, 0x38, 0, 4> MADPT_NRD_VIIR_PD_SP;              // NRD line buffer WRSTZ position adjustment

    typedef UReg<0x02, 0x38, 4, 4> MADPT_NRD_VIIR_PD_ST;              // NRD line buffer RRSTZ position adjustment


    typedef UReg<0x02, 0x39, 0, 4> MADPT_UVDLY_PD_SP;                 // UV delay line buffer WRSTZ position adjustment

    typedef UReg<0x02, 0x39, 4, 4> MADPT_UVDLY_PD_ST;                 // UV delay line buffer RRSTZ position adjustment


    typedef UReg<0x02, 0x3A, 0, 1> MADPT_EN_UV_DEINT;                 // Enable UV deinterlacer When set to 1, enable UV
                                                                      // deinterlacer

    typedef UReg<0x02, 0x3A, 1, 1> MADPT_EN_PULLDWN_FOR_NRD;          // Enable pull-down to block STILL for NRD Set it to 1,
                                                                      // background noise reduction will in low noise level when
                                                                      // in 32/22 pull- down source

    typedef UReg<0x02, 0x3A, 2, 1> MADPT_EN_NOUT_FOR_NRD;             // Enable NOUT for background noise reduction

    typedef UReg<0x02, 0x3A, 3, 1> MADPT_EN_STILL_FOR_NRD;            // Enable still for background noise reduction Enable STILL
                                                                      // to reset pull-down detection

    typedef UReg<0x02, 0x3A, 4, 1> MADPT_EN_STILL_FOR_PULLDWN;        // When set to 1, still will be used to reset 3:2/2:2 pull-
                                                                      // down detection

    typedef UReg<0x02, 0x3A, 5, 2> MADPT_MI_1BIT_DLY;                 // Delay pipe control for motion index feedback-bit
                                                                      // MADPT_MI_1BIT_DELAY MI feedback-bit delay pipes 00 1 01 2
                                                                      // 10 3 11 4

    typedef UReg<0x02, 0x3A, 7, 1> MADPT_UV_MI_DET_BYPS;              // UV motion index generation bypass When set to 1, UV
                                                                      // motion index generation is in manual mode


    typedef UReg<0x02, 0x3B, 0, 7> MADPT_UV_MI_OFFSET;                // UV motion index offset In auto mode, UV motion index
                                                                      // offset


    typedef UReg<0x02, 0x3C, 0, 4> MADPT_UV_MI_GAIN;                  // UV motion index gain UV motion index gain

    typedef UReg<0x02, 0x3C, 4, 3> MADPT_MI_DELAY;                    // Motion index delay control Control motion index (both Y
                                                                      // and UV’s) delay pipes, so that the motion index can align
                                                                      // with corresponding data. MADPT_MI_DELAY Motion index
                                                                      // delay pipes 000 1 pipe 001 2 pipe 010 3 pipes 011 4 pipes
                                                                      // 100 5 pipes 101 6 pipes 110 7 pipes

    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_DEINTERLACER_H
