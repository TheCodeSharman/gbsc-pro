#ifndef TV5725_TV5725_H_
#define TV5725_TV5725_H_

#include "../../tw.h"

#define GBS_ADDR 0x17 // 7 bit GBS I2C Address

namespace Tv5725
{

namespace detail
{
    struct TVAttrs
    {
        // Segment register at 0xf0, no bit offset, 8 bits, initial value assumed invalid
        static const uint8_t SegByteOffset = 0xf0;
        static const uint8_t SegBitOffset = 0;
        static const uint8_t SegBitWidth = 8;
        static const uint8_t SegInitial = 0xff;
    };
} // namespace detail

typedef tw::SegmentedSlave<GBS_ADDR, detail::TVAttrs> Slave;

// Namespace scope, so a subsystem class can declare its own registers with it.
// Every field on this chip is unsigned; nothing needs the signed form.
template <uint8_t Seg, uint8_t ByteOffset, uint8_t BitOffset, uint8_t BitWidth>
using UReg = Slave::Register<Seg, ByteOffset, BitOffset, BitWidth, tw::Signage::UNSIGNED>;

// The chip: the segmented bus below, every register field above. Registers
// migrate out of here into the subsystem class that owns them, so what is left
// is whatever has no owner yet. docs/chip-initialisation.md.
class Tv5725 : public Slave
{
public:

// STATUS REGISTERS

    typedef UReg<0x00, 0x00, 0, 8> STATUS_00;                         // Part of IF_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 45-bit block at s0_00 rather than field by field.

    typedef UReg<0x00, 0x00, 0, 1> STATUS_IF_VT_OK;                   // Part of IF_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 45-bit block at s0_00 rather than field by field.

    typedef UReg<0x00, 0x00, 1, 1> STATUS_IF_HT_OK;                   // Part of IF_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 45-bit block at s0_00 rather than field by field.

    typedef UReg<0x00, 0x00, 2, 1> STATUS_IF_HVT_OK;                  // When =1, means input H/V timing are both stable
                                                                      // [datasheet: IF_STATUS_[2]]

    typedef UReg<0x00, 0x00, 3, 1> STATUS_IF_INP_NTSC_INT;            // When =1, means input is NTSC interlace (480i) source
                                                                      // [datasheet: IF_STATUS_[3]]

    typedef UReg<0x00, 0x00, 4, 1> STATUS_IF_INP_NTSC_PRG;            // When =1, means input is NTSC progressive (480P) source
                                                                      // [datasheet: IF_STATUS_[4]]

    typedef UReg<0x00, 0x00, 5, 1> STATUS_IF_INP_PAL_INT;             // When =1, means input is PAL interlace (576i) source
                                                                      // [datasheet: IF_STATUS_[5]]

    typedef UReg<0x00, 0x00, 6, 1> STATUS_IF_INP_PAL_PRG;             // When =1, means input is PAL progressive (576P) source
                                                                      // [datasheet: IF_STATUS_[6]]

    typedef UReg<0x00, 0x00, 7, 1> STATUS_IF_INP_SD;                  // When =1, means input is SD mode (480i, 480P, 576i, 576P)
                                                                      // [datasheet: IF_STATUS_[7]]


    typedef UReg<0x00, 0x01, 0, 1> STATUS_IF_INP_VGA60;               // When =1, means input is VGA (640x480) 60Hz mode
                                                                      // [datasheet: IF_STATUS_[8]]

    typedef UReg<0x00, 0x01, 1, 1> STATUS_IF_INP_VGA75;               // When =1, means input is VGA (640x480) 75Hz mode
                                                                      // [datasheet: IF_STATUS_[9]]

    typedef UReg<0x00, 0x01, 2, 1> STATUS_IF_INP_VGA86;               // When =1, means input is VGA (640x480) 85Hz mode
                                                                      // [datasheet: IF_STATUS_[10]]

    typedef UReg<0x00, 0x01, 3, 1> STATUS_IF_INP_VGA;                 // When =1, means input is VGA (640x480) source, include
                                                                      // 60Hz/75Hz/85Hz [datasheet: IF_STATUS_[11]]

    typedef UReg<0x00, 0x01, 4, 1> STATUS_IF_INP_SVGA60;              // When =1, means input is SVGA (800x600) 60Hz mode
                                                                      // [datasheet: IF_STATUS_[12]]

    typedef UReg<0x00, 0x01, 5, 1> STATUS_IF_INP_SVGA75;              // When =1, means input is SVGA (800x600) 75Hz mode
                                                                      // [datasheet: IF_STATUS_[13]]

    typedef UReg<0x00, 0x01, 6, 1> STATUS_IF_INP_SVGA85;              // When =1, means input is SVGA (800x600) 85Hz mode
                                                                      // [datasheet: IF_STATUS_[14]]

    typedef UReg<0x00, 0x01, 7, 1> STATUS_IF_INP_SVGA;                // When =1, means input is SVGA (800x600) source, include
                                                                      // 60Hz/75Hz/85Hz [datasheet: IF_STATUS_[15]]


    typedef UReg<0x00, 0x02, 0, 1> STATUS_IF_INP_XGA60;               // When =1, means input is XGA (1024x768) 60Hz mode
                                                                      // [datasheet: IF_STATUS_[16]]

    typedef UReg<0x00, 0x02, 1, 1> STATUS_IF_INP_XGA70;               // When =1, means input is XGA (1024x768) 70Hz mode
                                                                      // [datasheet: IF_STATUS_[17]]

    typedef UReg<0x00, 0x02, 2, 1> STATUS_IF_INP_XGA75;               // When =1, means input is XGA (1024x768) 75Hz mode
                                                                      // [datasheet: IF_STATUS_[18]]

    typedef UReg<0x00, 0x02, 3, 1> STATUS_IF_INP_XGA85;               // When =1, means input is XGA (1024x768) 85Hz mode
                                                                      // [datasheet: IF_STATUS_[19]]

    typedef UReg<0x00, 0x02, 4, 1> STATUS_IF_INP_XGA;                 // When =1, means input is XGA (1024x768) source, include
                                                                      // 60/70/75/85Hz [datasheet: IF_STATUS_[20]]

    typedef UReg<0x00, 0x02, 5, 1> STATUS_IF_INP_SXGA60;              // When =1, means input is SXGA (1280x1024) 60Hz mode
                                                                      // [datasheet: IF_STATUS_[21]]

    typedef UReg<0x00, 0x02, 6, 1> STATUS_IF_INP_SXGA75;              // When =1, means input is SXGA (1280x1024) 75Hz mode
                                                                      // [datasheet: IF_STATUS_[22]]

    typedef UReg<0x00, 0x02, 7, 1> STATUS_IF_INP_SXGA85;              // When =1, means input is SXGA (1280x1024) 85Hz mode
                                                                      // [datasheet: IF_STATUS_[23]]


    typedef UReg<0x00, 0x03, 0, 8> STATUS_03;                         // Part of IF_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 45-bit block at s0_00 rather than field by field.

    typedef UReg<0x00, 0x03, 0, 1> STATUS_IF_INP_SXGA;                // When =1, means input is SXGA (1280x1024) mode, include
                                                                      // 60/75/85Hz [datasheet: IF_STATUS_[24]]

    typedef UReg<0x00, 0x03, 1, 1> STATUS_IF_INP_PC;                  // When =1, means input is graphic mode input, include
                                                                      // VGA/SVGA/XGA/SXGA [datasheet: IF_STATUS_[25]]

    typedef UReg<0x00, 0x03, 2, 1> STATUS_IF_INP_720P50;              // When =1, means input is HD720P (1280x720) 50Hz mode
                                                                      // [datasheet: IF_STATUS_[26]]

    typedef UReg<0x00, 0x03, 3, 1> STATUS_IF_INP_720P60;              // When =1, means input is HD720P (1280x720) 60Hz mode
                                                                      // [datasheet: IF_STATUS_[27]]

    typedef UReg<0x00, 0x03, 4, 1> STATUS_IF_INP_720;                 // When =1, means input is HD720P source, include 50Hz/60Hz
                                                                      // [datasheet: IF_STATUS_[28]]

    typedef UReg<0x00, 0x03, 5, 1> STATUS_IF_INP_2200_1125I;          // When =1, means input is 2200x1125i mode [datasheet:
                                                                      // IF_STATUS_[29]]

    typedef UReg<0x00, 0x03, 6, 1> STATUS_IF_INP_2376_1250I;          // When =1, means input is 2376x1250i mode [datasheet:
                                                                      // IF_STATUS_[30]]

    typedef UReg<0x00, 0x03, 7, 1> STATUS_IF_INP_2640_1125I;          // When =1, means input is 2640x1125i mode [datasheet:
                                                                      // IF_STATUS_[31]]


    typedef UReg<0x00, 0x04, 0, 8> STATUS_04;                         // Part of IF_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 45-bit block at s0_00 rather than field by field.

    typedef UReg<0x00, 0x04, 0, 1> STATUS_IF_INP_1080I;               // When =1, means input is HD1080i source, include
                                                                      // 2200x1125i, 2376x1250i, [datasheet: IF_STATUS_[32]]

    typedef UReg<0x00, 0x04, 1, 1> STATUS_IF_INP_2200_1125P;          // When =1, means input is HD 2200x1125P mode [datasheet:
                                                                      // IF_STATUS_[33]]

    typedef UReg<0x00, 0x04, 2, 1> STATUS_IF_INP_2376_1250P;          // When =1, means input is HD 2376x1250P mode [datasheet:
                                                                      // IF_STATUS_[34]]

    typedef UReg<0x00, 0x04, 3, 1> STATUS_IF_INP_2640_1125P;          // When =1, means input is HD 2640x1125P mode [datasheet:
                                                                      // IF_STATUS_[35]]

    typedef UReg<0x00, 0x04, 4, 1> STATUS_IF_INP_1808P;               // When =1, means input is 1080P source, include 2200x1250P,
                                                                      // 2376x1125P [datasheet: IF_STATUS_[36]]

    typedef UReg<0x00, 0x04, 5, 1> STATUS_IF_INP_HD;                  // When =1, means input is HD source, include 720P, 1080i,
                                                                      // 1080P [datasheet: IF_STATUS_[37]]

    typedef UReg<0x00, 0x04, 6, 2> INTERLACE_PROGRESSIVE_RECOGNIZE;

    typedef UReg<0x00, 0x04, 6, 1> STATUS_IF_INP_INT;                 // Part of IF_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 45-bit block at s0_00 rather than field by field.

    typedef UReg<0x00, 0x04, 7, 1> STATUS_IF_INP_PRG;                 // When =1, means input is progressive video source, include
                                                                      // 480P, 576P, 720P, [datasheet: IF_STATUS_[39]]


    typedef UReg<0x00, 0x05, 0, 8> STATUS_05;

    typedef UReg<0x00, 0x05, 0, 1> STATUS_IF_INP_USER;                // When =1, means input is the mode which match user define
                                                                      // resolution [datasheet: IF_STATUS_[40]]

    typedef UReg<0x00, 0x05, 1, 1> STATUS_IF_NO_SYNC;                 // When =1, means input is not sync timing [datasheet:
                                                                      // IF_STATUS_[41]]

    typedef UReg<0x00, 0x05, 2, 1> STATUS_IF_HT_BAD;                  // Part of IF_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 45-bit block at s0_00 rather than field by field.

    typedef UReg<0x00, 0x05, 3, 1> STATUS_IF_VT_BAD;                  // Part of IF_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 45-bit block at s0_00 rather than field by field.

    typedef UReg<0x00, 0x05, 4, 1> STATUS_IF_INP_SW;                  // When =1, means input source switch the mode [datasheet:
                                                                      // IF_STATUS_[44]]


    typedef UReg<0x00, 0x06, 0, 9> HPERIOD_IF;                        // Input source H total measurement result The value = input
                                                                      // source H total pixels / 4


    typedef UReg<0x00, 0x07, 1, 11> VPERIOD_IF;                       // Input source V total measurement result The value = input
                                                                      // source V total lines


    typedef UReg<0x00, 0x09, 6, 1> STATUS_MISC_PLL648_LOCK;

    typedef UReg<0x00, 0x09, 7, 1> STATUS_MISC_PLLAD_LOCK;            // Part of MISC_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 9-bit block at s0_09 rather than field by field.


    typedef UReg<0x00, 0x0A, 0, 1> STATUS_MISC_PIP_EN_V;              // When =1, means in display horizontal sync (the output
                                                                      // sync is high active) [datasheet: MISC_STATUS_[8]]

    typedef UReg<0x00, 0x0A, 1, 1> STATUS_MISC_PIP_EN_H;

    typedef UReg<0x00, 0x0A, 4, 1> STATUS_MISC_VBLK;

    typedef UReg<0x00, 0x0A, 5, 1> STATUS_MISC_HBLK;

    typedef UReg<0x00, 0x0A, 6, 1> STATUS_MISC_VSYNC;

    typedef UReg<0x00, 0x0A, 7, 1> STATUS_MISC_HSYNC;


    typedef UReg<0x00, 0x0B, 0, 8> CHIP_ID_FOUNDRY;


    typedef UReg<0x00, 0x0C, 0, 8> CHIP_ID_PRODUCT;                   // Part of CHIP_ID_, which RD-5725-1.1 documents as one
                                                                      // 24-bit block at s0_0B rather than field by field.


    typedef UReg<0x00, 0x0D, 0, 8> CHIP_ID_REVISION;                  // Part of CHIP_ID_, which RD-5725-1.1 documents as one
                                                                      // 24-bit block at s0_0B rather than field by field.


    typedef UReg<0x00, 0x0E, 0, 1> STATUS_GPIO_GPIO;

    typedef UReg<0x00, 0x0E, 1, 1> STATUS_GPIO_HALF;

    typedef UReg<0x00, 0x0E, 2, 1> STATUS_GPIO_SCLSA;

    typedef UReg<0x00, 0x0E, 3, 1> STATUS_GPIO_MBA;

    typedef UReg<0x00, 0x0E, 4, 1> STATUS_GPIO_MCS1;

    typedef UReg<0x00, 0x0E, 5, 1> STATUS_GPIO_HBOUT;

    typedef UReg<0x00, 0x0E, 6, 1> STATUS_GPIO_VBOUT;

    typedef UReg<0x00, 0x0E, 7, 1> STATUS_GPIO_CLKOUT;


    typedef UReg<0x00, 0x0F, 0, 8> STATUS_0F;                         // Interrupt status bit5, H-sync status When =1, means input
                                                                      // H-sync status is changed between stable and unstable

    typedef UReg<0x00, 0x0F, 0, 1> STATUS_INT_SOG_BAD;                // Part of INT_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 8-bit block at s0_0F rather than field by field.

    typedef UReg<0x00, 0x0F, 1, 1> STATUS_INT_SOG_SW;                 // Part of INT_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 8-bit block at s0_0F rather than field by field.

    typedef UReg<0x00, 0x0F, 2, 1> STATUS_INT_SOG_OK;                 // When =1, means input SOG source is stable [datasheet:
                                                                      // INT_STATUS_[2]]

    typedef UReg<0x00, 0x0F, 3, 1> STATUS_INT_INP_SW;                 // When =1, means input source switch the mode [datasheet:
                                                                      // INT_STATUS_[3]]

    typedef UReg<0x00, 0x0F, 4, 1> STATUS_INT_INP_NO_SYNC;            // Part of INT_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 8-bit block at s0_0F rather than field by field.

    typedef UReg<0x00, 0x0F, 5, 1> STATUS_INT_INP_HSYNC;              // When =1, means input H-sync status is changed between
                                                                      // stable and unstable [datasheet: INT_STATUS_[5]]

    typedef UReg<0x00, 0x0F, 6, 1> STATUS_INT_INP_VSYNC;              // When =1, means input V-sync status is changed between
                                                                      // stable and unstable [datasheet: INT_STATUS_[6]]

    typedef UReg<0x00, 0x0F, 7, 1> STATUS_INT_INP_CSYNC;              // When =1, means input H-sync status is changed between
                                                                      // stable and unstable [datasheet: INT_STATUS_[7]]


    typedef UReg<0x00, 0x10, 0, 4> STATUS_VDS_FR_NUM;

    typedef UReg<0x00, 0x10, 4, 1> STATUS_VDS_OUT_VSYNC;

    typedef UReg<0x00, 0x10, 5, 1> STATUS_VDS_OUT_HSYNC;


    typedef UReg<0x00, 0x11, 0, 1> STATUS_VDS_FIELD;                  // Part of VDS_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 24-bit block at s0_10 rather than field by field.

    typedef UReg<0x00, 0x11, 1, 1> STATUS_VDS_OUT_BLANK;              // When =1, in display blanking period [datasheet:
                                                                      // VDS_STATUS_[9]]

    typedef UReg<0x00, 0x11, 4, 11> STATUS_VDS_VERT_COUNT;            // Part of VDS_STATUS_, which RD-5725-1.1 documents as one
                                                                      // 24-bit block at s0_10 rather than field by field.


    typedef UReg<0x00, 0x13, 0, 1> STATUS_MEM_FF_WFF_FIFO_FULL;       // When =1, means WFF FIFO is full [datasheet:
                                                                      // MEM_FF_STATUS_[0]]

    typedef UReg<0x00, 0x13, 1, 1> STATUS_MEM_FF_WFF_FIFO_EMPTY;      // When =1, means WFF FIFO is empty [datasheet:
                                                                      // MEM_FF_STATUS_[1]]

    typedef UReg<0x00, 0x13, 2, 1> STATUS_MEM_FF_RFF_FIFO_FULL;       // When =1, means RFF FIFO is full [datasheet:
                                                                      // MEM_FF_STATUS_[2]]

    typedef UReg<0x00, 0x13, 3, 1> STATUS_MEM_FF_RFF_FIFO_EMPTY;      // When =1, means RFF FIFO is empty [datasheet:
                                                                      // MEM_FF_STATUS_[3]]

    typedef UReg<0x00, 0x13, 4, 1> STATUS_MEM_FF_CAP_FIFO_FULL;       // When =1, means capture FIFO is full [datasheet:
                                                                      // MEM_FF_STATUS_[4]]

    typedef UReg<0x00, 0x13, 5, 1> STATUS_MEM_FF_CAP_FIFO_EMPTY;      // When =1, means capture FIFO is empty [datasheet:
                                                                      // MEM_FF_STATUS_[5]]

    typedef UReg<0x00, 0x13, 6, 1> STATUS_MEM_FF_PLY_FIFO_FULL;       // When =1, means playback FIFO is full [datasheet:
                                                                      // MEM_FF_STATUS_[6]]

    typedef UReg<0x00, 0x13, 7, 1> STATUS_MEM_FF_PLY_FIFO_EMPTY;      // When =1, means playback FIFO is empty [datasheet:
                                                                      // MEM_FF_STATUS_[7]]


    typedef UReg<0x00, 0x14, 0, 1> STATUS_MEM_FF_EXT_FIN;             // When =1, means external memory chip initial is finished
                                                                      // [datasheet: MEM_FF_STATUS_[8]]


    typedef UReg<0x00, 0x15, 7, 1> STATUS_DEINT_PULLDN;               // When =1, means de-interlace is in 3:2 pull-down mode
                                                                      // [datasheet: DEINT_STATUS_[7]]


    typedef UReg<0x00, 0x16, 0, 8> STATUS_16;                         // Part of SYNC_PROC_STATUS_, which RD-5725-1.1 documents as
                                                                      // one 56-bit block at s0_16 rather than field by field.

    typedef UReg<0x00, 0x16, 0, 1> STATUS_SYNC_PROC_HSPOL;            // Part of SYNC_PROC_STATUS_, which RD-5725-1.1 documents as
                                                                      // one 56-bit block at s0_16 rather than field by field.

    typedef UReg<0x00, 0x16, 1, 1> STATUS_SYNC_PROC_HSACT;            // Part of SYNC_PROC_STATUS_, which RD-5725-1.1 documents as
                                                                      // one 56-bit block at s0_16 rather than field by field.

    typedef UReg<0x00, 0x16, 2, 1> STATUS_SYNC_PROC_VSPOL;            // Part of SYNC_PROC_STATUS_, which RD-5725-1.1 documents as
                                                                      // one 56-bit block at s0_16 rather than field by field.

    typedef UReg<0x00, 0x16, 3, 1> STATUS_SYNC_PROC_VSACT;            // Part of SYNC_PROC_STATUS_, which RD-5725-1.1 documents as
                                                                      // one 56-bit block at s0_16 rather than field by field.


    typedef UReg<0x00, 0x17, 0, 12> STATUS_SYNC_PROC_HTOTAL;          // Part of SYNC_PROC_STATUS_, which RD-5725-1.1 documents as
                                                                      // one 56-bit block at s0_16 rather than field by field.


    typedef UReg<0x00, 0x19, 0, 12> STATUS_SYNC_PROC_HLOW_LEN;        // Part of SYNC_PROC_STATUS_, which RD-5725-1.1 documents as
                                                                      // one 56-bit block at s0_16 rather than field by field.


    typedef UReg<0x00, 0x1B, 0, 11> STATUS_SYNC_PROC_VTOTAL;          // Part of SYNC_PROC_STATUS_, which RD-5725-1.1 documents as
                                                                      // one 56-bit block at s0_16 rather than field by field.


    typedef UReg<0x00, 0x1F, 0, 8> TEST_BUS_1F;


    typedef UReg<0x00, 0x20, 0, 16> TEST_FF_STATUS_;                  // Reserved


    typedef UReg<0x00, 0x22, 0, 8> CRC_REGOUT_RFF_;                   // Reserved


    typedef UReg<0x00, 0x23, 0, 8> CRC_REGOUT_PB_;                    // Reserved


    typedef UReg<0x00, 0x2E, 0, 16> TEST_BUS;                         // Part of TEST_BUS_, which RD-5725-1.1 documents as one
                                                                      // 24-bit block at s0_2E rather than field by field.

    typedef UReg<0x00, 0x2E, 0, 8> TEST_BUS_2E;


    typedef UReg<0x00, 0x2F, 0, 8> TEST_BUS_2F;                       // Part of TEST_BUS_, which RD-5725-1.1 documents as one
                                                                      // 24-bit block at s0_2E rather than field by field.

// INPUT FORMATTER REGISTERS



    typedef UReg<0x01, 0x02, 0, 8> INPUT_FORMATTER_02;




























    typedef UReg<0x01, 0x2B, 0, 7> GBS_PRESET_ID;

    typedef UReg<0x01, 0x2B, 7, 1> GBS_PRESET_CUSTOM;


    typedef UReg<0x01, 0x2C, 0, 1> GBS_OPTION_SCANLINES_ENABLED;

    typedef UReg<0x01, 0x2C, 1, 1> GBS_OPTION_SCALING_RGBHV;

    typedef UReg<0x01, 0x2C, 2, 1> GBS_OPTION_PALFORCED60_ENABLED;

    typedef UReg<0x01, 0x2C, 4, 1> GBS_RUNTIME_FTL_ADJUSTED;


    typedef UReg<0x01, 0x2D, 0, 8> GBS_PRESET_DISPLAY_CLOCK;

// DEINTERLACER REGISTERS

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

// HD_BYPS REGISTERS

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

// MISCELLANEOUS REGISTERS

    typedef UReg<0x00, 0x40, 0, 1> PLL_CKIS;                          // CKIS, PLL source clock selection When = 0, PLL use OSC
                                                                      // clock

    typedef UReg<0x00, 0x40, 1, 1> PLL_DIVBY2Z;                       // When = 1, PLL use input clock DIVBY2Z, PLL source clock
                                                                      // divide bypass When = 0, PLL source clock divide by two
                                                                      // When = 1, PLL source clock bypass divide

    typedef UReg<0x00, 0x40, 2, 1> PLL_IS;                            // IS, ICLK source selection When = 0, ICLK use PLL clock

    typedef UReg<0x00, 0x40, 3, 1> PLL_ADS;                           // When = 1, ICLK use input clock ADS, input clock selection
                                                                      // When = 0, input clock is from PCLKIN(pin40) When = 1,
                                                                      // input clock is from ADC

    typedef UReg<0x00, 0x40, 4, 3> PLL_MS;                            // MS[2:0], memory clock control When = 000, memory clock
                                                                      // =108MHz When = 001, memory clock = 81MHz When = 010,
                                                                      // memory clock from FBCLK (pin110) When = 011, memory clock
                                                                      // = 162MHz When = 100, memory clock = 144MHz When = 101,
                                                                      // memory clock = 185MHz When = 110, memory clock = 216MHz


    typedef UReg<0x00, 0x41, 0, 8> PLL648_CONTROL_01;

    typedef UReg<0x00, 0x41, 0, 2> PLL_VS;                            // VS[1:0] Display clock tuning register

    typedef UReg<0x00, 0x41, 2, 2> PLL_VS2;                           // VS2[1:0] Display clock tuning register

    typedef UReg<0x00, 0x41, 4, 2> PLL_VS4;                           // VS4[1:0] Display clock tuning register

    typedef UReg<0x00, 0x41, 6, 1> PLL_2XV;                           // 2XV Display clock tuning register

    typedef UReg<0x00, 0x41, 7, 1> PLL_4XV;                           // Part of PLL_4XV, which RD-5725-1.1 documents as one 1-bit
                                                                      // block at s0_41 rather than field by field.


    typedef UReg<0x00, 0x43, 0, 8> PLL648_CONTROL_03;

    typedef UReg<0x00, 0x43, 0, 2> PLL_R;                             // R[1:0] Skew control for testing

    typedef UReg<0x00, 0x43, 2, 2> PLL_S;                             // S[1:0] Skew control for testing

    typedef UReg<0x00, 0x43, 4, 1> PLL_LEN;                           // LEN Lock Enable

    typedef UReg<0x00, 0x43, 5, 1> PLL_VCORST;                        // VCORST VCO control voltage reset bit



    typedef UReg<0x00, 0x45, 6, 2> CKT_FF_CNTRL;                      // CKT used to control FIFO


    typedef UReg<0x00, 0x46, 0, 8> RESET_CONTROL_0x46;


    typedef UReg<0x00, 0x47, 0, 8> RESET_CONTROL_0x47;






    typedef UReg<0x00, 0x4D, 0, 5> TEST_BUS_SEL;                      // Test bus selection Test bus enable

    typedef UReg<0x00, 0x4D, 5, 1> TEST_BUS_EN;                       // When = 0, disable test bus output


    typedef UReg<0x00, 0x4E, 0, 1> DIGOUT_BYPS2PAD;                   // HD bypass channel to digital output control When = 0,
                                                                      // disable HD bypass to digital output

    typedef UReg<0x00, 0x4E, 1, 1> DIGOUT_ADC2PAD;                    // When = 1, enable HD bypass to digital output (VG_[7:0],
                                                                      // VR_[7:0], VB_[7:0]) ADC to digital output control When =
                                                                      // 0, disable ADC to digital output When = 1, enable ADC
                                                                      // (with decimation) to digital output (VG, VR, VB)


    typedef UReg<0x00, 0x4F, 1, 1> OUT_CLK_PHASE_CNTRL;               // When = 1, V4CLK will invert before go to DAC CLKOUT
                                                                      // invert control When = 0, CLKOUT output no invert When =
                                                                      // 1, CLKOUT will invert before output

    typedef UReg<0x00, 0x4F, 2, 2> OUT_CLK_EN;                        // CLKOUT selection control When = 00, CLKOUT = V4CLK When =
                                                                      // 01, CLKOUT = V2CLK When = 10, CLKOUT = VCLK

    typedef UReg<0x00, 0x4F, 4, 1> CLKOUT_EN;                         // When = 11, CLKOUT = ADC output clock CLKOUT enable
                                                                      // control When = 0, disable CLKOUT to PAD When = 1, enable
                                                                      // CLKOUT to PAD

    typedef UReg<0x00, 0x4F, 5, 1> OUT_SYNC_CNTRL;                    // H/V sync output enable When = 0, disable H/V sync output
                                                                      // to PAD

    typedef UReg<0x00, 0x4F, 6, 2> OUT_SYNC_SEL;                      // When = 1, enable H/V sync output to PAD H/V sync output
                                                                      // selection control When = 00, H/V sync output are from
                                                                      // vds_proc When = 01, H/V sync output are from HD bypass
                                                                      // When = 10, H/V sync output are from sync processor When =
                                                                      // 11, reserved


    typedef UReg<0x00, 0x50, 0, 1> OUT_BLANK_SEL_0;                   // HBOUT/VBUT selection control When = 0, VBOUT output
                                                                      // Vertical Blank

    typedef UReg<0x00, 0x50, 1, 1> OUT_BLANK_SEL_1;                   // When = 1, VBOUT output composite Display Enable
                                                                      // HBOUT/VBOUT selection control When = 0, HBOUT/VBOUT is
                                                                      // from vds_proc When = 1, HBOUT/VBOUT is from HD bypass

    typedef UReg<0x00, 0x50, 4, 1> IN_BLANK_SEL;                      // When = 0, disable input composite Display Enable

    typedef UReg<0x00, 0x50, 5, 1> IN_BLANK_IREG_BYPS;                // When = 1, enable input composite Display Enable Input
                                                                      // blank IREG bypas When = 0, input composite Display Enable
                                                                      // latched by falling edge DFF When = 1, bypass falling edge
                                                                      // DFF





    typedef UReg<0x00, 0x57, 7, 1> INVT_RING_EN;                      // When = 0, disable invert ring When = 1, enable invert
                                                                      // ring for processing test


    typedef UReg<0x00, 0x58, 0, 8> INTERRUPT_CONTROL_00;


    typedef UReg<0x00, 0x59, 0, 8> INTERRUPT_CONTROL_01;

// MEMORY REGISTERS

    typedef UReg<0x04, 0x00, 0, 8> MEM_INI_REG;                       // refresh cycle; otherwise refresh cycle will be before
                                                                      // mode cycle. SDRAM Start Initial Cycle: This register
                                                                      // should work with the register 80/[2:0]; When this bit is
                                                                      // 1, memory controller initial cycle enable; When this bit
                                                                      // is 0, memory controller initial cycle disable

    typedef UReg<0x04, 0x00, 4, 1> SDRAM_RESET_SIGNAL;                // Part of MEM_INI_REG, which RD-5725-1.1 documents as one
                                                                      // 8-bit block at s4_00 rather than field by field.


    typedef UReg<0x04, 0x01, 0, 16> MEM_MODE_REG;                     // SDRAM Mode Information Low 8bits: [2:0] Burst length, [3]
                                                                      // Wrap type : 0 = Sequential , 1= interleave ; [6:4]
                                                                      // Latency mode, 010: select Latency =2; 011: select Latency
                                                                      // =3


    typedef UReg<0x04, 0x03, 0, 1> MEM_MODE_BA0;                      // Bank0 Select Value In Load Mode Register Cycle : This
                                                                      // register ‘s aim is compatible with more sdram chips

    typedef UReg<0x04, 0x03, 1, 1> MEM_MODE_BA1;                      // Bank1 Select Value In Load Mode Register Cycle : This
                                                                      // register ‘s aim is compatible with more sdram chips

    typedef UReg<0x04, 0x03, 2, 1> MEM_MODE_CS0;                      // Chip0 Select Value in Load Mode Register Cycle : This
                                                                      // register ‘s aim is compatible with more sdram chips

    typedef UReg<0x04, 0x03, 3, 1> MEM_MODE_CS1;                      // Chip1 Select Value in Load Mode Register Cycle : This
                                                                      // register ‘s aim is compatible with more sdram chips

    typedef UReg<0x04, 0x03, 4, 1> MEM_MODE_CYC;                      // Mode Cycle Period Select When this bit is 1, then mode
                                                                      // cycle for memory initialization will be 3 clocks

    typedef UReg<0x04, 0x03, 5, 1> MEM_INI_REF_CYC;                   // otherwise be 2 clocks ; Initial Cycle Refresh Period
                                                                      // Clock Number Select: This register is control the delays
                                                                      // of Command, address and data sent to PAD When it is at 1,
                                                                      // select NCASDLY cell, when it is at 0, select DLY8LV cell


    typedef UReg<0x04, 0x04, 0, 3> MEM_FK_RD_DLY;                     // SDRAM Rising Edge Clock Delay for Latching Read Data:
                                                                      // (default set 3’b 000); With DLY8LV and NCASDLY
                                                                      // MEM_FK_RD_DLY [2:0] #of Mclk 0 0 0 0.00/0.0 0 0 1
                                                                      // 0.25/2.0 0 1 0 0.50/4.0 0 1 1 0.75/6.0 1 0 0 1.00 1 0 1
                                                                      // 1.50 1 1 0 2.00

    typedef UReg<0x04, 0x04, 4, 3> MEM_RD_LAT_PIP;                    // MEM_RD_LAT_PIP [2:0] #of Mclk 0 0 0 3 0 0 1 2 0 1 0 1 0 1
                                                                      // 1 4 1 0 0 5


    typedef UReg<0x04, 0x05, 0, 2> MEM_ACT_CYCLE;                     // Number of Memory Clock For SDRAM Active Cycle:
                                                                      // MEM_ACT_CYCLE [1:0] # of Mclk 0 0 2 0 1 3 1 0 4 1 1 5

    typedef UReg<0x04, 0x05, 4, 2> MEM_PCHG_CYCLE;                    // MEM_PCHG_CYCLE [1:0] # of Mclk 0 0 2 0 1 3 1 0 4


    typedef UReg<0x04, 0x06, 0, 3> MEM_REF_RATE;                      // For VGA Mode of Refresh Cycle: #of MEM_REF_RATE [2:0]
                                                                      // refresh 0 0 0 3 0 0 1 5 0 1 X 1 1 0 X 2 1 1 X 4

    typedef UReg<0x04, 0x06, 4, 3> MEM_REF_CYCLE;                     // MEM_REF_CYCLE [2:0] # Of Mclk 0 0 0 6 0 0 1 7 0 1 0 8


    typedef UReg<0x04, 0x07, 0, 3> MEM_TWR_SEL;                       // TWR Period Select (Number of Memory Clock Inserted from
                                                                      // Last Write Cycle to Precharge) MEM_TWR_SEL [2:0] #OF MCLK
                                                                      // 0 0 0 0 0 0 1 1 0 1 0 2 0 1 1 3 1 0 0 4

    typedef UReg<0x04, 0x07, 4, 3> MEM_TRAS_SEL;                      // 0 0 0 3 0 0 1 4


    typedef UReg<0x04, 0x08, 0, 2> MEM_R2W_NOP_CYC;                   // Number of Dummy Clock For SDRAM Read to Write Cycle:
                                                                      // MEM_R2W_NOP_CYC [1:0] #OF MCLK 0 0 0 0 1 1 1 0 2 1 1 3

    typedef UReg<0x04, 0x08, 4, 2> MEM_W2R_SEL_CYC;                   // MEM_W2R_SEL_CYC [1:0] #OF MCLK 0 0 0 0 1 1 1 0 2


    typedef UReg<0x04, 0x09, 0, 2> MEM_BK0_SEL;                       // Bank Select Address Mux: MEM_BK0_SEL [1:0] # OF ADDRESS
                                                                      // BIT 0 0 ADR 19 0 1 ADR 20 1 0 ADR 21 1 1 NOP

    typedef UReg<0x04, 0x09, 2, 2> MEM_BK1_SEL;                       // Bank Select Address Mux: MEM_BK1_SEL [1:0] # OF ADDRESS
                                                                      // BIT 0 0 ADR 19 0 1 ADR 20 1 0 ADR 21 1 1 NOP

    typedef UReg<0x04, 0x09, 4, 2> MEM_CS0_SEL;                       // Bank Select Address Mux: MEM_CS0_SEL [1:0] # OF ADDRESS
                                                                      // BIT 0 0 ADR 19 0 1 ADR 20 1 0 ADR 21 1 1 NOP

    typedef UReg<0x04, 0x09, 6, 2> MEM_CS1_SEL;                       // Bank Select Address Mux: MEM_CS1_SEL [1:0] # OF ADDRESS
                                                                      // BIT 0 0 ADR 19 0 1 ADR 20 1 0 ADR 21 1 1 NOP


    typedef UReg<0x04, 0x0A, 0, 2> MEM_COL_ST_SEL;                    // When this bit is 0,column address will not start with
                                                                      // address bit 0 Col Address Start with address bit 1
                                                                      // (default value 0). When this bit is 1,column address
                                                                      // starts with address bit 1 When this bit is 0,column
                                                                      // address will not start with address bit 1

    typedef UReg<0x04, 0x0A, 4, 2> MEM_ROW_ST_SEL;                    // When this bit is 0,row address will not start with
                                                                      // address bit 8. Row Address Start with address bit 9
                                                                      // (default value 0). When this bit is 1,row address starts
                                                                      // with address bit 9; When this bit is 0,row address will
                                                                      // not start with address bit 9


    typedef UReg<0x04, 0x0B, 0, 7> MEM_ADR_REG;                       // When this bit is 0,address 19 changed will not do
                                                                      // precharge. Memory Row Address Precharge enable for
                                                                      // Address 20 When this bit is 1,address 20 changed will do
                                                                      // precharge; When this bit is 0,address 20 changed will not
                                                                      // do precharge


    typedef UReg<0x04, 0x0C, 0, 4> MEM_COL_ADR_VLD;                   // Memory Column Address Enable For Address bit 10 (default
                                                                      // value 0) For others SDRAM chip that column address more
                                                                      // than 8bits When this bit is 1,address 10 will act as
                                                                      // column address; When this bit is 0,address 10 will not be
                                                                      // column address


    typedef UReg<0x04, 0x0D, 0, 3> MEM_SPECIAL_PIN;                   // Special Pin10 For Precharge: default value 0 If Memory
                                                                      // module Address 10 is special Pin, In initialization
                                                                      // cycle, must set this register to 1, and precharge all
                                                                      // banks; otherwise, will set 0

    typedef UReg<0x04, 0x0D, 4, 1> MEM_BA_ADR11_SEL;                  // When this register is 1: bank select pad will be memory
                                                                      // address 11 bit, support 1M x 16bits x4 banks memory chip;

    typedef UReg<0x04, 0x0D, 5, 1> MEM_CS0_BA0_SEL;                   // When this register is 0: bank select pad will be bank
                                                                      // select pad, support 1M x16bits x 2banks memory chip; CHIP
                                                                      // SELECT 0 PAD SAHRE WITH BANK SELECT 0 : When this
                                                                      // register is 1: chip select 0pad will be bank select 0
                                                                      // pad, support 1M x 16bits x 4 banks memory chip; When this
                                                                      // register is 0: chip select 0 pad will be chip select 0
                                                                      // pad, support 1M x16bits x 2banks memory chip; CHIP SELECT
                                                                      // 1 PAD SAHRE WITH BANK SELECT 1 :

    typedef UReg<0x04, 0x0D, 6, 1> MEM_CS1_BA1_SEL;                   // When this register is 1: chip select 1 pad will be bank
                                                                      // select 1 pad, support 1M x 16bits x 4 banks memory chip;


    typedef UReg<0x04, 0x0E, 0, 3> MEM_CMD_PIPE;                      // SDRAM RAS Command Pipe Select: When it is at 0, RAS
                                                                      // signal pass through a pipe, or it will bypass a pipe;


    typedef UReg<0x04, 0x0F, 0, 4> MEM_FST_REG;                       // SDRAM Write and Read Signal Fast Mode Signal Don’t care,
                                                                      // default value 0;In fast mode, When this bit is 1,DQM
                                                                      // signal will advance. When this bit is 0,DQM signal will
                                                                      // be normal


    typedef UReg<0x04, 0x10, 0, 5> MEM_MISC_REG;                      // When this bit sets 0, will make state machine hold during
                                                                      // read/write operation Add No Operation For Precharge Cycle
                                                                      // Don’t care, default value 0; When this bit sets 0, will
                                                                      // add NOP for precharge cycle ; When this bit sets 1,will
                                                                      // no add NOP for precharge cycle. Turn Off Qualified Active
                                                                      // Cycle Done: default value 0;


    typedef UReg<0x04, 0x11, 0, 1> MEM_FBK_CLK_SEL;                   // Select Clock Feed Back from PAD; This register will be
                                                                      // valid when the register BC/[4] = 1’b0; When this bit is
                                                                      // 1, select external pad feed back clock; When this bit is
                                                                      // 0, select internal PAD feed back clock

    typedef UReg<0x04, 0x11, 1, 1> MEM_FBK_SEL_MCLK;                  // If BC/[4] = 1’b1,this bit should be set 0 ; FEEDBACK
                                                                      // CLOCK SELECT SOURCE: When this bit sets 1, feedback clock
                                                                      // will select PLL clock; When this bit sets 0, feedback
                                                                      // clock will select clock from PAD

    typedef UReg<0x04, 0x11, 2, 1> MEM_FBK_CS2_SEL;                   // FEEDBACK CLOCK PAD SHARE WITH CHIP SELECT 2: When this
                                                                      // register is 1: Pad will be chip select 2 PAD; When this
                                                                      // register is 0: Pad will be feedback clock pad; This
                                                                      // register uses only 6M memory, 3 chips

    typedef UReg<0x04, 0x11, 4, 1> MEM_FBK_INV_PATH_SEL;              // When this register set 1, it will select falling edge
                                                                      // fetch feedback data;

    typedef UReg<0x04, 0x11, 7, 1> MEM_FBK_PATH_SEL;                  // When this bit is 1, it will capture data with feedback
                                                                      // clock path, When this bit is 0, It will capture data with
                                                                      // memory clock


    typedef UReg<0x04, 0x12, 0, 1> MEM_INTER_DLYCELL_SEL;             // Select SDRAM Delay Cell: MEM_INTER_DLYCELL_S This
                                                                      // register is control the delay of data/address/command EL

    typedef UReg<0x04, 0x12, 1, 1> MEM_CLK_DLYCELL_SEL;               // When it is at 0, select bypass delay cell, when it is at
                                                                      // 1, select DLY8LV cell. Select SDRAM Delay Cell: This
                                                                      // register is only control the delay of clock send to PAD
                                                                      // When it is at 0, select bypass delay cell, when it is at
                                                                      // 1, select DLY8LV cell

    typedef UReg<0x04, 0x12, 2, 1> MEM_FBK_CLK_DLYCELL_SEL;           // Select SDRAM Delay Cell: This register is only control
                                                                      // the delay of feed back clock. When it is at 0, select
                                                                      // bypass delay cell, when it is at 1, select DLY8LV cell


    typedef UReg<0x04, 0x13, 0, 1> MEM_PAD_CLK_INVERT;                // Invert Memory Rising Edge Clock to PAD: When this bit is
                                                                      // 1, invert memory clock and send to PAD;

    typedef UReg<0x04, 0x13, 1, 1> MEM_RD_DATA_CLK_INVERT;            // When this bit is 0, will bypass memory clock and send to
                                                                      // PAD. Read memory data with Memory Clock rising or falling
                                                                      // edge: When this bit is 1, with Memory clock falling edge;
                                                                      // When this bit is 0, with Memory clock rising edge.
                                                                      // Control feedback clock register

    typedef UReg<0x04, 0x13, 2, 1> MEM_FBK_CLK_INVERT;                // When this bit is at 1, will invert feedback clock;


    typedef UReg<0x04, 0x14, 0, 3> MEM_NEW_FUNC_CTL;                  // When this register sets 0, no change. REFRESH CYCLE
                                                                      // SIGNAL IS LOW: When this bit is 1, when refresh more than
                                                                      // 2 times, in refresh cycle, make DQM will high; When this
                                                                      // bit is 0, only for refresh one time, DQM will high.
                                                                      // CONTROL TIMING FOR ACTIVE TO PRECHARGE;

    typedef UReg<0x04, 0x14, 4, 1> MEM_WRITE_CYCL_CTL;                // When this bit sets 1, read cycle hold will enter write
                                                                      // cycle directly. When this bit sets 0, will not enter
                                                                      // write cycle directly

    typedef UReg<0x04, 0x14, 7, 1> MEM_MBUS32OR16_SEL;                // When this bit sets 1, memory bus is 32-bit. When this bit
                                                                      // set2 0, memory bus is 16-bit


    typedef UReg<0x04, 0x15, 0, 1> MEM_REQ_PBH_RFFH;                  // Play back high request priority exchange with read FIFO
                                                                      // high request When this bit is 1, read FIFO high request >
                                                                      // play back high request;

    typedef UReg<0x04, 0x15, 1, 1> MEM_REQ_PB_RFF_CAP;                // When this bit is 0, play back high request >read FIFO
                                                                      // high request; Capture request exchange with PlayBack low
                                                                      // request and Read FIFO low request When this bit is 0:
                                                                      // play back low req > read FIFO low req > capture req When
                                                                      // this bit is 1: cap req > play back low req > read FIFO
                                                                      // low req Write FIFO request priority exchange with capture
                                                                      // request

    typedef UReg<0x04, 0x15, 2, 1> MEM_REQ_WFF_CAP;                   // When this bit is 1, capture request >write FIFO request


    typedef UReg<0x04, 0x16, 0, 3> MEM_TEST_SEL;                      // Test Logic Controll Select four groups test signals
                                                                      // (internal hardware debug use only)


    typedef UReg<0x04, 0x17, 0, 3> MEM_WOEZ_DLY;                      // Data TRI_STATE Enable Delay Control Bits: with DLY8LV and
                                                                      // NCASDLY MEM_WOEZ_DLY [2:0] #OF NS 0 0 0 0.00/0.0 0 0 1
                                                                      // 0.25/2.0 0 1 0 0.50/4.0 0 1 1 0.75/6.0 1 0 0 1.00 1 0 1
                                                                      // 1.50 1 1 0 2.00 1 1 1 3.00

    typedef UReg<0x04, 0x17, 4, 1> MEM_WOEZ_PIP;                      // When this register is 1: the sdram data tri_state enable
                                                                      // will extend a pipe; When this register is 0: the sdram
                                                                      // data tri_state enable will be selected by

    typedef UReg<0x04, 0x17, 5, 1> MEM_WOEZ_SEL_DLYCELL;              // the other registers. SDRAM DATA TRI_state ENABLE DELAY
                                                                      // SELECT: When this register is 0: will select extension
                                                                      // from delay cells; When this register is 1: will select
                                                                      // not extension. This register will control sdram data
                                                                      // tri_state enable with the register r_mwoeslpz


    typedef UReg<0x04, 0x18, 0, 3> MEM_DATA_DLY_REG;                  // Data Delay Control Bits: with DLY8LV MEM_DATA_DLY_REG
                                                                      // [2:0] #OF NS 0 0 0 0.00 0 0 1 0.25 0 1 0 0.50 0 1 1 0.75
                                                                      // 1 0 0 1.00 1 0 1 1.50 1 1 0 2.00 1 1 1 3.00

    typedef UReg<0x04, 0x18, 4, 2> MEM_WR_DATA_PIP;                   // (In 5705,only 2’b00) MEM_WR_DATA_PIP [1:0] # OF PIPE


    typedef UReg<0x04, 0x19, 0, 3> MEM_RAS_DLY_REG;                   // RAS Delay Control bits: default value 3’b000;with DLY8LV
                                                                      // MEM_RAS_DLY_REG [2:0] #OF NS 0 0 0 0.00 0 0 1 0.25 0 1 0
                                                                      // 0.50 0 1 1 0.75 1 0 0 1.00 1 0 1 1.50 1 1 0 2.00 1 1 1
                                                                      // 3.00

    typedef UReg<0x04, 0x19, 4, 3> MEM_CAS_DLY_REG;                   // MEM_CAS_DLY_REG [2:0] #OF NS 0 0 0 0.00 0 0 1 0.25 0 1 0
                                                                      // 0.50 0 1 1 0.75 1 0 0 1.00 1 0 1 1.50 1 1 0 2.00


    typedef UReg<0x04, 0x1A, 0, 3> MEM_WE_DLY_REG;                    // WE Delay Control bits High 2 bits: with DLY8LV
                                                                      // MEM_WE_DLY_REG [2:0] #OF NS 0 0 0 0.00 0 0 1 0.25 0 1 0
                                                                      // 0.50 0 1 1 0.75 1 0 0 1.00 1 0 1 1.50 1 1 0 2.00 1 1 1
                                                                      // 3.00

    typedef UReg<0x04, 0x1A, 4, 3> MEM_DQM_DLY_REG;                   // MEM_DQM_DLY_REG [2:0] #OF NS 0 0 0 0.00 0 0 1 0.25 0 1 0
                                                                      // 0.50 0 1 1 0.75 1 0 0 1.00 1 0 1 1.50 1 1 0 2.00


    typedef UReg<0x04, 0x1B, 0, 3> MEM_ADR_DLY_REG;                   // Address Delay Control bits: with DLY8LV MEM_ADR_DLY_REG
                                                                      // [2:0] #OF NS 0 0 0 0.00 0 0 1 0.25 0 1 0 0.50 0 1 1 0.75
                                                                      // 1 0 0 1.00 1 0 1 1.50 1 1 0 2.00 1 1 1 3.00

    typedef UReg<0x04, 0x1B, 4, 3> MEM_CLK_DLY_REG;                   // MEM_CLK_DLY_REG [2:0] #OF NS 0 0 0 0.00 0 0 1 0.20 0 1 0
                                                                      // 0.50 0 1 1 0.75 1 0 0 1.00 1 0 1 1.50 1 1 0 2.00


    typedef UReg<0x04, 0x1C, 0, 3> MEM_CS0_DLY_REG;                   // Chip Select 0 Delay Control Low 2bits: with DLY8LV
                                                                      // MEM_CS0_DLY_REG [2:0] #OF NS 0 0 0 0.00 0 0 1 0.25 0 1 0
                                                                      // 0.50 0 1 1 0.75 1 0 0 1.00 1 0 1 1.50 1 1 0 2.00 1 1 1
                                                                      // 3.00

    typedef UReg<0x04, 0x1C, 4, 3> MEM_CS1_DLY_REG;                   // MEM_CS1_DLY_REG [2:0] #OF NS 0 0 0 0.00 0 0 1 0.25 0 1 0
                                                                      // 0.50 0 1 1 0.75 1 0 0 1.00 1 0 1 1.50 1 1 0 2.00


    typedef UReg<0x04, 0x1D, 0, 3> MEM_BA0_DLY_REG;                   // Bank0 Delay Control bits: with DLY8LV MEM_BA0_DLY_REG
                                                                      // [2:0] #OF NS 0 0 0 0.00 0 0 1 0.25 0 1 0 0.50 0 1 1 0.75
                                                                      // 1 0 0 1.00 1 0 1 1.50 1 1 0 2.00 1 1 1 3.00

    typedef UReg<0x04, 0x1D, 4, 3> MEM_BA1_DLY_REG;                   // MEM_BA1_DLY_REG [2:0] #OF NS 0 0 0 0.00 0 0 1 0.25 0 1 0
                                                                      // 0.50 0 1 1 0.75 1 0 0 1.00 1 0 1 1.50 1 1 0 2.00

// CAPTURE & PLAYBACK REGISTERS

    typedef UReg<0x04, 0x20, 0, 3> CAP_CNTRL_TST;                     // Capture Test logic control: Bit [2:0]: select capture
                                                                      // internal test bus


    typedef UReg<0x04, 0x21, 0, 1> CAPTURE_ENABLE;                    // Enable capture When it’s set 1, capture will be turn on.
                                                                      // When it’s set 0, capture will be turn off. Request
                                                                      // generated when capture FIFO half

    typedef UReg<0x04, 0x21, 1, 1> CAP_FF_HALF_REQ;                   // When set to 1, request generated when capture FIFO half

    typedef UReg<0x04, 0x21, 2, 1> CAP_BUF_STA_INV;                   // When set to 0, request generated when capture FIFO write
                                                                      // pointer is 1. Capture double buffer status invert before
                                                                      // output When set to 1, double buffer status invert. When
                                                                      // set to 0, double buffer status doesn’t change

    typedef UReg<0x04, 0x21, 3, 1> CAP_DOUBLE_BUFFER;                 // Enable double buffer When set to 1, enable double buffer

    typedef UReg<0x04, 0x21, 5, 1> CAP_SAFE_GUARD_EN;                 // When set to 1, turn on safe guard function

    typedef UReg<0x04, 0x21, 6, 1> CAP_VRST_FFRST_EN;                 // When set to 0, turn off safe guard function. Enable input
                                                                      // v-sync reset FIFO When set to 1, enable feed back v-sync
                                                                      // reset FIFO. When set to 0, disable feed back v-sync reset
                                                                      // FIFO

    typedef UReg<0x04, 0x21, 7, 1> CAP_ADR_ADD_2;                     // Enable address add by 2 When set to 1,address added by 2
                                                                      // per pixel, When set to 0,added by 1 per pixel


    typedef UReg<0x04, 0x22, 0, 1> CAP_REQ_OVER;                      // Horizontal request end When this bit set 1, the final
                                                                      // capture request of one line is in the horizontal blank

    typedef UReg<0x04, 0x22, 1, 1> CAP_STATUS_SEL;                    // rising edge, set 0 capture request will free run Capture
                                                                      // FIFO half status select When set to 1, request generated
                                                                      // when capture FIFO is half. When set to 0, request
                                                                      // generated when capture FIFO is delm’s value

    typedef UReg<0x04, 0x22, 2, 1> CAP_LAST_POP_CTL;                  // Capture POP data control When set to 1, horizontal or
                                                                      // vertical load start address will check if there is pop

    typedef UReg<0x04, 0x22, 3, 1> CAP_REQ_FREEZ;                     // When set to 0, horizontal or vertical load start address
                                                                      // will not check. Capture Request Freeze When set to 1,
                                                                      // capture FIFO will pause the FIFO write and read . When
                                                                      // set to 0, capture FIFO will operate normally


    typedef UReg<0x04, 0x23, 0, 8> CAP_FF_STATUS;                     // Capture FIFO status When cap_cntrl_[17] set 1’b1, this
                                                                      // register will be valid, this value will less than 64


    typedef UReg<0x04, 0x24, 0, 21> CAP_SAFE_GUARD_A;                 // Safe Guard Address For Buffer A[20:16]: Safe guard
                                                                      // address A [20:16], Mapping to 32bits width data bus field


    typedef UReg<0x04, 0x27, 0, 21> CAP_SAFE_GUARD_B;                 // Safe Guard Address For Buffer B[20:16]: Safe guard
                                                                      // address B [20:16], Mapping to 32bits width data bus field


    typedef UReg<0x04, 0x2B, 0, 1> PB_CUT_REFRESH;                    // Disable refresh request generation When set to 1, disable
                                                                      // refresh request generation

    typedef UReg<0x04, 0x2B, 1, 2> PB_REQ_SEL;                        // When set to 0, enable refresh request generation. Enable
                                                                      // playback request mode PB_REQ_SEL PBHREQ PBLREQ 00 0 Low
                                                                      // request 01 0 High request Enable VDS input to select
                                                                      // playback output or de-interlace data out

    typedef UReg<0x04, 0x2B, 3, 1> PB_BYPASS;                         // When this bit is 1, select de-interlace data out to VDS

    typedef UReg<0x04, 0x2B, 4, 1> PB_DB_FIELD_EN;                    // When this bit is 0, select playback output to VDS. Enable
                                                                      // double field display When set to 1, enable double field
                                                                      // display. When set to 0, disable double field display

    typedef UReg<0x04, 0x2B, 5, 1> PB_DB_BUFFER_EN;                   // Enable double buffer When set to 1, enable double buffer

    typedef UReg<0x04, 0x2B, 6, 1> PB_2FRAME_EXCHG;                   // When set to 0, disable double buffer. Exchange playback
                                                                      // two frames output data When set to 1, exchange playback
                                                                      // current frame with past frame and output. When set to 0,
                                                                      // don’t exchange

    typedef UReg<0x04, 0x2B, 7, 1> PB_ENABLE;                         // Enable Playback When it’s set 1, play back will be on
                                                                      // work, or will not work


    typedef UReg<0x04, 0x2C, 0, 6> PB_MAST_FLAG_REG;                  // Master line flag [5:0] Playback FIFO policy master value:


    typedef UReg<0x04, 0x2D, 0, 6> PB_GENERAL_FLAG_REG;               // General line flag [5:0] Playback FIFO policy general
                                                                      // value:


    typedef UReg<0x04, 0x2E, 0, 1> PB_UP_DOW_RBUF_INV;                // PB_RBUF_INV When rate convert from up to down, capture
                                                                      // FIFO will refer to the play back

    typedef UReg<0x04, 0x2E, 1, 1> PB_UP_DOW_RBUF_SEL;                // buffer status, this bit is invert play back buffer
                                                                      // status. PB_RBUF_SEL When rate convert from up to down,
                                                                      // capture FIFO will refer to the play back buffer status,
                                                                      // this bit will be set to 1. Otherwise, it will be set to 0

    typedef UReg<0x04, 0x2E, 7, 1> PB_DOUBLE_REFRESH_EN;              // When set to 1, refresh request will at the rising and
                                                                      // falling edge of hbout. When set to 0, refresh will be
                                                                      // only at the rising edge of hbout


    typedef UReg<0x04, 0x2F, 0, 4> PB_TST_REG;                        // PlayBack Test Logic To select playback test bus, total 8
                                                                      // groups can be selected


    typedef UReg<0x04, 0x30, 0, 4> PB_CAP_NOISE_CMD;                  // Capture Noise Reduction Command 0: disable noise reduce
                                                                      // function 1: turn on PAL mode 2 (50hz to 50hz) and storage
                                                                      // in memory 5 frames 2: turn on PAL mode 3 5: turn on NTSC
                                                                      // mode 2 and storage memory 3 frames 6: turn on NTSC mode 3
                                                                      // 9: turn on PAL mode 2 (50hz to 50hz, 50hz to 60hz, 50hz
                                                                      // to 100hz) and storage memory 6 frames. D: turn on NTSC
                                                                      // mode 2 (60hz to 60hz, 60hz to 120hz) and storage memory 4
                                                                      // frames


    typedef UReg<0x04, 0x31, 0, 21> PB_CAP_BUF_STA_ADDR_A;            // Capture and Play Back Buffer A START ADDRESS[20:16]:
                                                                      // Start address buffer A [20:16], Mapping to 32bits width
                                                                      // data bus field


    typedef UReg<0x04, 0x34, 0, 21> PB_CAP_BUF_STA_ADDR_B;            // Buffer B START address [15:8] PB_CAP_BUF_STA_ADD When in
                                                                      // double buffer mode, this is defined as capture and
                                                                      // playback buffer B R_B [15:8] start address. Mapping to
                                                                      // 32bits width data bus field


    typedef UReg<0x04, 0x37, 0, 10> PB_CAP_OFFSET;                    // Capture and Play Back Offset [7:0]: Offset [7:0] will
                                                                      // determine next line start address, Mapping to 64bits
                                                                      // width data bus field


    typedef UReg<0x04, 0x39, 0, 10> PB_FETCH_NUM;                     // Fetch number: Fetch number [7:0] will determine to fetch
                                                                      // the number of pixels from memory, Mapping to 64bits width
                                                                      // data bus field


    typedef UReg<0x04, 0x3B, 0, 21> PB_CAP_BUF_STA_ADDR_C;            // Capture and Play Back Buffer C Start Address [20:16]
                                                                      // Start address buffer C [20:16] When in noise reduction
                                                                      // mode, this is defined as capture and playback buffer C
                                                                      // start address. Mapping to 32 bits width data bus field


    typedef UReg<0x04, 0x3E, 0, 21> PB_CAP_BUF_STA_ADDR_D;            // Capture and Play Back Buffer D Start Address [20:16]
                                                                      // Start address buffer D [20:16], When in noise reduction
                                                                      // mode, this is defined as capture and playback buffer D
                                                                      // start address. Mapping to 32 bits width data bus field

// WRITE & READ FIFO REGISTERS

    typedef UReg<0x04, 0x41, 0, 8> WFF_TST_REG;                       // WRITE FIFO Test logic control: BIT[7:0] : SELECT CAPTURE
                                                                      // INTERNAL TEST BUS


    typedef UReg<0x04, 0x42, 0, 1> WFF_ENABLE;                        // Enable write FIFO When it’s set 1, write FIFO will be
                                                                      // turn on

    typedef UReg<0x04, 0x42, 1, 1> WFF_FF_HALF_REQ;                   // When it’s set 0, write FIFO will be turn off. Request
                                                                      // generated when FIFO half When set to 1, request generated
                                                                      // when FIFO half. When set to 0, request generate when FIFO
                                                                      // write pointer is 1

    typedef UReg<0x04, 0x42, 2, 1> WFF_FF_STA_INV;                    // Write FIFO status invert When set to 1, write FIFO status
                                                                      // invert

    typedef UReg<0x04, 0x42, 3, 1> WFF_SAFE_GUARD;                    // When set to 0, write FIFO status don’t change. Enable
                                                                      // write FIFO safe guard When set to 1, enable write FIFO
                                                                      // safe guard. When set to 0, disable write FIFO safe guard

    typedef UReg<0x04, 0x42, 4, 1> WFF_VRST_FF_RST;                   // Enable input V-sync reset FIFO When set to 1, enable
                                                                      // feedback v-sync reset FIFO

    typedef UReg<0x04, 0x42, 5, 1> WFF_ADR_ADD_2;                     // When set to 0, disable feedback v-sync reset FIFO. WRITE
                                                                      // FIFO Address count select: When it’s set to 1, address
                                                                      // added by 2 per pixel. When it’s set to 0, address added
                                                                      // by 1 per pixel

    typedef UReg<0x04, 0x42, 6, 1> WFF_REQ_OVER;                      // WRITE FIFO Horizontal Request End When this bit set 1,
                                                                      // the final write FIFO request of one line is in the
                                                                      // horizontal

    typedef UReg<0x04, 0x42, 7, 1> WFF_FF_STATUS_SEL;                 // blank rising edge, set 0 write FIFO request will free run
                                                                      // WRITE FIFO HALF STATUS SELECT When set to 1, request
                                                                      // generated when FIFO is half. When set to 0, request
                                                                      // generated when c FIFO is delm’s value


    typedef UReg<0x04, 0x43, 0, 8> WFF_FF_STATUS;                     // Write FIFO status When wff_cntrl_[15] set 1’b1, this
                                                                      // register will be valid, this value will less than 64


    typedef UReg<0x04, 0x44, 0, 21> WFF_SAFE_GUARD_A;                 // Write FIFO Buffer A Safe Guard Address [20:16] Safe guard
                                                                      // address buffer A [20:16], Mapping to 32bits width data
                                                                      // bus field


    typedef UReg<0x04, 0x47, 0, 21> WFF_SAFE_GUARD_B;                 // WRITE FIFO 06-08, s4_47..49. The pair to _A, and the same
                                                                      // shape. It was missing here because the extraction lost it
                                                                      // too, so every check that compared header against
                                                                      // datasheet agreed -- on a set with the same hole in both.
                                                                      // RD-5725-1.1 documents it in full. Write FIFO Buffer B
                                                                      // Safe Guard Address [20:16] Safe guard address buffer B
                                                                      // [20:16], Mapping to 32bits width data bus field


    typedef UReg<0x04, 0x4A, 0, 1> WFF_YUV_DEINTERLACE;               // WRITE FIFO YUV DE-INTERLACE When set 1, write FIFO will
                                                                      // write one field YUV, set 0, will write one frame Y

    typedef UReg<0x04, 0x4A, 4, 1> WFF_LINE_FLIP;                     // When set 1, line id will be inverted;

    typedef UReg<0x04, 0x4A, 7, 1> WFF_LAST_POP_CTL;                  // When set to 1, horizontal or vertical load start address
                                                                      // will check if there is pop When set to 0, horizontal or
                                                                      // vertical load start address will not check


    typedef UReg<0x04, 0x4B, 0, 3> WFF_HB_DELAY;                      // Write FIFO H-Timing Programmable Delay:

    typedef UReg<0x04, 0x4B, 4, 3> WFF_VB_DELAY;                      // Write FIFO V-Timing Programmable Delay:


    typedef UReg<0x04, 0x4D, 0, 4> RFF_NEW_PAGE;                      // Read buffer page select from 1 to 16 RFF_NEW_PAGE Read
                                                                      // buffer page 0 1 1 2 2 3 3 4 4 5 5 6 6 7 7 8 8 9 9 10 A 11
                                                                      // B 12 C 13 D 14 E 15 F 16

    typedef UReg<0x04, 0x4D, 4, 1> RFF_ADR_ADD_2;                     // Enable read FIFO address add by 2: Default 0 for added by
                                                                      // 1 When set 1, read FIFO address will count by 2

    typedef UReg<0x04, 0x4D, 5, 2> RFF_REQ_SEL;                       // When set 0, read FIFO address will count by 1. Enable
                                                                      // read FIFO request mode RFF_REQ_SEL RFFHREQ RFFLREQ 00 0
                                                                      // Low request 01 0 High request Enable Read FIFO

    typedef UReg<0x04, 0x4D, 7, 1> RFF_ENABLE;                        // When set 1, read FIFO will be turned on; When set 0, read
                                                                      // FIFO will be turned off


    typedef UReg<0x04, 0x4E, 0, 6> RFF_MASTER_FLAG;                   // Master line flag [5:0] Read FIFO policy master value:


    typedef UReg<0x04, 0x4F, 0, 6> RFF_GENERAL_FLAG;                  // General line flag [5:0] Read FIFO policy master value:


    typedef UReg<0x04, 0x50, 0, 4> RFF_TST_REG;                       // General Test Logic [3:0] Read FIFO test bus select

    typedef UReg<0x04, 0x50, 5, 1> RFF_LINE_FLIP;                     // When set 1, line ID will be inverted;

    typedef UReg<0x04, 0x50, 6, 1> RFF_YUV_DEINTERLACE;               // When set 0, line ID will be normal. Read FIFO YUV De-
                                                                      // interlace When set 1, Read FIFO will read Frame 2 YUV
                                                                      // data in line = 1, line =0, read Frame 1 YUV data. When
                                                                      // set 0, Read FIFO will read Frame 2 Y data in line = 1,
                                                                      // line =0 , read Frame 1 Y data

    typedef UReg<0x04, 0x50, 7, 1> RFF_LREQ_CUT;                      // READ FIFO LOW REQUEST CUT ENABLE Cut the read FIFO low
                                                                      // request, only output high request to memory


    typedef UReg<0x04, 0x51, 0, 21> RFF_WFF_STA_ADDR_A;               // Read FIFO and Write FIFO START Address Buffer A [20:16]
                                                                      // Start address buffer A [20:16], Mapping to 32bits width
                                                                      // data bus field


    typedef UReg<0x04, 0x54, 0, 21> RFF_WFF_STA_ADDR_B;               // Read FIFO AND Write FIFO START Address [20:16] Start
                                                                      // address buffer B [20:16], Mapping to 32 bits width data
                                                                      // bus field


    typedef UReg<0x04, 0x57, 0, 10> RFF_WFF_OFFSET;                   // Read FIFO and Write FIFO offset: Offset [7:0] will
                                                                      // determine next line start address, Mapping to 64bits
                                                                      // width data bus field


    typedef UReg<0x04, 0x59, 0, 10> RFF_FETCH_NUM;                    // Fetch number [7:0] (READ FIFO USE ONLY) This will
                                                                      // determine to fetch the number of pixels from memory each
                                                                      // horizontal line. Mapping to 64bits width data bus field


    typedef UReg<0x04, 0x5B, 7, 1> MEM_FF_TOP_FF_SEL;                 // When set 1, all FIFO status output, can read FIFO status
                                                                      // through test bus; When set 0, not FIFO status output.
                                                                      // ISTERS

// PIP REGISTERS

    typedef UReg<0x03, 0x80, 0, 1> PIP_UV_FLIP;                       // 422 to 444 conversion UV flip control This bit is used to
                                                                      // flip UV, when this bit is 1, UV position will be flipped

    typedef UReg<0x03, 0x80, 1, 1> PIP_U_DELAY;                       // UV 422 to 444 conversion U delay When this bit is 1, U
                                                                      // will delay 1 clock, otherwise, no delay for internal pipe

    typedef UReg<0x03, 0x80, 2, 1> PIP_V_DELAY;                       // UV 422 to 444 conversion V delay When this bit is 1, V
                                                                      // will delay 1 clock, otherwise, no delay for internal pipe

    typedef UReg<0x03, 0x80, 3, 1> PIP_TAP3_BYPS;                     // Tap3 filter in 422 to 444 conversion bypass control,
                                                                      // active high This bit is the UV interpolation filter
                                                                      // enable control; when this bit is 1, UV bypass

    typedef UReg<0x03, 0x80, 4, 2> PIP_Y_DELAY;                       // the filter Y compensation delay control bit [1:0] in 422
                                                                      // to 444 conversion To compensation the pipe of UV, program
                                                                      // this field can delay Y from 1 to 4 clocks. PIP_Y_DELAY
                                                                      // [1:0] Y delay 0 0 1 0 1 2 1 0 3 1 1 4 PIP 16-bit sub-
                                                                      // picture select, active high

    typedef UReg<0x03, 0x80, 6, 1> PIP_SUB_16B_SEL;                   // When this bit is 1, select 16-bit sub-picture;

    typedef UReg<0x03, 0x80, 7, 1> PIP_DYN_BYPS;                      // When it is 0, select 24-bit sub-picture. Dynamic range
                                                                      // expansion bypass control, active high When this bit is 1,
                                                                      // data will bypass the dynamic range expansion process


    typedef UReg<0x03, 0x81, 0, 1> PIP_CONVT_BYPS;                    // YUV to RGB color space conversion bypass control, active
                                                                      // high When this bit is 1, YUV data will bypass the YUV to
                                                                      // RGB conversion, the output will still be YUV data. When
                                                                      // this bit is 0, YUV data will do YUV to RGB conversion,
                                                                      // the output will be

    typedef UReg<0x03, 0x81, 3, 1> PIP_DREG_BYPS;                     // When this bit is 0, input data will triggered by falling
                                                                      // edge clock

    typedef UReg<0x03, 0x81, 7, 1> PIP_EN;                            // When this bit is 1, PIP insertion is enabled, otherwise,
                                                                      // no PIP


    typedef UReg<0x03, 0x82, 0, 8> PIP_Y_GAIN;                        // Y dynamic range expansion gain control bit [7:0] This
                                                                      // field contains the Y gain value in dynamic range
                                                                      // expansion process, its range is (0 ~ 2)*128


    typedef UReg<0x03, 0x83, 0, 8> PIP_U_GAIN;                        // U dynamic range expansion gain control bit [7:0] This
                                                                      // field contains the U gain value in dynamic range
                                                                      // expansion process, its range is (0 ~ 4)*64


    typedef UReg<0x03, 0x84, 0, 8> PIP_V_GAIN;                        // V dynamic range expansion gain control bit [7:0] This
                                                                      // field contains the V gain value in dynamic range
                                                                      // expansion process, its range is (0 ~ 4)*64


    typedef UReg<0x03, 0x85, 0, 8> PIP_Y_OFST;                        // Y dynamic range expansion offset control bit [7:0] This
                                                                      // field contains the Y offset value in dynamic range
                                                                      // expansion process, its range is –128 ~ 127


    typedef UReg<0x03, 0x86, 0, 8> PIP_U_OFST;                        // U dynamic range expansion offset control bit [7:0] This
                                                                      // field contains the U offset value in dynamic range
                                                                      // expansion process, its range is –128 ~ 127


    typedef UReg<0x03, 0x87, 0, 8> PIP_V_OFST;                        // V dynamic range expansion offset control bit [7:0] This
                                                                      // field contains the V offset value in dynamic range
                                                                      // expansion process, its range is –128 ~ 127


    typedef UReg<0x03, 0x88, 0, 12> PIP_H_ST;                         // PIP window horizontal start position control bit [7:0]
                                                                      // This field contains the horizontal start position of PIP
                                                                      // window


    typedef UReg<0x03, 0x8A, 0, 12> PIP_H_SP;                         // PIP window horizontal stop position control bit [7:0]
                                                                      // This field contains the horizontal stop position of PIP
                                                                      // window


    typedef UReg<0x03, 0x8C, 0, 11> PIP_V_ST;                         // PIP window vertical start position control bit [7:0] This
                                                                      // field contains the vertical start position of PIP window


    typedef UReg<0x03, 0x8E, 0, 11> PIP_V_SP;                         // PIP window vertical stop position control bit [7:0] This
                                                                      // field contains the vertical stop position of PIP window

// OSD REGISTERS

    typedef UReg<0x00, 0x90, 0, 1> OSD_SW_RESET;                      // Software reset for module , active high When this bit is
                                                                      // 1, it reset osd_top module

    typedef UReg<0x00, 0x90, 1, 3> OSD_HORIZONTAL_ZOOM;               // Osd horizontal zoom select OSD_HORIZONTAL_ZOOM [2:0] SIZE
                                                                      // 0 0 0 Orignal size 0 0 1 2 0 1 0 3 0 1 1 4 1 0 0 5 1 0 1
                                                                      // 6

    typedef UReg<0x00, 0x90, 4, 2> OSD_VERTICAL_ZOOM;

    typedef UReg<0x00, 0x90, 6, 1> OSD_DISP_EN;                       // 1 1 0 7 1 1 1 8 Osd vertical zoom select
                                                                      // OSD_VERTICAL_ZOOM [1:0] SIZE 0 0 1 1 0 3 1 1 4 Osd
                                                                      // display enable, active high When this bit is 1, osd can
                                                                      // display on screen

    typedef UReg<0x00, 0x90, 7, 1> OSD_MENU_EN;                       // Osd menu display enable, active high When this bit is 1,
                                                                      // osd state will jump to menu display state


    typedef UReg<0x00, 0x91, 0, 4> OSD_MENU_ICON_SEL;

    typedef UReg<0x00, 0x91, 4, 4> OSD_MENU_MOD_SEL;                  // Osd menu icons select OSD_MENU_ICON_SEL [3:0] Select icon
                                                                      // 0 0 0 1 Brightness icon 0 0 1 0 Contrast icon 0 0 1 1 Hue
                                                                      // icon 0 1 0 0 Sound icon 1 0 0 1 Left/right moving icon 1
                                                                      // 0 1 0 Vertical size icon 1 0 1 1 Horizontal size icon
                                                                      // Reserved , if SEL[3:0] = 4’h0, others Nothing is selected
                                                                      // Osd icons modification select OSD_MENU_MOD_SEL [3:0]
                                                                      // Select icon 0 0 0 1 Brightness icon 0 0 1 0 Contrast icon
                                                                      // 0 0 1 1 Hue icon 0 1 0 0 Sound icon 1 0 0 0 Up/down
                                                                      // moving icon 1 0 0 1 Left/right moving icon 1 0 1 0
                                                                      // Vertical size icon 1 0 1 1 Horizontal size icon Reserved
                                                                      // , if MOD[3:0] = 4’h0, others Nothing is selected


    typedef UReg<0x00, 0x92, 0, 3> OSD_MENU_BAR_FONT_FORCOR;          // Menu font or bar foreground color. OSD_MENU_BAR_FONT_ For
                                                                      // bar and menu will not display on screen at the same time,
                                                                      // so they are shared

    typedef UReg<0x00, 0x92, 3, 3> OSD_MENU_BAR_FONT_BGCOR;           // Menu font or bar background color. For bar and menu will
                                                                      // not display on screen at the same time, so they are
                                                                      // shared. Menu or bar border color. It is the low 2 bits of
                                                                      // menu or bar border color, for bar and menu will not
                                                                      // display _COR [1:0]

    typedef UReg<0x00, 0x92, 6, 3> OSD_MENU_BAR_BORD_COR;


    typedef UReg<0x00, 0x93, 1, 3> OSD_MENU_SEL_FORCOR;               // Selected icon or bar’s icon foreground color

    typedef UReg<0x00, 0x93, 4, 3> OSD_MENU_SEL_BGCOR;                // Selected icon or bar’s icon background color. Command
                                                                      // finished status

    typedef UReg<0x00, 0x93, 7, 1> OSD_COMMAND_FINISH;


    typedef UReg<0x00, 0x94, 0, 1> OSD_MENU_DISP_STYLE;               // Menu display in row or column mode. When 1, osd menu
                                                                      // displays in row style, else in column style

    typedef UReg<0x00, 0x94, 2, 1> OSD_YCBCR_RGB_FORMAT;              // YCbCr or RGB output. Osd display in YCbCr or RGB format,
                                                                      // when set to 1, display in YCbCr mode

    typedef UReg<0x00, 0x94, 3, 1> OSD_INT_NG_LAT;                    // V2clk latch osd data with negative enable. When set to 1,
                                                                      // V2CLK clock can latch osd data with negative edge

    typedef UReg<0x00, 0x94, 4, 4> OSD_TEST_SEL;                      // Test logic output select. TEST_SEL[0], test logic output
                                                                      // enable, when set to 1, test logic can output.
                                                                      // TEST_SEL[3:1] select 8 test logics to test bus


    typedef UReg<0x00, 0x95, 0, 8> OSD_MENU_HORI_START;               // Menu or bar horizontal start address The real address is
                                                                      // { MENU_BAR_HORZ_START [7:0], 3’h0}


    typedef UReg<0x00, 0x96, 0, 8> OSD_MENU_VER_START;                // Menu or bar vertical start address The real address is {
                                                                      // MENU_BAR_VIRT_START [7:0], 3’h0}


    typedef UReg<0x00, 0x97, 0, 8> OSD_BAR_LENGTH;                    // BAR DISPLAY TOTAL LENGTH Bar display on screen’s total
                                                                      // length, when horizontal zoom is 0


    typedef UReg<0x00, 0x98, 0, 8> OSD_BAR_FOREGROUND_VALUE;

// MODE_DETECT REGISTERS

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

// ADC REGISTERS

    typedef UReg<0x05, 0x00, 0, 8> ADC_5_00;

    typedef UReg<0x05, 0x00, 0, 2> ADC_CLK_PA;                        // Clock selection for PA_ADC When = 00, PA_ADC input clock
                                                                      // is from PLLAD’s CLKO2 When = 01, PA_ADC input clock is
                                                                      // from PCLKIN When = 10, PA_ADC input clock is from V4CLK

    typedef UReg<0x05, 0x00, 2, 1> ADC_CLK_PLLAD;                     // When = 11, reserved Clock selection for PLLAD When = 0,
                                                                      // PLLAD input clock is from sync processor When = 1, PLLAD
                                                                      // input clock is from OSC

    typedef UReg<0x05, 0x00, 3, 1> ADC_CLK_ICLK2X;                    // ICLK2X control When = 0, ICLK2X = ADC output clock

    typedef UReg<0x05, 0x00, 4, 1> ADC_CLK_ICLK1X;                    // When = 1, ICLK2X = ADC output clock / 2 ICLK1X control
                                                                      // When = 0, ICLK1X = ICLK2X When = 1, ICLK1X = ICLK2X /2


    typedef UReg<0x05, 0x02, 0, 1> ADC_SOGEN;                         // ADC SOG enable When = 0, ADC disable SOG mode

    typedef UReg<0x05, 0x02, 1, 5> ADC_SOGCTRL;                       // When = 1, ADC enable SOG mode SOG control signal ADC
                                                                      // input selection When = 00, R0/G0/B0/SOG0 as input

    typedef UReg<0x05, 0x02, 6, 2> ADC_INPUT_SEL;                     // When = 01, R1/G1/B1/SOG1 as input When = 10, R2/G2/B2 as
                                                                      // input When = 11, reserved


    typedef UReg<0x05, 0x03, 0, 8> ADC_5_03;

    typedef UReg<0x05, 0x03, 0, 1> ADC_POWDZ;                         // ADC power down control When = 0, ADC in power down mode

    typedef UReg<0x05, 0x03, 1, 1> ADC_RYSEL_R;                       // When = 1, ADC work normally Clamp to ground or midscale
                                                                      // for R ADC When = 0, clamp to GND When = 1, clamp to
                                                                      // midscale

    typedef UReg<0x05, 0x03, 2, 1> ADC_RYSEL_G;                       // Clamp to ground or midscale for G ADC When = 0, clamp to
                                                                      // GND

    typedef UReg<0x05, 0x03, 3, 1> ADC_RYSEL_B;                       // When = 1, clamp to midscale Clamp to ground or midscale
                                                                      // for B ADC When = 0, clamp to GND When = 1, clamp to
                                                                      // midscale

    typedef UReg<0x05, 0x03, 4, 2> ADC_FLTR;                          // ADC internal filter control When = 00, 150MHz When = 01,
                                                                      // 110MHz When = 10, 70MHz


    typedef UReg<0x05, 0x04, 0, 8> ADC_TEST_04;

    typedef UReg<0x05, 0x04, 0, 2> ADC_TR_RSEL;                       // REF test resistor selection

    typedef UReg<0x05, 0x04, 2, 3> ADC_TR_ISEL;                       // REF test currents selection


    typedef UReg<0x05, 0x05, 0, 8> ADC_TA_05_CTRL;

    typedef UReg<0x05, 0x05, 0, 1> ADC_TA_EN;                         // ADC test enable When = 0, ADC work normally

    typedef UReg<0x05, 0x05, 1, 4> ADC_TA_CTRL;                       // When = 1, ADC is in test mode ADC test bus control bit


    typedef UReg<0x05, 0x06, 0, 7> ADC_ROFCTRL;                       // Offset control for R channel of ADC


    typedef UReg<0x05, 0x07, 0, 7> ADC_GOFCTRL;                       // Offset control for G channel of ADC


    typedef UReg<0x05, 0x08, 0, 7> ADC_BOFCTRL;                       // Offset control for B channel of ADC


    typedef UReg<0x05, 0x09, 0, 8> ADC_RGCTRL;                        // Gain control for R channel of ADC


    typedef UReg<0x05, 0x0A, 0, 8> ADC_GGCTRL;                        // Gain control for G channel of ADC


    typedef UReg<0x05, 0x0B, 0, 8> ADC_BGCTRL;                        // Gain control for B channel of ADC


    typedef UReg<0x05, 0x0C, 0, 8> ADC_TEST_0C;

    typedef UReg<0x05, 0x0C, 0, 1> ADC_CKBS;                          // ADC output clock invert control When = 0, default

    typedef UReg<0x05, 0x0C, 1, 4> ADC_TEST;                          // When = 1, ADC output clock will be invert For ADC test
                                                                      // reserved


    typedef UReg<0x05, 0x0E, 0, 1> ADC_AUTO_OFST_EN;                  // Auto offset adjustment enable When = 0, auto offset
                                                                      // adjustment disable

    typedef UReg<0x05, 0x0E, 1, 1> ADC_AUTO_OFST_PRD;                 // When = 1, auto offset adjustment enable Offset adjustment
                                                                      // by frame When = 0, offset adjustment by frame When = 1,
                                                                      // offset adjustment by line

    typedef UReg<0x05, 0x0E, 2, 2> ADC_AUTO_OFST_DELAY;               // Horizontal sample delay control When = 00, offset
                                                                      // adjustment horizontal sample delay 1 pipe When = 01,
                                                                      // offset adjustment horizontal sample delay 2 pipe When =
                                                                      // 10, offset adjustment horizontal sample delay 3 pipe

    typedef UReg<0x05, 0x0E, 4, 2> ADC_AUTO_OFST_STEP;                // When = 11, offset adjustment horizontal sample delay 4
                                                                      // pipe Offset adjustment step control When = 00, offset
                                                                      // adjustment by absolute difference. When = 01, offset
                                                                      // adjustment by 1 When = 10, offset adjustment by 2 When =
                                                                      // 11, offset adjustment by 3

    typedef UReg<0x05, 0x0E, 7, 1> ADC_AUTO_OFST_TEST;                // Auto offset adjustment test control


    typedef UReg<0x05, 0x0F, 0, 8> ADC_AUTO_OFST_RANGE_REG;

    typedef UReg<0x05, 0x0F, 0, 4> ADC_AUTO_OFST_U_RANGE;             // U channel offset detection range Define U channel offset
                                                                      // detection range 0~15

    typedef UReg<0x05, 0x0F, 4, 4> ADC_AUTO_OFST_V_RANGE;             // V channel offset detection range Define V channel offset
                                                                      // detection range 0~15


    typedef UReg<0x05, 0x11, 0, 8> PLLAD_CONTROL_00_5x11;

    typedef UReg<0x05, 0x11, 0, 1> PLLAD_VCORST;                      // VCORST Initial VCO control voltage

    typedef UReg<0x05, 0x11, 1, 1> PLLAD_LEN;                         // LEN Enable signal for clock

    typedef UReg<0x05, 0x11, 2, 1> PLLAD_TEST;                        // TEST Test clock selection

    typedef UReg<0x05, 0x11, 3, 1> PLLAD_TS;                          // TS Test clock selection and HSL clock selection

    typedef UReg<0x05, 0x11, 4, 1> PLLAD_PDZ;                         // PDZ When = 0, PLLAD is power down mode

    typedef UReg<0x05, 0x11, 5, 1> PLLAD_FS;                          // When = 1, PLLAD work normally FS, VCO gain selection When
                                                                      // = 0, default When = 1, high gain selected

    typedef UReg<0x05, 0x11, 6, 1> PLLAD_BPS;                         // BPS When = 0, default

    typedef UReg<0x05, 0x11, 7, 1> PLLAD_LAT;                         // When = 1, bypass input clock to CKO1 and CKO2 Latch
                                                                      // control for PLLAD control This bit’s rising edge is used
                                                                      // to trigger PLLAD control bit: ND, MD, KS, CKOS, ICP


    typedef UReg<0x05, 0x12, 0, 12> PLLAD_MD;                         // MD[11:8] PLLAD feedback divider control


    typedef UReg<0x05, 0x14, 0, 12> PLLAD_ND;                         // ND[11:8] PLLAD input divider control


    typedef UReg<0x05, 0x16, 0, 8> PLLAD_5_16;

    typedef UReg<0x05, 0x16, 0, 2> PLLAD_R;                           // R Skew control for testing

    typedef UReg<0x05, 0x16, 2, 2> PLLAD_S;                           // S Skew control for testing

    typedef UReg<0x05, 0x16, 4, 2> PLLAD_KS;                          // KS VCO post divider control, it is determined by CKO
                                                                      // frequency When = 00, divide by 1 (162MHz~80MHz) When =
                                                                      // 01, divide by 2 (80MHz~40MHz) When = 10, divide by 4
                                                                      // (40MHz~20MHz) When = 11, divide by 8 (20MHz~min MHz)

    typedef UReg<0x05, 0x16, 6, 2> PLLAD_CKOS;                        // Part of PLLAD_CKOS, which RD-5725-1.1 documents as one
                                                                      // 2-bit block at s5_16 rather than field by field.


    typedef UReg<0x05, 0x17, 0, 3> PLLAD_ICP;                         // ICP Charge pump current selection When = 000, Icp = 50uA
                                                                      // When = 001, Icp = 100uA When = 010, Icp = 150uA When =
                                                                      // 011, Icp = 250uA When = 100, Icp = 350uA When = 101, Icp
                                                                      // = 500uA When = 110, Icp = 750uA When = 111, Icp = 1mA


    typedef UReg<0x05, 0x18, 0, 1> PA_ADC_BYPSZ;                      // BYPSZ, PA for ADC bypass control When = 0, PA_ADC is
                                                                      // bypass

    typedef UReg<0x05, 0x18, 1, 5> PA_ADC_S;                          // When = 1, PA_ADC work normally PA_ADC phase control
                                                                      // LOCKOFF

    typedef UReg<0x05, 0x18, 6, 1> PA_ADC_LOCKOFF;                    // When = 0, default

    typedef UReg<0x05, 0x18, 7, 1> PA_ADC_LAT;                        // When = 1, PA_ADC lock circuit disable PA_ADC latch signal
                                                                      // This bit’s rising edge is used to trigger
                                                                      // PA_ADC_CNTRL_[5:1]


    typedef UReg<0x05, 0x19, 0, 1> PA_SP_BYPSZ;                       // BYPSZ, PA for PLLAD bypass control When = 0, PA_PLLAD is
                                                                      // bypass

    typedef UReg<0x05, 0x19, 1, 5> PA_SP_S;                           // When = 1, PA_PLLAD work normally PA_PLLAD phase control
                                                                      // LOCKOFF

    typedef UReg<0x05, 0x19, 6, 1> PA_SP_LOCKOFF;                     // When = 0, default

    typedef UReg<0x05, 0x19, 7, 1> PA_SP_LAT;                         // When = 1, PA_PLLAD lock circuit disable PA_PLLAD latch
                                                                      // signal This bit’s rising edge is used to trigger
                                                                      // PA_PLLAD_CNTRL_[5:1]


    typedef UReg<0x05, 0x1E, 7, 1> DEC_WEN_MODE;                      // Write enable mode enable. When this bit is 1, then
                                                                      // decimator will drop data by write enable signal generated
                                                                      // by horizontal sync, else write enable is not used


    typedef UReg<0x05, 0x1F, 0, 8> DEC_5_1F;

    typedef UReg<0x05, 0x1F, 0, 1> DEC1_BYPS;                         // The 4x to 2x decimator bypass enable When 1, the 4x to 2x
                                                                      // decimator bypass

    typedef UReg<0x05, 0x1F, 1, 1> DEC2_BYPS;                         // The 2x to 1x decimator bypass enable When 1, the 2x to 1x
                                                                      // decimator hypass

    typedef UReg<0x05, 0x1F, 2, 1> DEC_MATRIX_BYPS;                   // Color space convert bypass enable When set to 1, color
                                                                      // space convert module bypass

    typedef UReg<0x05, 0x1F, 3, 4> DEC_TEST_SEL;

    typedef UReg<0x05, 0x1F, 3, 1> DEC_TEST_ENABLE;

    typedef UReg<0x05, 0x1F, 7, 1> DEC_IDREG_EN;

// SYNC_PROC REGISTERS

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


    typedef UReg<0x05, 0x60, 0, 8> ADC_UNUSED_60;


    typedef UReg<0x05, 0x61, 0, 8> ADC_UNUSED_61;


    typedef UReg<0x05, 0x62, 0, 8> ADC_UNUSED_62;


    typedef UReg<0x05, 0x63, 0, 8> TEST_BUS_SP_SEL;

    typedef UReg<0x05, 0x63, 0, 1> SP_TEST_EN;                        // Test control Test bus enable

    typedef UReg<0x05, 0x63, 1, 3> SP_TEST_MODULE;                    // Test control test module select # 0 none # 1 hs_pol_det
                                                                      // module # 2 hs_act_det module # 3 vs_pol_det module # 4
                                                                      // vs_act_det module # 5 cs_sep module # 6 retiming module #
                                                                      // 7 out proc module

    typedef UReg<0x05, 0x63, 4, 3> SP_TEST_SIGNAL_SEL;                // Test control Test signal select


    typedef UReg<0x05, 0x64, 0, 8> ADC_UNUSED_64;


    typedef UReg<0x05, 0x65, 0, 8> ADC_UNUSED_65;


    typedef UReg<0x05, 0x66, 0, 8> ADC_UNUSED_66;


    typedef UReg<0x05, 0x67, 0, 16> ADC_UNUSED_67;


    typedef UReg<0x05, 0x69, 0, 8> ADC_UNUSED_69;


















    static const uint8_t OSD_ZOOM_1X = 0;
    static const uint8_t OSD_ZOOM_2X = 1;
    static const uint8_t OSD_ZOOM_3X = 2;
    static const uint8_t OSD_ZOOM_4X = 3;
    static const uint8_t OSD_ZOOM_5X = 4;
    static const uint8_t OSD_ZOOM_6X = 5;
    static const uint8_t OSD_ZOOM_7X = 6;
    static const uint8_t OSD_ZOOM_8X = 7;

    static const uint8_t OSD_MENU_DISP_STYLE_VERTICAL = 0;
    static const uint8_t OSD_MENU_DISP_STYLE_HORIZONTAL = 1;

    static const uint8_t OSD_ICON_NONE = 0;
    static const uint8_t OSD_ICON_BRIGHTNESS = 1;
    static const uint8_t OSD_ICON_CONTRAST = 2;
    static const uint8_t OSD_ICON_HUE = 3;
    static const uint8_t OSD_ICON_SOUND = 4;
    static const uint8_t OSD_ICON_UP_DOWN = 8;
    static const uint8_t OSD_ICON_LEFT_RIGHT = 9;
    static const uint8_t OSD_ICON_VERTICAL_SIZE = 10;
    static const uint8_t OSD_ICON_HORIZONTAL_SIZE = 11;
    static const uint8_t OSD_ICON_COUNT = 8;

    static inline uint8_t osdIcon(uint8_t index)
    {
        static const uint8_t osdIcons[8] = {
            OSD_ICON_BRIGHTNESS,
            OSD_ICON_CONTRAST,
            OSD_ICON_HUE,
            OSD_ICON_SOUND,
            OSD_ICON_UP_DOWN,
            OSD_ICON_LEFT_RIGHT,
            OSD_ICON_VERTICAL_SIZE,
            OSD_ICON_HORIZONTAL_SIZE,
        };
        return osdIcons[index];
    }

    static const uint8_t OSD_COLOR_BLACK = 0;
    static const uint8_t OSD_COLOR_BLUE = 1;
    static const uint8_t OSD_COLOR_GREEN = 2;
    static const uint8_t OSD_COLOR_CYAN = 3;
    static const uint8_t OSD_COLOR_RED = 4;
    static const uint8_t OSD_COLOR_MAGENTA = 5;
    static const uint8_t OSD_COLOR_YELLOW = 6;
    static const uint8_t OSD_COLOR_WHITE = 7;

    static const uint8_t OSD_FORMAT_YCBCR = 1;
    static const uint8_t OSD_FORMAT_RGB = 0;
};

} // namespace Tv5725

#endif
