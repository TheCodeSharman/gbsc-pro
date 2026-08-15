#include "Chip.h"

#include "../../gbs_types.h"

namespace Tv5725 {

void Chip::init()
{
    // --- the display PLL's static half ------------------------------------
    //
    // The parts that do not change with the clock chosen; the rest is
    // Tv5725::DisplayClock's. PLL_ADS = 1 takes the input clock from the crystal
    // rather than the digital video input port, which this board does not drive.
    // Nothing on the scaling path clears PLL_VCORST -- setResetParameters() and
    // runSyncWatcher() both assert it and the preset table was the only thing
    // that put it back, so held it means no output clock, no picture, and every
    // register reading correct.
    GBS::PLL_DIVBY2Z::write(0x0);                     // s0_40[1:1]
    GBS::PLL_IS::write(0x1);                          // s0_40[2:2]
    GBS::PLL_ADS::write(0x1);                         // s0_40[3:3]
    GBS::PLL_VS::write(0x1);                          // s0_41[1:0]
    GBS::PLL_VS2::write(0x1);                         // s0_41[3:2]
    GBS::PLL_LEN::write(0x1);                         // s0_43[4:4]
    GBS::PLL_VCORST::write(0x0);                      // s0_43[5:5]

    // --- the video DACs ---------------------------------------------------
    //
    // Per channel: *PD is power-down (0 = powered), *0ENZ selects whether the
    // DAC follows its input data, *1EN the second input. The MS9288A samples
    // this analog output, so all three channels are live -- and a cleared B0ENZ
    // is the yellow-tint fault, which only the bypass switches ever wrote.
    GBS::DAC_RGBS_RPD::write(0x0);                    // s0_44[1:1]
    GBS::DAC_RGBS_R0ENZ::write(0x1);                  // s0_44[2:2]
    GBS::DAC_RGBS_R1EN::write(0x0);                   // s0_44[3:3]
    GBS::DAC_RGBS_GPD::write(0x0);                    // s0_44[4:4]
    GBS::DAC_RGBS_G0ENZ::write(0x1);                  // s0_44[5:5]
    GBS::DAC_RGBS_G1EN::write(0x0);                   // s0_44[6:6]
    GBS::DAC_RGBS_BPD::write(0x0);                    // s0_44[7:7]
    GBS::DAC_RGBS_B0ENZ::write(0x1);                  // s0_45[0:0]
    GBS::DAC_RGBS_B1EN::write(0x0);                   // s0_45[1:1]
    GBS::CKT_FF_CNTRL::write(0x0);                    // s0_45[7:6]

    // --- block resets, RELEASED, and before anything they hold ------------
    //
    // 1 releases. This group is why Chip::init() runs first, and
    // test_bringup.cpp fails if a later class's write overtakes it.
    // SFTRST_HDBYPS_RSTZ stays held: scaling does not go through that block.
    GBS::SFTRST_DEINT_RSTZ::write(0x1);               // s0_46[1:1]
    GBS::SFTRST_MEM_FF_RSTZ::write(0x1);              // s0_46[2:2]
    GBS::SFTRST_MEM_RSTZ::write(0x1);                 // s0_46[3:3]
    GBS::SFTRST_FIFO_RSTZ::write(0x1);                // s0_46[4:4]
    GBS::SFTRST_OSD_RSTZ::write(0x1);                 // s0_46[5:5]
    GBS::SFTRST_HDBYPS_RSTZ::write(0x0);              // s0_47[3:3]
    GBS::SFTRST_INT_RSTZ::write(0x1);                 // s0_47[4:4]

    // --- the analog pads --------------------------------------------------
    //
    // ENZ is active low: 1 disables. The RGB inputs are on and the digital RGB
    // output drivers off, because nothing loads them. PAD_CKOUT_ENZ = 1 is why
    // DS-5725-3.2 Table 15's 108 MHz CLKOUT rating is a pad spec for a pin
    // nobody loads rather than a ceiling on the display clock -- DisplayClock.h.
    GBS::PAD_BIN_ENZ::write(0x1);                     // s0_48[1:1]
    GBS::PAD_ROUT_EN::write(0x0);                     // s0_48[2:2]
    GBS::PAD_RIN_ENZ::write(0x1);                     // s0_48[3:3]
    GBS::PAD_GOUT_EN::write(0x0);                     // s0_48[4:4]
    GBS::PAD_GIN_ENZ::write(0x1);                     // s0_48[5:5]
    GBS::PAD_SYNC1_IN_ENZ::write(0x1);                // s0_48[6:6]
    GBS::PAD_SYNC2_IN_ENZ::write(0x1);                // s0_48[7:7]
    GBS::PAD_CKOUT_ENZ::write(0x1);                   // s0_49[1:1]
    GBS::PAD_BLK_OUT_ENZ::write(0x1);                 // s0_49[3:3]
    GBS::PAD_TRI_ENZ::write(0x0);                     // s0_49[4:4]
    GBS::PAD_PLDN_ENZ::write(0x0);                    // s0_49[5:5]
    GBS::PAD_PLUP_ENZ::write(0x0);                    // s0_49[6:6]
    GBS::PAD_OSC_CNTRL::write(0x0);                   // s0_4a[2:0]
    GBS::PAD_XTOUT_TTL::write(0x0);                   // s0_4a[3:3]

    // --- what reaches the DACs, and what reaches the digital port ---------
    //
    // All three routes off: the DACs take the scaled video, not the input
    // register and not the ADC. DAC_RGBS_ADC2DAC = 1 is bypassModeSwitch_RGBHV()
    // putting the ADC straight on the DACs, and only the table cleared it again.
    // DIGOUT_* drive the digital port the pads above have already turned off.
    GBS::DAC_RGBS_BYPS_IREG::write(0x0);              // s0_4b[0:0]
    GBS::DAC_RGBS_BYPS2DAC::write(0x0);               // s0_4b[1:1]
    GBS::DAC_RGBS_ADC2DAC::write(0x0);                // s0_4b[2:2]
    GBS::DIGOUT_BYPS2PAD::write(0x0);                 // s0_4e[0:0]
    GBS::DIGOUT_ADC2PAD::write(0x0);                  // s0_4e[1:1]

    // --- output clock and blanking selects --------------------------------
    //
    // OUT_SYNC_SEL 0 selects vds_proc, the only sync source scaling has. Every
    // other writer of it is a bypass path, so without this the H/V sync outputs
    // stayed wherever the last bypass excursion left them -- registers all
    // correct, nothing arriving at the encoder. The bypass branch runs later in
    // the same function and still wins.
    GBS::DAC_RGBS_V4CLK_INVT::write(0x0);             // s0_4f[0:0]
    GBS::OUT_CLK_PHASE_CNTRL::write(0x0);             // s0_4f[1:1]
    GBS::OUT_CLK_EN::write(0x3);                      // s0_4f[3:2]
    GBS::CLKOUT_EN::write(0x1);                       // s0_4f[4:4]
    GBS::OUT_SYNC_SEL::write(0x0);                    // s0_4f[7:6]
    GBS::OUT_BLANK_SEL_0::write(0x0);                 // s0_50[0:0]
    GBS::OUT_BLANK_SEL_1::write(0x0);                 // s0_50[1:1]
    GBS::IN_BLANK_SEL::write(0x0);                    // s0_50[4:4]
    GBS::IN_BLANK_IREG_BYPS::write(0x0);              // s0_50[5:5]

    // --- odds and ends ----------------------------------------------------
    //
    // No derivation for these beyond "all twelve tables agree", marked in place
    // rather than moved somewhere that hides it.
    GBS::INVT_RING_EN::write(0x0);                    // s0_57[7:7]

    // The OSD's command handshake at rest: every writer toggles it 0 and back
    // around a command, and nothing establishes it until the OSD is first drawn.
    GBS::OSD_COMMAND_FINISH::write(0x1);              // s0_93[7:7]
    GBS::OSD_INT_NG_LAT::write(0x0);                  // s0_94[3:3]
    GBS::OSD_TEST_SEL::write(0x0);                    // s0_94[7:4]
}

}  // namespace Tv5725
