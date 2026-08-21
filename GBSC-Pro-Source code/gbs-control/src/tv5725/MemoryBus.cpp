#include "MemoryBus.h"

#include "SdramTimings.h"
#include "../../gbs_types.h"

namespace Tv5725 {

void MemoryBus::init()
{
    // Nothing is read back off the chip here: a subsystem taking its own inputs
    // from registers is one that stops working when whatever wrote them goes
    // away. The derivation runs the other way -- pick the fastest clock the
    // part's tCK allows, then compute the activate and precharge counts that
    // cover tRCD and tRP at it. See SdramTimings.
    const uint8_t clock = SdramTimings::fastestInSpec();
    GBS::PLL_MS::write(clock);

    // --- The SDRAM's own mode register, driven during the Load Mode cycle ---
    //
    // 0x30 is [6:4] latency = 3 and [2:0] burst length = 1, wrap sequential. CAS
    // latency 3 is what makes 162 MHz legal at all -- the part's 6 ns tCK is the
    // CL3 figure -- so this byte and the clock are one decision.
    MEM_MODE_REG::write(0x30);           // s4_01[15:0]
    MEM_MODE_BA0::write(0x0);            // s4_03[0:0]
    MEM_MODE_BA1::write(0x0);            // s4_03[1:1]
    MEM_MODE_CS0::write(0x0);            // s4_03[2:2]
    MEM_MODE_CS1::write(0x0);            // s4_03[3:3]
    MEM_MODE_CYC::write(0x0);            // s4_03[4:4]
    MEM_INI_REF_CYC::write(0x0);         // s4_03[5:5]

    // --- Core timing, against the part's AC characteristics ---
    //
    // MEM_FK_RD_DLY is one of the three board trims, sharing this byte with the
    // read latency pipe. Both ends of its range measured clean, so 2 is
    // continuity rather than tuning -- see the header.
    MEM_FK_RD_DLY::write(2);             // s4_04[2:0]
    MEM_RD_LAT_PIP::write(0x3);          // s4_04[6:4]
    // tRCD and tRP, computed for the clock chosen above rather than inherited.
    MEM_ACT_CYCLE::write(SdramTimings::actCycleRegister(clock));    // s4_05[1:0]
    MEM_PCHG_CYCLE::write(SdramTimings::pchgCycleRegister(clock));  // s4_05[5:4]
    MEM_REF_RATE::write(0x2);            // s4_06[2:0]
    MEM_REF_CYCLE::write(0x4);           // s4_06[6:4]
    MEM_TWR_SEL::write(0x0);             // s4_07[2:0]
    MEM_TRAS_SEL::write(0x3);            // s4_07[6:4]
    MEM_R2W_NOP_CYC::write(0x1);         // s4_08[1:0]
    MEM_W2R_SEL_CYC::write(0x0);         // s4_08[5:4]

    // --- How a word address maps onto bank, row and column ---
    //
    // This is the part-specific half: one EM638325TS is 512K x 32 x 4 banks on
    // a single chip select, and these say so. MEM_MBUS32OR16_SEL below is the
    // 32-bit bus width that makes a pixel cost one word (see MemoryMap).
    MEM_BK0_SEL::write(0x0);             // s4_09[1:0]
    MEM_BK1_SEL::write(0x1);             // s4_09[3:2]
    MEM_CS0_SEL::write(0x1);             // s4_09[5:4]
    MEM_CS1_SEL::write(0x2);             // s4_09[7:6]
    MEM_COL_ST_SEL::write(0x1);          // s4_0a[1:0]
    MEM_ROW_ST_SEL::write(0x1);          // s4_0a[5:4]
    MEM_ADR_REG::write(0x7F);            // s4_0b[6:0]
    MEM_COL_ADR_VLD::write(0x0);         // s4_0c[3:0]
    MEM_SPECIAL_PIN::write(0x4);         // s4_0d[2:0]
    MEM_BA_ADR11_SEL::write(0x1);        // s4_0d[4:4]
    MEM_CS0_BA0_SEL::write(0x1);         // s4_0d[5:5]
    MEM_CS1_BA1_SEL::write(0x1);         // s4_0d[6:6]
    MEM_CMD_PIPE::write(0x0);            // s4_0e[2:0]
    MEM_FST_REG::write(0x6);             // s4_0f[3:0]
    MEM_MISC_REG::write(0x0);            // s4_10[4:0]

    // --- The feedback clock path ---
    //
    // FBCLK is pin 110, and PLL_MS = 010 would take the memory clock FROM it.
    // We never select that (SdramTimings refuses to derive an unknown
    // frequency), but the return path is still how read data gets latched, so
    // these are configured regardless.
    MEM_FBK_CLK_SEL::write(0x0);         // s4_11[0:0]
    MEM_FBK_SEL_MCLK::write(0x1);        // s4_11[1:1]
    MEM_FBK_CS2_SEL::write(0x0);         // s4_11[2:2]
    MEM_FBK_INV_PATH_SEL::write(0x1);    // s4_11[4:4]
    MEM_FBK_PATH_SEL::write(0x1);        // s4_11[7:7]
    MEM_INTER_DLYCELL_SEL::write(0x1);   // s4_12[0:0]
    MEM_CLK_DLYCELL_SEL::write(0x0);     // s4_12[1:1]
    MEM_FBK_CLK_DLYCELL_SEL::write(0x1); // s4_12[2:2]
    // No owner at all, invisibly: s4_13 is written twice just below, so the byte
    // always looked configured while only bit 0 was not. Both its other writers
    // are bypass entries writing 0, and the preset table put it back.
    MEM_PAD_CLK_INVERT::write(0x1);      // s4_13[0:0]
    MEM_RD_DATA_CLK_INVERT::write(0x0);  // s4_13[1:1]
    MEM_FBK_CLK_INVERT::write(0x0);      // s4_13[2:2]
    MEM_NEW_FUNC_CTL::write(0x6);        // s4_14[2:0]
    MEM_WRITE_CYCL_CTL::write(0x1);      // s4_14[4:4]
    MEM_MBUS32OR16_SEL::write(0x1);      // s4_14[7:7]

    // --- Who wins the bus ---
    //
    // Capture writes, playback reads and the deinterlacer's field store share
    // one SDRAM, and these decide the priority between them. Left exactly as
    // measured; membus.py drives them on a live unit and
    // docs/investigations/hscale-tearing-characterisation.md is the measurement.
    MEM_REQ_PBH_RFFH::write(0x1);        // s4_15[0:0]
    MEM_REQ_PB_RFF_CAP::write(0x0);      // s4_15[1:1]
    MEM_REQ_WFF_CAP::write(0x1);         // s4_15[2:2]
    MEM_TEST_SEL::write(0x0);            // s4_16[2:0]

    // --- Per-pin delay trim, in nanoseconds ---
    //
    // One shared encoding: 000 = 0.00 ns, 010 = 0.50, 100 = 1.00, 110 = 2.00,
    // 111 = 3.00. These compensate the board's traces, so they are constants
    // rather than a preset's business -- a PCB trace does not get longer when
    // the output resolution does.
    MEM_WOEZ_DLY::write(0x0);            // s4_17[2:0]
    MEM_WOEZ_PIP::write(0x0);            // s4_17[4:4]
    MEM_WOEZ_SEL_DLYCELL::write(0x0);    // s4_17[5:5]
    MEM_DATA_DLY_REG::write(0);          // s4_18[2:0]  0.00 ns
    MEM_WR_DATA_PIP::write(0x0);         // s4_18[5:4]
    MEM_RAS_DLY_REG::write(0x0);         // s4_19[2:0]
    MEM_CAS_DLY_REG::write(0x0);         // s4_19[6:4]
    MEM_WE_DLY_REG::write(0x0);          // s4_1a[2:0]
    MEM_DQM_DLY_REG::write(0x5);         // s4_1a[6:4]
    MEM_ADR_DLY_REG::write(0x1);         // s4_1b[2:0]  0.25 ns
    MEM_CLK_DLY_REG::write(4);           // s4_1b[6:4]  1.00 ns
    MEM_CS0_DLY_REG::write(0x0);         // s4_1c[2:0]
    MEM_CS1_DLY_REG::write(0x0);         // s4_1c[6:4]
    MEM_BA0_DLY_REG::write(0x0);         // s4_1d[2:0]
    MEM_BA1_DLY_REG::write(0x0);         // s4_1d[6:4]
}

}  // namespace Tv5725
