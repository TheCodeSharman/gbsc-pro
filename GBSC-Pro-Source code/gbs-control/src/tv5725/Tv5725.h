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

                                                                      // 8-bit block at s0_0F rather than field by field.

                                                                      // 8-bit block at s0_0F rather than field by field.

                                                                      // INT_STATUS_[2]]

                                                                      // INT_STATUS_[3]]

                                                                      // 8-bit block at s0_0F rather than field by field.

                                                                      // stable and unstable [datasheet: INT_STATUS_[5]]

                                                                      // stable and unstable [datasheet: INT_STATUS_[6]]

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





    typedef UReg<0x01, 0x2C, 0, 1> GBS_OPTION_SCANLINES_ENABLED;

    typedef UReg<0x01, 0x2C, 1, 1> GBS_OPTION_SCALING_RGBHV;






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


// MEMORY REGISTERS


// CAPTURE & PLAYBACK REGISTERS


// WRITE & READ FIFO REGISTERS


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

// ADC REGISTERS


    typedef UReg<0x05, 0x1E, 7, 1> DEC_WEN_MODE;                      // Write enable mode enable. When this bit is 1, then
                                                                      // decimator will drop data by write enable signal generated
                                                                      // by horizontal sync, else write enable is not used


    typedef UReg<0x05, 0x1F, 0, 8> DEC_5_1F;

                                                                      // space convert module bypass

    typedef UReg<0x05, 0x1F, 7, 1> DEC_IDREG_EN;

// SYNC_PROC REGISTERS


    typedef UReg<0x05, 0x60, 0, 8> ADC_UNUSED_60;


    typedef UReg<0x05, 0x61, 0, 8> ADC_UNUSED_61;


    typedef UReg<0x05, 0x62, 0, 8> ADC_UNUSED_62;


    typedef UReg<0x05, 0x63, 0, 8> TEST_BUS_SP_SEL;


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
