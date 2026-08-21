#ifndef TV5725_INPUT_FORMATTER_H
#define TV5725_INPUT_FORMATTER_H

#include "Tv5725.h"

#include <stdint.h>

namespace Tv5725 {

// The input formatter: what the chip does to a captured line before the scaler
// sees it.
//
// The static half: IF_SEL24BIT = 1 takes the 24-bit input path, IF_SEL_HSCALE =
// 1 puts the horizontal scaler in circuit, IF_SEL_ADC_SYNC = 1 takes sync from
// the ADC rather than the digital port, and the piecewise H-sync rate correction
// is off. What moves per mode -- IF_HB_*, IF_HBIN_SP, IF_LINE_SP -- is the
// engine's, which computes the capture window rather than transcribing it.
//
// IF_LD_ST shares s1_0c with IF_LD_RAM_BYPS (bit 0) and IF_INI_ST (bits 7-5),
// both written by doPostPresetLoadSteps(). Three owners in one byte is safe only
// because every access is read-modify-write; test_input_formatter.cpp asserts
// the neighbours survive rather than assuming it.
class InputFormatter {
public:
    typedef UReg<0x01, 0x00, 0, 1> IF_IN_DREG_BYPS;                   // Input pipe by pass Use the falling or rising edge of
                                                                      // clock to get the input data. 0: Clock input data on the
                                                                      // falling edge of ICLK. 1: Clock input date on the rising
                                                                      // edge of ICLK

    typedef UReg<0x01, 0x00, 1, 1> IF_MATRIX_BYPS;                    // Rgb2yuv matrix bypass If source is yuv24bit, bypass the
                                                                      // rgb2yuv matrix. 0:source is 24bit RGB. Do rgb2yuv. 1:
                                                                      // data bypass

    typedef UReg<0x01, 0x00, 2, 1> IF_UV_REVERT;                      // 8bit to 16bit convert Y/UV flip control If input is 8bit
                                                                      // data, when it convert to 16bit, this bit control Y and UV
                                                                      // order: 0: Keep the designed order 1: Flip the Y and UV
                                                                      // order

    typedef UReg<0x01, 0x00, 3, 1> IF_SEL_656;                        // Select CCIR656 data If input data is 8bit CCIR656 mode,
                                                                      // choose the 656 data path. 0: input is CCIR 601 mode.
                                                                      // Choose the CCIR601mode timing. 1: input is CCIR 656 mode.
                                                                      // Choose the CCIR656 mode timing

    typedef UReg<0x01, 0x00, 4, 1> IF_SEL16BIT;                       // Select 16bit data If source data is 16bit. Choose the
                                                                      // 16bits data path. Use in conjunction with register
                                                                      // sel_24bit to choose the input data format. Sel_16bit
                                                                      // Sel_24bit IF_SEL16BIT 8bit 656/601 input 0 0

    typedef UReg<0x01, 0x00, 5, 1> IF_VS_SEL;                         // 16bit 601 input 1 0 24bit yuv/rgb 601 input * 1 Vertical
                                                                      // sync select Choose the periodical or virtual vertical
                                                                      // timing. 0: choose the VCR mode timing generation. 1:
                                                                      // choose the normal mode timing generation. Select
                                                                      // progressive data Progressive mode. Choose the progressive
                                                                      // data

    typedef UReg<0x01, 0x00, 6, 1> IF_PRGRSV_CNTRL;                   // 0: source is interlaced

    typedef UReg<0x01, 0x00, 7, 1> IF_HS_FLIP;                        // 1: source is progressive. Horizontal sync flip control
                                                                      // Control the horizontal sync output from CCIR process 0:
                                                                      // keep the original horizontal sync. 1: flip horizontal
                                                                      // sync

    typedef UReg<0x01, 0x01, 0, 1> IF_VS_FLIP;                        // Vertical sync flip control Control the vertical sync
                                                                      // output from CCIR process 0: keep original vertical sync.
                                                                      // 1: flip vertical sync

    typedef UReg<0x01, 0x01, 1, 1> IF_UV_FLIP;                        // YUV 422to444 UV flip control Control the U and V order in
                                                                      // yuv422to444 conversion. 0: keep original U and V order.
                                                                      // 1: exchange the U and V order

    typedef UReg<0x01, 0x01, 2, 1> IF_U_DELAY;                        // U data select in YUV 422to444 conversion Select original
                                                                      // U data or 1-clock delayed U data, so that it can align
                                                                      // with V data. 0: select original U data after dmux. 1:
                                                                      // select 1-clock delayed U data after dmux

    typedef UReg<0x01, 0x01, 3, 1> IF_V_DELAY;                        // V data select in YUV 422to444 conversion Select original
                                                                      // V data or 1-clock delayed V data, so that it can align
                                                                      // with U data. 0: select original V data after dmux. 1:
                                                                      // select 1-clock delayed V data after dmux

    typedef UReg<0x01, 0x01, 4, 1> IF_TAP6_BYPS;                      // Tap6 interpolator bypass control in YUV 422to444
                                                                      // conversion Select the data if pass the tap6 interpolator
                                                                      // or not

    typedef UReg<0x01, 0x01, 5, 2> IF_Y_DELAY;                        // Part of IF_Y_DELAY, which RD-5725-1.1 documents as one
                                                                      // 2-bit block at s1_01 rather than field by field.

    typedef UReg<0x01, 0x01, 7, 1> IF_SEL24BIT;                       // If input source is 24bit data, choose the 24bit data path

    typedef UReg<0x01, 0x02, 0, 1> IF_SEL_WEN;                        // Select the write enable for line double If the input is
                                                                      // HD source, this bit will be set to 1. 0: if the source is
                                                                      // SD data. 1: if the source is HD data

    typedef UReg<0x01, 0x02, 1, 1> IF_HS_SEL_LPF;                     // Low pass filter or interpolator selection The low pass
                                                                      // filter and interpolator data path is combined together.
                                                                      // 0: select interpolator data path. 1: select low pass
                                                                      // filter data path

    typedef UReg<0x01, 0x02, 2, 1> IF_HS_INT_LPF_BYPS;                // Combined INT and LPF data path bypass control If the data
                                                                      // can’t do horizontal scaling-down, bypass the INT/LPF data
                                                                      // path. 0: select the INT/LPF data path. 1: bypass the
                                                                      // INT/LPF data path

    typedef UReg<0x01, 0x02, 3, 1> IF_HS_PSHIFT_BYPS;                 // Phase adjustment bypass control If the data can’t do
                                                                      // phase adjustment, this bit should be set to 1. 0: select
                                                                      // phase adjustment data path. 1: bypass phase adjustment

    typedef UReg<0x01, 0x02, 4, 1> IF_HS_TAP11_BYPS;                  // Tap11 LPF bypass control in YUV444to422 conversion Select
                                                                      // the data if pass the tap11 LPF or not. 0: the data will
                                                                      // pass the tap11 low pass filter. 1: the data will not pass
                                                                      // the tap11 low pass filter

    typedef UReg<0x01, 0x02, 5, 2> IF_HS_Y_PDELAY;                    // Y data pipes control in YUV444to422 conversion Control
                                                                      // the Y data pipe delay, so that it can align with UV.
                                                                      // IF_HS_Y_DELAY Y data delay pipes 00 1 01 2 10 3

    typedef UReg<0x01, 0x02, 7, 1> IF_HS_UV_SIGN2UNSIGN;              // 11 4 UV data select If UV is signed, select the unsigned
                                                                      // UV data 0: select the original UV 1: select the UV after
                                                                      // sign processing

    typedef UReg<0x01, 0x03, 0, 8> IF_HS_RATE_SEG0;                   // Horizontal non-linear scaling-down 1st segment DDA
                                                                      // increment [11:4] (total 12 bits) The entire segment share
                                                                      // the lowest 4bit, that is to say, the whole scale ration
                                                                      // is hscale = {hscale0, hscale_low}. Assume the scaling
                                                                      // ratio is n/m, then the value should be 4095x(m-n)/n

    typedef UReg<0x01, 0x04, 0, 8> IF_HS_RATE_SEG1;                   // Horizontal non-linear scaling-down 2nd segment DDA
                                                                      // increment [11:4] (total 12 bits)

    typedef UReg<0x01, 0x05, 0, 8> IF_HS_RATE_SEG2;                   // Horizontal non-linear scaling-down 3rd segment DDA
                                                                      // increment [11:4] (total 12 bits)

    typedef UReg<0x01, 0x06, 0, 8> IF_HS_RATE_SEG3;                   // Horizontal non-linear scaling-down 4th segment DDA
                                                                      // increment [11:4] (total 12 bits)

    typedef UReg<0x01, 0x07, 0, 8> IF_HS_RATE_SEG4;                   // Horizontal non-linear scaling-down 5th segment DDA
                                                                      // increment [11:4] (total 12 bits)

    typedef UReg<0x01, 0x08, 0, 8> IF_HS_RATE_SEG5;                   // Horizontal non-linear scaling-down 6th segment DDA
                                                                      // increment [11:4] (total 12 bits)

    typedef UReg<0x01, 0x09, 0, 8> IF_HS_RATE_SEG6;                   // Horizontal non-linear scaling-down 7th segment DDA
                                                                      // increment [11:4] (total 12 bits)

    typedef UReg<0x01, 0x0A, 0, 8> IF_HS_RATE_SEG7;                   // Horizontal non-linear scaling-down 8th segment DDA
                                                                      // increment [11:4] (total 12 bits)

    typedef UReg<0x01, 0x0B, 0, 4> IF_HS_RATE_LOW;                    // Horizontal non-linear scaling-down DDA increment shared
                                                                      // lowest 4 bits [3:0] (total 12 bits)

    typedef UReg<0x01, 0x0B, 4, 2> IF_HS_DEC_FACTOR;                  // Horizontal non-linear scaling-down factor select If the
                                                                      // scaling ratio is less than ½, use it and DDA to generate
                                                                      // the we and phase 00: scaling-ratio is more than ½. 01:
                                                                      // scaling-ratio is less than ½

    typedef UReg<0x01, 0x0B, 6, 1> IF_SEL_HSCALE;                     // 10: scaling-ratio is less than ¼. Select the data path
                                                                      // after horizontal scaling-down If the data have do
                                                                      // scaling-down, this bit should be open. 0: select the data
                                                                      // and write enable from CCIR to line double. 1: select the
                                                                      // scaling-down data and write enable to line double. Line
                                                                      // double read reset select

    typedef UReg<0x01, 0x0B, 7, 1> IF_LD_SEL_PROV;                    // If source is progressive data, choose the related
                                                                      // progressive timing as read reset timing. 0: select read
                                                                      // reset timing of interlace data. 1: select read reset
                                                                      // timing of progressive data

    typedef UReg<0x01, 0x0C, 0, 1> IF_LD_RAM_BYPS;                    // Line double bypass control If the interlace data can’t do
                                                                      // line double, if the progressive data can’t do scaling-
                                                                      // down, line double FIFO should be bypass. 0: select
                                                                      // interlace line double data from FIFO

    typedef UReg<0x01, 0x0C, 1, 4> IF_LD_ST;                          // 1: bypass line double FIFO. Line double write reset
                                                                      // generation start position If the internal counter equals
                                                                      // the defined value the write reset will be high pulse.
                                                                      // Initial position

    typedef UReg<0x01, 0x0C, 5, 11> IF_INI_ST;                        // Initial position Start position indicator of vertical
                                                                      // blanking. For the internal line_counter, the detail
                                                                      // pixel’s shift that the line_counter count compare to the
                                                                      // horizontal sync

    typedef UReg<0x01, 0x0E, 0, 11> IF_HSYNC_RST;                     // Total pixel number per line Use to generate progressive
                                                                      // timing if input is interlace data [7:0]

    typedef UReg<0x01, 0x10, 0, 11> IF_HB_ST;                         // Horizontal blanking start position (set 0) Horizontal
                                                                      // blanking (set 0) start position [7:0]

    typedef UReg<0x01, 0x12, 0, 11> IF_HB_SP;                         // Horizontal blanking stop position (set 0) Horizontal
                                                                      // blanking (set 0) stop position [7:0]

    typedef UReg<0x01, 0x14, 0, 11> IF_HB_ST1;                        // Horizontal blanking start position (set 1) Horizontal
                                                                      // blanking (set 1) start position [7:0]

    typedef UReg<0x01, 0x16, 0, 11> IF_HB_SP1;                        // Horizontal blanking stop position (set 1) Horizontal
                                                                      // blanking (set 1) stop position [7:0]

    typedef UReg<0x01, 0x18, 0, 11> IF_HB_ST2;                        // Horizontal blanking start position (set 2) Horizontal
                                                                      // blanking (set 2) start position [7:0]

    typedef UReg<0x01, 0x1A, 0, 11> IF_HB_SP2;                        // Horizontal blanking stop position (set 2) Horizontal
                                                                      // blanking (set 2) stop position [7:0]

    typedef UReg<0x01, 0x1C, 0, 11> IF_VB_ST;                         // Vertical blanking start position Vertical blanking start
                                                                      // position [7:0]

    typedef UReg<0x01, 0x1E, 0, 11> IF_VB_SP;                         // Vertical blanking stop position Vertical blanking stop
                                                                      // position [7:0]

    typedef UReg<0x01, 0x20, 0, 12> IF_LINE_ST;                       // Line signal start position Progressive line start
                                                                      // position

    typedef UReg<0x01, 0x22, 0, 12> IF_LINE_SP;                       // Line signal stop position Progressive line stop position

    typedef UReg<0x01, 0x24, 0, 12> IF_HBIN_ST;                       // Horizontal blank for scale down start position Horizontal
                                                                      // blank for scale down line reset start position

    typedef UReg<0x01, 0x26, 0, 12> IF_HBIN_SP;                       // Horizontal blank for scale down stop position Horizontal
                                                                      // blank for scale down line reset stop position

    typedef UReg<0x01, 0x28, 1, 1> IF_LD_WRST_SEL;                    // Line double write reset select Select hbin/line write
                                                                      // reset 0: select line generated write reset 1: select hbin
                                                                      // generated write reset

    typedef UReg<0x01, 0x28, 2, 1> IF_SEL_ADC_SYNC;                   // ADC sync select Select ADC sync to data path

    typedef UReg<0x01, 0x28, 3, 1> IF_TEST_EN;                        // IF test bus control enable Enable test signal

    typedef UReg<0x01, 0x28, 4, 4> IF_TEST_SEL;                       // Test signals select bits. Select which signal to the test
                                                                      // bus

    typedef UReg<0x01, 0x29, 0, 1> IF_AUTO_OFST_EN;                   // Auto offset adjustment enable 1: enable

    typedef UReg<0x01, 0x29, 1, 1> IF_AUTO_OFST_PRD;                  // 0: disable Auto offset adjustment period control 1: by
                                                                      // frame

    typedef UReg<0x01, 0x2A, 0, 4> IF_AUTO_OFST_U_RANGE;              // U channel offset detection range

    typedef UReg<0x01, 0x2A, 4, 4> IF_AUTO_OFST_V_RANGE;

    // Every static register of this subsystem, in address order. Called from
    // the bring-up where a preset table would have been loaded.
    static void init();

    // The line counter, in IF units. SourceMeasurement decides the value off
    // the ADC divider; this block is where the register lives.
    static void writeLineCounter(uint16_t units);

    // Whether the line doubler is in the capture path.
    enum ScanMode {
        LineDoubled,   // 15 kHz source, doubled to the output line rate
        Progressive,   // already at line rate, doubler bypassed
    };

    // Put every register that decides the scan mode into one of the two states.
    static void applyScanMode(ScanMode mode);
};

}  // namespace Tv5725

#endif
