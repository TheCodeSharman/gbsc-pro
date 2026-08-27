#ifndef TV5725_MEMORY_BUS_H
#define TV5725_MEMORY_BUS_H

#include "Tv5725.h"

#include <stdint.h>

namespace Tv5725 {

// The SDRAM bus itself: the clock it runs at, and the nanosecond trims that
// compensate this board's traces. Where MemoryMap says what lives at which
// address, this says how the wires under it are driven.
//
// The three delay lines are a property of the memory chip and the board traces
// rather than of the output resolution. Swept one at a time to both ends of
// range the picture is unchanged at every step, and a delay line's working
// region is contiguous, so none of the three tunes anything here. That sweep
// was at 129.6 MHz, a 7.72 ns clock period against 6.17 ns at 162 -- re-take it
// before relying on the margin at a higher clock.
//
// doPostPresetLoadSteps() re-initialises the part at whatever clock this
// selected, so no re-init is needed here.
class MemoryBus {
public:
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

    typedef UReg<0x04, 0x5B, 7, 1> MEM_FF_TOP_FF_SEL;                 // When set 1, all FIFO status output, can read FIFO status
                                                                      // through test bus; When set 0, not FIFO status output.
                                                                      // ISTERS

    // Bring up the SDRAM bus: the clock, the part's mode register, its timing,
    // the address mapping, the arbitration and the board's delay trim.
    static void init();

    // Run the bus off the FBCLK pin instead of the derived clock. Bypass takes
    // the frame buffer out of the video path, so what clocks it stops mattering
    // and the pad's own return path is what the bypass switches ask for.
    //
    // The scaling path does not have to undo this: init() picks the clock from
    // SdramTimings again, and BringUp::arm() is the first statement in either
    // switch. docs/investigations/pll-ms-has-four-writers.md
    static void useFeedbackClock();
};

}  // namespace Tv5725

#endif
