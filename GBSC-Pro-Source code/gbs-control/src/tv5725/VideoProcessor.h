#ifndef TV5725_VIDEO_PROCESSOR_H
#define TV5725_VIDEO_PROCESSOR_H

#include "Tv5725.h"

namespace Tv5725 {

// The video display scaler's fixed configuration: filter taps, chroma tag
// slopes, peaking and coring coefficients, the scan-velocity modulator, and the
// test bypasses that are all off.
//
// All twelve preset tables agree on every field here and none of it moves with
// the mode, while RD-5725-1.1 documents what the fields do without offering a
// right value for a board -- so this is a hundred constants carried for
// continuity. The geometry, which is what does move, is Geometry's.
//
// Two absences that look like omissions:
//
//   - VDS_EXT_HB_* and VDS_EXT_VB_* do nothing on this board. They program the
//     HBOUT/VBOUT blanking for external use and PAD_BLK_OUT_ENZ is 1, so those
//     pins are off; VDS_SYNC_IN_SEL is 0 too, so there is no internal consumer
//     either. Measured stale against the live windows with a perfect picture.
//
//   - VDS_TAP6_BYPS, VDS_D_RAM_BYPS, VDS_PK_Y_H_BYPS and VDS_UV_STEP_BYPS are
//     the user's, written from uopt-> in doPostPresetLoadSteps(). Writing them
//     here would reset those preferences on every mode change.
class VideoProcessor {
public:
    typedef UReg<0x03, 0x00, 0, 1> VDS_SYNC_EN;                       // External sync enable, active high This bit enable sync
                                                                      // lock mode. vds_flock_en (1A[4]) vds_sync_en VDS timing 0
                                                                      // 0 Free run 0 1 Sync lock ABAB double field mode enable

    typedef UReg<0x03, 0x00, 1, 1> VDS_FIELDAB_EN;                    // In field double mode, when this bit is 1, VDS works in
                                                                      // ABAB mode, otherwise it

    typedef UReg<0x03, 0x00, 2, 1> VDS_DFIELD_EN;                     // works in AABB mode. Double field mode enable active high
                                                                      // This bit enable field double mode, ex, frame rate from
                                                                      // 50Hz to 100Hz, or from 60Hz to 120Hz. When this bit is 1,
                                                                      // the output timing is interlaced

    typedef UReg<0x03, 0x00, 3, 1> VDS_FIELD_FLIP;                    // Flip field control. This bit is field flip control bit,
                                                                      // it only used in interlace mode. When it is 1, it

    typedef UReg<0x03, 0x00, 4, 1> VDS_HSCALE_BYPS;                   // inverts the output field. Horizontal scale up bypass
                                                                      // control, active high When this bit is 1, data will bypass
                                                                      // horizontal scale up process

    typedef UReg<0x03, 0x00, 5, 1> VDS_VSCALE_BYPS;                   // Vertical scale up bypass control, active high When this
                                                                      // bit is 1, data will bypass vertical scale up process

    typedef UReg<0x03, 0x00, 6, 1> VDS_HALF_EN;                       // Horizontal scale up bypass control, active high
                                                                      // Horizontal scale up bypass control, active high

    typedef UReg<0x03, 0x00, 7, 1> VDS_SRESET;                        // When this bit is 1, it reset the VDS_PROC internal module
                                                                      // ds_video_enhance


    typedef UReg<0x03, 0x01, 0, 12> VDS_HSYNC_RST;                    // Internal Horizontal period control bit[7:0], Half of
                                                                      // total pixels in field double mode. This field contains
                                                                      // horizontal total value minus 1. EX: Horizontal pixels is
                                                                      // A, then HSYNC_RST[9:0] = A-1, in field double mode,
                                                                      // HSYNC_RST[9:0] = (A/2 –1)


    typedef UReg<0x03, 0x02, 4, 11> VDS_VSYNC_RST;                    // HSYNC_RST[9:0] = (A/2 –1) Internal Vertical period
                                                                      // control bit[3:0] This field contains vertical total value
                                                                      // minus 1


    typedef UReg<0x03, 0x04, 0, 12> VDS_HB_ST;                        // Horizontal blanking start position control bit[7:0] This
                                                                      // field is used to program horizontal blanking stop
                                                                      // position, this blanking is used to get data from memory


    typedef UReg<0x03, 0x05, 4, 12> VDS_HB_SP;                        // used to get data from memory. Horizontal blanking stop
                                                                      // position control bit[3:0] This field is used to program
                                                                      // horizontal blanking stop position, this blanking is used
                                                                      // to get data from memory


    typedef UReg<0x03, 0x07, 0, 11> VDS_VB_ST;                        // Vertical blanking start position control bit[10:8] This
                                                                      // field is used to program vertical blanking start position


    typedef UReg<0x03, 0x08, 4, 11> VDS_VB_SP;                        // Vertical blanking stop position control bit[10:4] This
                                                                      // field is used to program vertical blanking stop position


    typedef UReg<0x03, 0x0A, 0, 12> VDS_HS_ST;                        // Horizontal sync start position control bit [7:0] This
                                                                      // field is used to program horizontal sync start position


    typedef UReg<0x03, 0x0B, 4, 12> VDS_HS_SP;                        // Horizontal sync stop position control bit [11:4] This
                                                                      // field is used to program horizontal sync stop position


    typedef UReg<0x03, 0x0D, 0, 11> VDS_VS_ST;                        // Vertical sync start position control bit [10:8] This
                                                                      // field is used to program vertical sync start position


    typedef UReg<0x03, 0x0E, 4, 11> VDS_VS_SP;                        // Vertical sync stop position control bit [10:4] This field
                                                                      // is used to program vertical sync stop position


    typedef UReg<0x03, 0x10, 0, 12> VDS_DIS_HB_ST;                    // Final display horizontal blanking start position control
                                                                      // bit [7:0] This field contains final display horizontal
                                                                      // blanking start position control, this blanking is used to
                                                                      // clean the output data in blanking


    typedef UReg<0x03, 0x11, 4, 12> VDS_DIS_HB_SP;                    // blanking is used to clean the output data in blanking.
                                                                      // Final display horizontal blanking stop position control
                                                                      // bit [3:0] This field contains final display horizontal
                                                                      // blanking stop position control, this blanking is used to
                                                                      // clean the output data in blanking


    typedef UReg<0x03, 0x13, 0, 11> VDS_DIS_VB_ST;                    // Final display vertical blanking start position control
                                                                      // bit [7:0] This field contains final display vertical
                                                                      // blanking start position control, this blanking is used to
                                                                      // clean the output data in blanking


    typedef UReg<0x03, 0x14, 4, 11> VDS_DIS_VB_SP;                    // Final display vertical blanking stop position control bit
                                                                      // [10:4] This field contains final display vertical
                                                                      // blanking stop position control, this blanking is used to
                                                                      // clean the output data in blanking


    typedef UReg<0x03, 0x16, 0, 10> VDS_HSCALE;                       // Horizontal scaling coefficient bit [7:0] This field
                                                                      // indicates the ratio of scaling up. HSCALE = 1024 *
                                                                      // (resolution of input) / (resolution of output) EX: 720 *
                                                                      // 480 Æ 800 * 480, HSCALE = 1024 * 720 / 800


    typedef UReg<0x03, 0x17, 4, 10> VDS_VSCALE;                       // This field indicates the ratio of vertical scaling up.
                                                                      // VSCALE = 1024 * (resolution of input / resolution of
                                                                      // output) EX: 720*480 Æ 720*576, VSCALE = 1024 * 480 /576


    typedef UReg<0x03, 0x19, 0, 10> VDS_FRAME_RST;                    // Frame reset period control bit [7:0] This field indicates
                                                                      // how many frames VSD_PROC locked at each time, it based on
                                                                      // the input vertical sync. EX: FRAME_RST=4, this means
                                                                      // VDS_PROC will lock every 5 frames, (This frame number is
                                                                      // counts at every input vertical sync, the frame number of
                                                                      // VDS_PROC output maybe different)


    typedef UReg<0x03, 0x1A, 4, 1> VDS_FLOCK_EN;                      // Frame lock enable, active high This bit enables the frame
                                                                      // lock mode, when this bit is 1, VDS_PROC output timing
                                                                      // will lock with its input timing (from INPUT_FORMATTER) at
                                                                      // every 2 or more frames

    typedef UReg<0x03, 0x1A, 5, 1> VDS_FREERUN_FID;                   // Enable internal free run field index generation, active
                                                                      // high When this bit is 1, the output field index is
                                                                      // internal free run field, otherwise the

    typedef UReg<0x03, 0x1A, 6, 1> VDS_FID_AA_DLY;                    // output field index is based on input field index. Enable
                                                                      // internal free run AABB field delay 1 frame, active high
                                                                      // When this bit is 1, the internal free run AABB field will
                                                                      // delay 1 frame. Enable internal free run field index
                                                                      // reset, active high

    typedef UReg<0x03, 0x1A, 7, 1> VDS_FID_RST;                       // When this bit is 1, internal free run field index will
                                                                      // reset at every frame number is 0


    typedef UReg<0x03, 0x1B, 0, 32> VDS_FR_SELECT;                    // Frame size select control bit [23:16] FR_SELECT[2n+1:2n]
                                                                      // is for frame n selection. 0 select VSYNC_RST; 1 select
                                                                      // VSYNC_SIZE1; 2 select VSYNC_SIZE2


    typedef UReg<0x03, 0x1F, 0, 4> VDS_FRAME_NO;                      // Programmable repeat frame number control bit [3:0] This
                                                                      // field defines the repeated frame number, EX: if frame_no
                                                                      // = 2, then the frame will repeat every 3 frame.
                                                                      // VDS_FRAME_NO [3:0] repeat num 0 0 0 0 1 0 0 0 1 2 0 0 1 0
                                                                      // 3 0 0 1 1 4 0 1 0 0 5 0 1 0 1 6 0 1 1 0 7 0 1 1 1 8 1 0 0
                                                                      // 0 9 1 0 0 1 10 1 0 1 0 11 1 0 1 1 12 1 1 0 0 13 1 1 0 1
                                                                      // 14 1 1 1 0 15 1 1 1 1 16

    typedef UReg<0x03, 0x1F, 4, 1> VDS_DIF_FR_SEL_EN;                 // Enable the different frame size, active high When this
                                                                      // bit is 1, VDS_PROC can generate a sequence of different
                                                                      // frame size

    typedef UReg<0x03, 0x1F, 5, 1> VDS_EN_FR_NUM_RST;                 // Enable frame number reset, active high When this bit is
                                                                      // 1, frame number will be reset to 1 when frame lock is
                                                                      // occur


    typedef UReg<0x03, 0x20, 0, 11> VDS_VSYN_SIZE1;                   // Programmable vertical total size 1 control bit [7:0] This
                                                                      // field contains the vertical total line number minus 1. It
                                                                      // can be the same as vsync_rst and vsync_size2, it also can
                                                                      // different with them, and it can be used to define
                                                                      // different frame size


    typedef UReg<0x03, 0x22, 0, 11> VDS_VSYN_SIZE2;                   // Programmable vertical total size 2 control bit [7:0] This
                                                                      // field contains the vertical total line number minus 1. It
                                                                      // can be the same as vsync_rst and vsync_size1, it also can
                                                                      // different with them, and it can be used to define
                                                                      // different frame size


    typedef UReg<0x03, 0x24, 0, 8> VDS_3_24;

    typedef UReg<0x03, 0x24, 0, 1> VDS_UV_FLIP;                       // 422 to 444 conversion UV flip control This bit is used to
                                                                      // flip UV, when this bit is 1, UV position will be flipped

    typedef UReg<0x03, 0x24, 1, 1> VDS_U_DELAY;                       // UV 422 to 444 conversion U delay When this bit is 1, U
                                                                      // will delay 1 clock, otherwise, no delay for internal pipe

    typedef UReg<0x03, 0x24, 2, 1> VDS_V_DELAY;                       // UV 422 to 444 conversion V delay When this bit is 1, V
                                                                      // will delay 1 clock, otherwise, no delay for internal pipe

    typedef UReg<0x03, 0x24, 3, 1> VDS_TAP6_BYPS;                     // Tap6 filter in 422 to 444 conversion bypass control,
                                                                      // active high This bit is the UV interpolation filter
                                                                      // enable control; when this bit is 1, UV bypass

    typedef UReg<0x03, 0x24, 4, 2> VDS_Y_DELAY;                       // the filter Y compensation delay control bit [1:0] in 422
                                                                      // to 444 conversion To compensation the pipe of UV, program
                                                                      // this field can delay Y from 1 to 4 clocks. VDS_Y_DELAY
                                                                      // [1:0] Y delay 0 0 1 0 1 2 1 0 3 1 1 4 Compensation delay
                                                                      // control bit [1:0] for horizontal write enable

    typedef UReg<0x03, 0x24, 6, 2> VDS_WEN_DELAY;                     // This two-bit register defines the compensation delay of
                                                                      // horizontal scale up write enable and phase. VDS_WEN_DELAY
                                                                      // [1:0] Delay (VCLK) 0 0 1 0 1 2 1 0 3 1 1 4


    typedef UReg<0x03, 0x25, 0, 10> VDS_D_SP;                         // Line buffer write reset position control bit [7:0] This
                                                                      // field contains the write reset position of the line
                                                                      // buffer, this position is also the write start position of
                                                                      // the buffer


    typedef UReg<0x03, 0x26, 6, 1> VDS_D_RAM_BYPS;                    // When this bit is 1, data will bypass the line buffer

    typedef UReg<0x03, 0x26, 7, 1> VDS_BLEV_AUTO_EN;                  // Y minimum and maximum level auto detection enable, active
                                                                      // high This bit is the Y min and max auto detection enable
                                                                      // bit for black/white level expansion, when this bit is 1,
                                                                      // the min and max value of Y in every frame will be
                                                                      // detected, otherwise, the min and max value are defined by
                                                                      // register


    typedef UReg<0x03, 0x27, 0, 4> VDS_USER_MIN;                      // Programmable minimum value control bit [3:0] This field
                                                                      // is the user defined min value for black level expansion,
                                                                      // the actual min

    typedef UReg<0x03, 0x27, 4, 4> VDS_USER_MAX;                      // value in use is 2*blev_det_min+1. Programmable maximum
                                                                      // value control bit [3:0] This field is the user defined
                                                                      // max value for black level expansion, the actual min value
                                                                      // in use is 16*blev_det_max+15


    typedef UReg<0x03, 0x28, 0, 8> VDS_BLEV_LEVEL;                    // Black level expansion level control bit [7:0] This field
                                                                      // defines the black level expansion threshold level value,
                                                                      // data larger than this level will have no black level
                                                                      // expansion process


    typedef UReg<0x03, 0x29, 0, 8> VDS_BLEV_GAIN;                     // Black level expansion gain control bit [7:0] This field
                                                                      // contains the gain control of black level expansion, its
                                                                      // range is (0~16)*16


    typedef UReg<0x03, 0x2A, 0, 1> VDS_BLEV_BYPS;                     // Black level expansion bypass control, active high This
                                                                      // bit is the bypass control bit of black level expansion,
                                                                      // when it is 1, data will

    typedef UReg<0x03, 0x2A, 4, 2> VDS_STEP_DLY_CNTRL;                // VDS_STEP_DLY_CNTRL [1:0] Data selelect 0 0 U/V5 – U/V6 0
                                                                      // 1 U/V4 – U/V7 1 0 U/V3 – U/V8 1 1 U/V2 – U/V9 U/V2 is 2
                                                                      // clocks delay of input U/V, UV3 is 3 clocks delay of input
                                                                      // U/V, and so


    typedef UReg<0x03, 0x2B, 0, 4> VDS_STEP_GAIN;                     // UV Step response gain control bit [3:0] This field
                                                                      // register can adjust the UV edge improvement, the larger
                                                                      // value of this

    typedef UReg<0x03, 0x2B, 4, 3> VDS_STEP_CLIP;                     // register, the sharper edge will appear, the range of this
                                                                      // gain is (0~4)*4. UV step response clip control bit [2:0]
                                                                      // This filed contains the clip control value of UV step
                                                                      // response UV step response bypass control, active high

    typedef UReg<0x03, 0x2B, 7, 1> VDS_UV_STEP_BYPS;                  // When this bit is 1, UV data will don’t do step response


    typedef UReg<0x03, 0x2C, 0, 8> VDS_SK_U_CENTER;                   // Skin color correction U center position control bit [7:0]
                                                                      // This field contains the skin color center position U
                                                                      // value, the value is 2’s


    typedef UReg<0x03, 0x2D, 0, 8> VDS_SK_V_CENTER;                   // Skin color correction V center position control bit [7:0]
                                                                      // This field contains the skin color center position U
                                                                      // value, the value is 2’s


    typedef UReg<0x03, 0x2E, 0, 8> VDS_SK_Y_LOW_TH;                   // Skin color correction Y low threshold control bit [7:0] Y
                                                                      // low threshold value for skin color correction, if y less
                                                                      // than this threshold, no skin color correction done


    typedef UReg<0x03, 0x2F, 0, 8> VDS_SK_Y_HIGH_TH;                  // Skin color correction Y high threshold control bit [7:0]
                                                                      // Y high threshold value for skin color correction, if y
                                                                      // larger than this threshold, no skin color correction done


    typedef UReg<0x03, 0x30, 0, 8> VDS_SK_RANGE;                      // Skin color correction range control bit [7:0] The skin
                                                                      // color correction will done just when the value
                                                                      // abs(u-u_center)+abs(v- v_enter) less than this
                                                                      // programmable range


    typedef UReg<0x03, 0x31, 0, 4> VDS_SK_GAIN;                       // Skin color correction gain control bit [3:0] This
                                                                      // register defines the degree of the skin color correction,
                                                                      // the higher the

    typedef UReg<0x03, 0x31, 4, 1> VDS_SK_Y_EN;                       // value, the more skin color correction done. Its range is
                                                                      // (0~1)*16 Skin color Y detect enable, active high When
                                                                      // this bit is 1, take the Y value as the condition of skin
                                                                      // color correction, just when the Y value larger than
                                                                      // y_low_th and less the y_high_th, the correction can be
                                                                      // done. Skin color correction bypass control, active high

    typedef UReg<0x03, 0x31, 5, 1> VDS_SK_BYPS;                       // When this bit is 1, the skin color correction will be
                                                                      // bypassed


    typedef UReg<0x03, 0x32, 0, 2> VDS_SVM_BPF_CNTRL;                 // SVM data generation select control [1:0]
                                                                      // VDS_SVM_BPF_CNTRL [1:0] SVM data 0 0 a0-a4 0 1 a1-a4 1 0
                                                                      // a2-a4 1 1 a3-a4 A1 is one pipe delay of a0, a2 is one
                                                                      // pipe delay of a1, a3 is one pipe delay of a2, a4 is one
                                                                      // pipe delay of a3, here a* is the input data y for
                                                                      // generate SVM signal

    typedef UReg<0x03, 0x32, 2, 1> VDS_SVM_POL_FLIP;                  // SVM polarity flip control bit When this bit is 1, the SVM
                                                                      // signal’s polarity will be flipped, otherwise, SVM remains

    typedef UReg<0x03, 0x32, 3, 1> VDS_SVM_2ND_BYPS;                  // the original phase. nd 2 order SVM signal generation
                                                                      // bypass, active high When this bit is 1, SVM signal is 1st
                                                                      // order, otherwise, it is 2nd order derivative signal. To
                                                                      // match YUV pipe, SVM data delay by VCLK control bit [2:0]

    typedef UReg<0x03, 0x32, 4, 3> VDS_SVM_VCLK_DELAY;                // This field define the SVM compensation delay from 1 to 8
                                                                      // VCLKs

    typedef UReg<0x03, 0x32, 7, 1> VDS_SVM_SIGMOID_BYPS;              // SVM bypass the sigmoid function, active high When this
                                                                      // bit is 1, SVM signal bypass a sigmoid function. This
                                                                      // function can make the SVM signal sharper


    typedef UReg<0x03, 0x33, 0, 8> VDS_SVM_GAIN;                      // SVM gain control bit[7:0] This field contains the gain
                                                                      // value of SVM data., its range is (0~16)*16


    typedef UReg<0x03, 0x34, 0, 8> VDS_SVM_OFFSET;                    // SVM offset control bit [7:0] This field contains the
                                                                      // offset value of SVM data, its range is 0~255


    typedef UReg<0x03, 0x35, 0, 8> VDS_Y_GAIN;                        // Y dynamic range expansion gain control bit [7:0] This
                                                                      // field contains the Y gain value in dynamic range
                                                                      // expansion process, its range is (0 ~ 2)*128


    typedef UReg<0x03, 0x36, 0, 8> VDS_UCOS_GAIN;                     // U dynamic range expansion cos gain control bit [7:0] This
                                                                      // field contains the U gain value in dynamic range
                                                                      // expansion process, its range is (-4 ~ 4)*32


    typedef UReg<0x03, 0x37, 0, 8> VDS_VCOS_GAIN;                     // V dynamic range expansion gain control bit [7:0] This
                                                                      // field contains the V gain value in dynamic range
                                                                      // expansion process, its range is (-4 ~ 4)*32


    typedef UReg<0x03, 0x38, 0, 8> VDS_USIN_GAIN;                     // control bit [7:0] value in dynamic range expansion
                                                                      // process, its range is (-4 ~ 4)*32


    typedef UReg<0x03, 0x39, 0, 8> VDS_VSIN_GAIN;                     // V dynamic range expansion sin gain control bit [7:0] This
                                                                      // field contains the V sin gain value in dynamic range
                                                                      // expansion process, its range is (-4 ~ 4)*32


    typedef UReg<0x03, 0x3A, 0, 8> VDS_Y_OFST;                        // Y dynamic range expansion offset control bit [7:0] This
                                                                      // field contains the Y offset value in dynamic range
                                                                      // expansion process, its range is –128 ~ 127


    typedef UReg<0x03, 0x3B, 0, 8> VDS_U_OFST;                        // U dynamic range expansion offset control bit [7:0] This
                                                                      // field contains the U offset value in dynamic range
                                                                      // expansion process, its range is –128 ~ 127


    typedef UReg<0x03, 0x3C, 0, 8> VDS_V_OFST;                        // V dynamic range expansion offset control bit [7:0] This
                                                                      // field contains the V offset value in dynamic range
                                                                      // expansion process., its range is –128 ~ 127


    typedef UReg<0x03, 0x3D, 0, 9> VDS_SYNC_LEV;                      // Sync level bit [7:0] This field contains the composite
                                                                      // sync level value, this value will add on Y, outside the
                                                                      // composite sync interval. If the Y out is 1V, sync is
                                                                      // 0.3V, then this value is (0.3/1)*1024=307, and the output
                                                                      // sync’s max voltage is 0.5V


    typedef UReg<0x03, 0x3E, 3, 1> VDS_CONVT_BYPS;                    // YUV to RGB color space conversion bypass control, active
                                                                      // high When this bit is 1, YUV data will bypass the YUV to
                                                                      // RGB conversion, the output will still be YUV data. When
                                                                      // this bit is 0, YUV data will do YUV to RGB conversion,
                                                                      // the output will be

    typedef UReg<0x03, 0x3E, 4, 1> VDS_DYN_BYPS;                      // RGB data. Dynamic range expansion bypass control, active
                                                                      // high When this bit is 1, data will bypass the dynamic
                                                                      // range expansion process. Blanking set up enable, active
                                                                      // high

    typedef UReg<0x03, 0x3E, 7, 1> VDS_BLK_BF_EN;                     // When this bit is 1, final composite blank (dis_hb|dis_vb)
                                                                      // will cut the garbage data in blanking interval


    typedef UReg<0x03, 0x3F, 0, 8> VDS_UV_BLK_VAL;                    // UV blanking amplitude value control bit[7:0] This filed
                                                                      // indicates the amplitude value of UV in blanking interval,
                                                                      // the highest bit of this programmable register is sign bit


    typedef UReg<0x03, 0x40, 0, 1> VDS_1ST_INT_BYPS;                  // st The 1 stage interpolation bypass control, active high
                                                                      // When this bit is 1, the 1st stage interpolation (in YUV
                                                                      // domain) will be bypassed, Y use tap19, and UV use tap7

    typedef UReg<0x03, 0x40, 1, 1> VDS_2ND_INT_BYPS;                  // nd The 2 stage interpolation bypass control, active high
                                                                      // When this bit is 1, the 2nd stage interpolation (in RGB
                                                                      // domain) will be bypassed, all RGB use tap11

    typedef UReg<0x03, 0x40, 2, 1> VDS_IN_DREG_BYPS;                  // Input data bypass the negedge trigger control, active
                                                                      // high When this bit is 0, input data will triggered by
                                                                      // falling edge clock

    typedef UReg<0x03, 0x40, 4, 2> VDS_SVM_V4CLK_DELAY;               // This field define the SVM delay from 1 to 4 V2CLKs
                                                                      // VDS_SVM_V4CLK_DELAY SVM delay 0 0 1 0 1 2


    typedef UReg<0x03, 0x41, 0, 10> VDS_PK_LINE_BUF_SP;               // Line buffer for 2D peaking write reset position control
                                                                      // bit [7:0] VDS_PK_LINE_BUF_SP This field contains the
                                                                      // write reset position of the line buffer, this position is
                                                                      // also [7:0] the write start position of the buffer


    typedef UReg<0x03, 0x42, 6, 1> VDS_PK_RAM_BYPS;                   // When this bit is 1, data will bypass the line buffer


    typedef UReg<0x03, 0x43, 0, 1> VDS_PK_VL_HL_SEL;                  // 2D peaking vertical low-pass signal select the horizontal
                                                                      // split filter control low-pass filter select, 1 for tap3
                                                                      // and 0 for tap5

    typedef UReg<0x03, 0x43, 1, 1> VDS_PK_VL_HH_SEL;                  // 2D peaking vertical low-pass signal select the horizontal
                                                                      // split filter control for high-pass filter select, 1 for
                                                                      // tap3 and 0 for tap5

    typedef UReg<0x03, 0x43, 2, 1> VDS_PK_VH_HL_SEL;                  // 2D peaking vertical high-pass signal select the
                                                                      // horizontal split filter control high-pass filter select,
                                                                      // 1 for tap3 and 0 for tap5. 2D peaking vertical high-pass
                                                                      // signal select the horizontal split filter

    typedef UReg<0x03, 0x43, 3, 1> VDS_PK_VH_HH_SEL;                  // control


    typedef UReg<0x03, 0x44, 0, 3> VDS_PK_LB_CORE;                    // 2D peaking vertical low-pass horizontal band-pass signal
                                                                      // coring level Vertical low-pass and horizontal band-pass
                                                                      // signal larger than this coring level

    typedef UReg<0x03, 0x44, 3, 5> VDS_PK_LB_CMP;                     // will remain unchanged, otherwise it will be cut to 0. 2D
                                                                      // peaking vertical low-pass horizontal band-pass signal
                                                                      // threshold level Vertical low-pass and horizontal band-
                                                                      // pass signal larger than this coring level will remain
                                                                      // unchanged, otherwise the gain will added on it


    typedef UReg<0x03, 0x45, 0, 6> VDS_PK_LB_GAIN;                    // 2D peaking vertical low-pass horizontal band-pass signal
                                                                      // gain control


    typedef UReg<0x03, 0x46, 0, 3> VDS_PK_LH_CORE;                    // 2D peaking vertical low-pass horizontal high-pass signal
                                                                      // coring level Vertical low-pass and horizontal high-pass
                                                                      // signal larger than this coring level will

    typedef UReg<0x03, 0x46, 3, 5> VDS_PK_LH_CMP;                     // remain unchanged, otherwise it will be cut to 0. 2D
                                                                      // peaking vertical low-pass horizontal high-pass signal
                                                                      // threshold level Vertical low-pass and horizontal high-
                                                                      // pass signal larger than this coring level will remain
                                                                      // unchanged, otherwise the gain will added on it


    typedef UReg<0x03, 0x47, 0, 6> VDS_PK_LH_GAIN;                    // 2D peaking vertical low-pass horizontal high-pass signal
                                                                      // gain control


    typedef UReg<0x03, 0x48, 0, 3> VDS_PK_HL_CORE;                    // 2D peaking vertical high-pass horizontal low-pass signal
                                                                      // coring level Vertical high-pass and horizontal low-pass
                                                                      // signal larger than this coring level will

    typedef UReg<0x03, 0x48, 3, 5> VDS_PK_HL_CMP;                     // remain unchanged, otherwise it will be cut to 0. 2D
                                                                      // peaking vertical high-pass horizontal low-pass signal
                                                                      // threshold level Vertical high-pass and horizontal low-
                                                                      // pass signal larger than this coring level will remain
                                                                      // unchanged, otherwise the gain will added on it


    typedef UReg<0x03, 0x49, 0, 6> VDS_PK_HL_GAIN;                    // 2D peaking vertical high-pass horizontal low-pass signal
                                                                      // gain control


    typedef UReg<0x03, 0x4A, 0, 3> VDS_PK_HB_CORE;                    // 2D peaking vertical high-pass horizontal band-pass signal
                                                                      // coring level Vertical high-pass and horizontal band-pass
                                                                      // signal larger than this coring level

    typedef UReg<0x03, 0x4A, 3, 5> VDS_PK_HB_CMP;                     // will remain unchanged, otherwise it will be cut to 0. 2D
                                                                      // peaking vertical high-pass horizontal band-pass signal
                                                                      // threshold level Vertical high-pass and horizontal band-
                                                                      // pass signal larger than this coring level will remain
                                                                      // unchanged, otherwise the gain will added on it


    typedef UReg<0x03, 0x4B, 0, 6> VDS_PK_HB_GAIN;                    // 2D peaking vertical high-pass horizontal band-pass signal
                                                                      // gain control


    typedef UReg<0x03, 0x4C, 0, 3> VDS_PK_HH_CORE;                    // 2D peaking vertical high-pass horizontal high-pass signal
                                                                      // coring level Vertical high-pass and horizontal high-pass
                                                                      // signal larger than this coring level

    typedef UReg<0x03, 0x4C, 3, 5> VDS_PK_HH_CMP;                     // will remain unchanged, otherwise it will be cut to 0. 2D
                                                                      // peaking vertical high-pass horizontal high-pass signal
                                                                      // threshold level Vertical high-pass and horizontal high-
                                                                      // pass signal larger than this coring level will remain
                                                                      // unchanged, otherwise the gain will added on it


    typedef UReg<0x03, 0x4D, 0, 6> VDS_PK_HH_GAIN;                    // 2D peaking vertical high-pass horizontal high-pass signal
                                                                      // gain control


    typedef UReg<0x03, 0x4E, 0, 1> VDS_PK_Y_H_BYPS;                   // Y horizontal peaking bypass control, active high When
                                                                      // this bit is 1, Y horizontal peaking will be bypassed

    typedef UReg<0x03, 0x4E, 1, 1> VDS_PK_Y_V_BYPS;                   // Y vertical peaking bypass control, active high When this
                                                                      // bit is 1, Y vertical peaking will be bypassed

    typedef UReg<0x03, 0x4E, 3, 1> VDS_C_VPK_BYPS;                    // UV vertical peaking bypass control, active high When this
                                                                      // bit is 1, UV vertical peaking will be bypassed

    typedef UReg<0x03, 0x4E, 4, 3> VDS_C_VPK_CORE;                    // UV vertical peaking coring level UV vertical high-pass
                                                                      // signal larger than this coring level will remain
                                                                      // unchanged


    typedef UReg<0x03, 0x4F, 0, 6> VDS_C_VPK_GAIN;                    // UV vertical peaking gain control bit [5:0]


    typedef UReg<0x03, 0x50, 0, 4> VDS_TEST_BUS_SEL;                  // Test out select control bit [3:0] This register is used
                                                                      // to select internal status bus to test bus

    typedef UReg<0x03, 0x50, 4, 1> VDS_TEST_EN;                       // Test enable, active high This bit is the test bus out
                                                                      // enable bit, when this bit is 1, the test bus can output

    typedef UReg<0x03, 0x50, 5, 1> VDS_DO_UV_DEC_BYPS;                // the internal status, and otherwise, the test bus is
                                                                      // 0Xaaaa. 16-bit digital out UV decimation filter bypass
                                                                      // control, active high When this bit is 1, 16-bit 422 YUV
                                                                      // digital out UV decimation will be bypassed. 16-bit
                                                                      // digital out UV flip control

    typedef UReg<0x03, 0x50, 6, 1> VDS_DO_UVSEL_FLIP;                 // When this bit is 1, 16-bit 422 YUV digital out UV
                                                                      // position will be flipped

    typedef UReg<0x03, 0x50, 7, 1> VDS_DO_16B_EN;                     // 16-bit digital out (422 format yuv) enable When this bit
                                                                      // is 1, digital out is 16-bit 422 YUV format; When it is 0,
                                                                      // digital out is 24-bit


    typedef UReg<0x03, 0x51, 0, 11> VDS_GLB_NOISE;                    // Global still detection threshold value control bit [7:0]
                                                                      // This field contains the global noise threshold value. If
                                                                      // the total difference of two frame less than this
                                                                      // programmable value, the picture is taken as still,
                                                                      // otherwise, the picture is taken as moving picture


    typedef UReg<0x03, 0x52, 4, 1> VDS_NR_Y_BYPASS;                   // When this bit is 1, Y data will bypass the noise
                                                                      // reduction process

    typedef UReg<0x03, 0x52, 5, 1> VDS_NR_C_BYPASS;                   // UV bypass the noise reduction process control When this
                                                                      // bit is 1, UV data will bypass the noise reduction process

    typedef UReg<0x03, 0x52, 6, 1> VDS_NR_DIF_LPF5_BYPS;              // Bypass control of the tap5 low-pass filter used for Y
                                                                      // difference between two frames

    typedef UReg<0x03, 0x52, 7, 1> VDS_NR_MI_TH_EN;                   // When this bit is 1, Y difference data will bypass the
                                                                      // tap5 low-pass filter Noise reduction threshold control
                                                                      // enable This bit will enable the threshold control, active
                                                                      // high


    typedef UReg<0x03, 0x53, 0, 7> VDS_NR_MI_OFFSET;                  // Motion index offset control bit [6:0] The offset control
                                                                      // for motion index generation

    typedef UReg<0x03, 0x53, 7, 1> VDS_NR_MIG_USER_EN;                // When ds_mig_en is 1, ds_mig_offset[3:0] is user-defined
                                                                      // motion index. Motion index generation user mode enable
                                                                      // When this bit is 1, the motion index generation will use
                                                                      // nr_mig_offt[3:0] as Motion index


    typedef UReg<0x03, 0x54, 0, 4> VDS_NR_MI_GAIN;                    // Motion index generation gain control bit [3:0] Motion
                                                                      // index generation gain control, its range is (0~8)*2

    typedef UReg<0x03, 0x54, 4, 4> VDS_NR_STILL_GAIN;                 // Motion index generation gain control bit [3:0] for still
                                                                      // picture When picture is still, this field contains the
                                                                      // motion index generation gain, its range is (0~8)*2


    typedef UReg<0x03, 0x55, 0, 4> VDS_NR_MI_THRESH;                  // Noise reduction threshold value bit [3:0] Noise-reduction
                                                                      // threshold value. When MI is smaller than the threshold
                                                                      // value

    typedef UReg<0x03, 0x55, 4, 1> VDS_NR_EN_H_NOISY;                 // the noise reduction is enabled. Otherwise it is not. High
                                                                      // noisy picture index enable, active high Enable high noisy
                                                                      // index from de-interlacer, it means the picture’s noise is
                                                                      // very

    typedef UReg<0x03, 0x55, 6, 1> VDS_NR_EN_GLB_STILL;               // This bit enables the global still signal

    typedef UReg<0x03, 0x55, 7, 1> VDS_NR_GLB_STILL_MENU;             // Part of VDS_NR_GLB_STILL_MENU, which RD-5725-1.1
                                                                      // documents as one 1-bit block at s3_55 rather than field
                                                                      // by field.


    typedef UReg<0x03, 0x56, 0, 7> VDS_NR_NOISY_OFFSET;               // Motion index generation offset control bit [6:0] for high
                                                                      // noisy picture When the picture is high noisy picture,
                                                                      // this field contains the offset control for

    typedef UReg<0x03, 0x56, 7, 1> VDS_W_LEV_BYPS;                    // motion index generation. White level expansion bypass
                                                                      // control, active high When this bit is 1, Y don’t do white
                                                                      // level expansion


    typedef UReg<0x03, 0x57, 0, 8> VDS_W_LEV;                         // White level expansion level control bit[7:0] This field
                                                                      // defines the white level expansion threshold level value;
                                                                      // data less than this level will have no white level
                                                                      // expansion process


    typedef UReg<0x03, 0x58, 0, 8> VDS_WLEV_GAIN;                     // White level expansion gain control bit[7:0] This field
                                                                      // defines the white level expansion threshold level value;
                                                                      // data less than this level will have no white level
                                                                      // expansion process


    typedef UReg<0x03, 0x59, 0, 8> VDS_NS_U_CENTER;                   // Non-linear saturation center position U value control bit
                                                                      // [7:0] This field contains the non-linear saturation
                                                                      // center position U value, the value is 2’s


    typedef UReg<0x03, 0x5A, 0, 8> VDS_NS_V_CENTER;                   // Non-linear saturation center position V value control bit
                                                                      // [7:0] This field contains the non-linear saturation
                                                                      // center position V value, the value is 2’s


    typedef UReg<0x03, 0x5B, 0, 7> VDS_NS_U_GAIN;                     // Non-linear saturation U gain control bit [6:0] This field
                                                                      // contains the U gain control for U component in the area
                                                                      // which should

    typedef UReg<0x03, 0x5B, 7, 15> VDS_NS_SQUARE_RAD;                // 15 bits, and the LOW one lives in the HIGH bit of the LOW
                                                                      // register: s3_5b[7] is field bit 0, s3_5c[7:0] are bits
                                                                      // [8:1], s3_5d[5:0] are bits [14:9]. UReg's little-endian
                                                                      // concatenation addresses that correctly from lo=7 -- it is
                                                                      // only unusual, not special. do non-linear saturation, its
                                                                      // range is (0~1)*128. Non-linear saturation range control
                                                                      // bit [0] Non-linear saturation only did When
                                                                      // (u-u_center)^2 + (v-v_center)^2 less than this
                                                                      // programmable range value


    typedef UReg<0x03, 0x5D, 6, 8> VDS_NS_Y_HIGH_TH;                  // this programmable range value. Non-linear saturation Y
                                                                      // high threshold control bit [1:0] This filed defines the Y
                                                                      // high threshold value for non-linear saturation, when y
                                                                      // detect enable (60[3]=1), if y larger than this
                                                                      // programmable value, no non-linear did


    typedef UReg<0x03, 0x5E, 6, 7> VDS_NS_V_GAIN;                     // Non-linear saturation V gain control bit [1:0] This field
                                                                      // contains the V gain control for V component in the area
                                                                      // which should do non-linear saturation, its range is
                                                                      // (0~1)*128


    typedef UReg<0x03, 0x5F, 5, 5> VDS_NS_Y_LOW_TH;                   // do non-linear saturation, its range is (0~1)*128. Non-
                                                                      // linear saturation Y low threshold control bit [2:0] This
                                                                      // filed defines the Y low threshold value for non-linear
                                                                      // saturation, when y detect enable (60[3]=1), if y less
                                                                      // than this programmable value, no non-linear did


    typedef UReg<0x03, 0x60, 2, 1> VDS_NS_BYPS;                       // Non-linear saturation bypass control, active high When
                                                                      // this bit is 1, the process non-linear saturation will be
                                                                      // bypassed

    typedef UReg<0x03, 0x60, 3, 1> VDS_NS_Y_ACTIVE_EN;                // Non-linear saturation Y detect enable, active high When
                                                                      // this bit is 1, the process non-linear saturation only
                                                                      // done when the Y larger

    typedef UReg<0x03, 0x60, 4, 10> VDS_C1_TAG_LOW_SLOPE;             // than the value ns_y_low_th and less than the value
                                                                      // ns_y_high_th. Red enhance angle tan value low threshold
                                                                      // value control bit [3:0] This filed contains the low
                                                                      // threshold value for red enhance angle tan value, when the
                                                                      // input UV angle tan value less than this programmable
                                                                      // value, no enhancement did


    typedef UReg<0x03, 0x61, 6, 10> VDS_C1_TAG_HIGH_SLOPE;            // Red enhance angle tan value high threshold value control
                                                                      // bit [1:0] This filed contains the high threshold value
                                                                      // for red enhance angle tan value, when the input UV angle
                                                                      // tan value larger than this programmable value, no
                                                                      // enhancement did


    typedef UReg<0x03, 0x63, 0, 4> VDS_C1_GAIN;                       // Red enhance gain control bit [3:0] This field contains
                                                                      // the gain control for red enhance, its range is (0~1)*16

    typedef UReg<0x03, 0x63, 4, 8> VDS_C1_U_LOW;                      // Red enhance U low threshold value control bit [3:0] This
                                                                      // field contains the low threshold value for U component,
                                                                      // if input U less then this programmable value, no
                                                                      // enhancement did


    typedef UReg<0x03, 0x64, 4, 8> VDS_C1_U_HIGH;                     // this programmable value, no enhancement did. Red enhance
                                                                      // U high threshold value control bit [3:0] This field
                                                                      // contains the high threshold value for U component, if
                                                                      // input U larger then this programmable value, no
                                                                      // enhancement did


    typedef UReg<0x03, 0x65, 4, 1> VDS_C1_BYPS;                       // then this programmable value, no enhancement did. Red
                                                                      // enhance bypass control, active high When this bit is 1,
                                                                      // red enhancement will be bypassed. Red enhance Y threshold
                                                                      // value control bit [2:0]

    typedef UReg<0x03, 0x65, 5, 8> VDS_C1_Y_THRESH;                   // This field contains the Y threshold for red enhancement,
                                                                      // when input Y larger than this programmable value, no
                                                                      // enhancement did


    typedef UReg<0x03, 0x66, 5, 10> VDS_C2_TAG_LOW_SLOPE;


    typedef UReg<0x03, 0x67, 7, 10> VDS_C2_TAG_HIGH_SLOPE;            // enhancement did. Green enhance angle tan value high
                                                                      // threshold value control bit [0] This filed contains the
                                                                      // high threshold value for green enhance angle tan value,
                                                                      // when the input UV angle tan value larger than this
                                                                      // programmable value, no enhancement did


    typedef UReg<0x03, 0x69, 1, 4> VDS_C2_GAIN;                       // Color enhance gain control bit [3:0] This field contains
                                                                      // the gain control for green enhance, its range is (0~1)*16

    typedef UReg<0x03, 0x69, 5, 8> VDS_C2_U_LOW;                      // Green enhance U low threshold value control bit [2:0]
                                                                      // This field contains the low threshold value for U
                                                                      // component, if input U less then this programmable value,
                                                                      // no enhancement did


    typedef UReg<0x03, 0x6A, 5, 8> VDS_C2_U_HIGH;                     // this programmable value, no enhancement did. Green
                                                                      // enhance U high threshold value control bit [2:0] This
                                                                      // field contains the high threshold value for U component,
                                                                      // if input U larger then this programmable value, no
                                                                      // enhancement did


    typedef UReg<0x03, 0x6B, 5, 1> VDS_C2_BYPS;                       // then this programmable value, no enhancement did. Green
                                                                      // enhance bypass control When this bit is 1, color
                                                                      // enhancement will be bypassed. Green enhance Y threshold
                                                                      // value control bit [1:0]

    typedef UReg<0x03, 0x6B, 6, 8> VDS_C2_Y_THRESH;                   // Green enhance Y threshold value control bit [7:2] This
                                                                      // field contains the Y threshold for green enhancement,
                                                                      // when input Y larger


    // The four VDS_EXT_* windows drive the HBOUT/VBOUT pins, which PAD_BLK_OUT_ENZ
    // disables, and VDS_SYNC_IN_SEL 0 leaves no internal consumer either. Nothing
    // in the firmware writes or reads them. docs/firmware-geometry-engine.md
    typedef UReg<0x03, 0x6D, 0, 12> VDS_EXT_HB_ST;                    // External used horizontal blanking start position control
                                                                      // bit [7:0] This field is used to program horizontal
                                                                      // blanking start position, this blanking is for external
                                                                      // used


    typedef UReg<0x03, 0x6E, 4, 12> VDS_EXT_HB_SP;                    // for external used. External used horizontal blanking stop
                                                                      // position control bit [3:0] This field is used to program
                                                                      // horizontal blanking stop position, this blanking is for
                                                                      // external used


    typedef UReg<0x03, 0x70, 0, 11> VDS_EXT_VB_ST;                    // External used vertical blanking start position control
                                                                      // bit [7:0] This field is used to program vertical blanking
                                                                      // start position, this blanking is for external used


    typedef UReg<0x03, 0x71, 4, 11> VDS_EXT_VB_SP;                    // External used vertical blanking stop position control bit
                                                                      // [10:4] This field is used to program vertical blanking
                                                                      // stop position, this blanking is for


    typedef UReg<0x03, 0x72, 7, 1> VDS_SYNC_IN_SEL;                   // external used. VDS module input sync selection control
                                                                      // When this bit is 1, the sync to VDS module is from
                                                                      // external (out of the CHIP); When this bit is 0, the sync
                                                                      // to VDS module is from IF module


    typedef UReg<0x03, 0x73, 0, 3> VDS_BLUE_RANGE;                    // Blue extend range control bit [2:0] This field defines
                                                                      // the range for blue extend. VDS_BLUE_RANGE [2:0] Real
                                                                      // range 0 0 0 1 0 0 1 2 0 1 0 4 0 1 1 8 1 0 0 16 1 0 1 32 1
                                                                      // 1 0 64

    typedef UReg<0x03, 0x73, 3, 1> VDS_BLUE_BYPS;                     // 1 1 1 128 Blue extend bypass control, active high When
                                                                      // this bit is 1, the blue extend process will be bypassed
                                                                      // Blue extend U gain control bit [3:0]

    typedef UReg<0x03, 0x73, 4, 4> VDS_BLUE_UGAIN;                    // This field defines the U gain for U component in the area
                                                                      // which should do blue extend, its range is (0~1)*16


    typedef UReg<0x03, 0x74, 0, 4> VDS_BLUE_VGAIN;                    // Blue extend V gain control bit [3:0] This field defines
                                                                      // the V gain for V component in the area which should do
                                                                      // blue

    typedef UReg<0x03, 0x74, 4, 4> VDS_BLUE_Y_LEV;                    // extend, its range is (0~1)*16. Blue extend Y level
                                                                      // threshold control bit [3:0] This field defines the Y
                                                                      // threshold value of blue extend, the real level in the
                                                                      // circuit is 16*blue_y_th + 15, the blue extend process
                                                                      // done only when Y value larger than this level (real
                                                                      // level)

    // Every static register of this subsystem, in address order.
    static void init();

    // The picture-quality controls. Each names what the user asked for, not the
    // bypass bit that carries it: the registers are BYPS, so wanting a filter
    // clears one. Every writer of these four registers goes through here.
    static void setLineFilter(bool wanted);
    static void setPeaking(bool wanted);
    static void setStepResponse(bool wanted);
    static void setSixTapFilter(bool wanted);

    // Output timing that ignores the input vertical sync. RD-5725-1.1 gives the
    // two bits as one table: VDS_FLOCK_EN with VDS_SYNC_EN, 0/0 free run.
    static void applyFreeRunTiming();

    // How the output frames follow the input frames: the lock period, the
    // repeat number, and which vertical size each frame takes.
    static void applyFrameSequencing();

    // Input data taken on the falling edge of the clock.
    static void clockInputOnFallingEdge();
};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_PROCESSOR_H
