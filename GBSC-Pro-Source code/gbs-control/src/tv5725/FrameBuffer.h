#ifndef TV5725_FRAME_BUFFER_H
#define TV5725_FRAME_BUFFER_H

#include "Tv5725.h"

#include <stdint.h>

namespace Tv5725 {

// The SDRAM frame buffer subsystem: where capture, playback and the
// deinterlacer's field store live in memory, and the guards that bound them.
//
// The numbers, and every reason for them, are in MemoryMap: this class is only
// the register traffic, so that MemoryMap stays pure arithmetic.
//
// ORDER WITHIN init() IS LOAD-BEARING: the constants transcribed from the preset
// tables run FIRST, so the derived map wins where the two overlap.
//
// The registers this moves are ones the picture does not depend on, checked one
// at a time against a frozen source.
class FrameBuffer {
public:
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

    // Write the memory map and arm its guards. Called from the bring-up where
    // a preset table would have been loaded, and BEFORE the engine solves the
    // raster and the windows.
    static void init();

    // Capture off stops the SDRAM being written, so the playback stage keeps
    // showing the last frame. Held across a mode change, whose windows land
    // seconds after the load.
    static void freezeCapture();
    static void releaseCapture();

    // How the capture, playback and read FIFOs ask for the memory bus: when a
    // request is raised, which of the two playback requests carries it, and
    // whether the refresh generator runs.
    static void applyRequestModes();

    // The line stride the read and write FIFOs advance by. The deinterlacer
    // sets its own while it is running.
    static void writeFifoLineOffset(uint16_t offset);
};

}  // namespace Tv5725

#endif
