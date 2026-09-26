#include "InputFormatter.h"

#include "../../gbs_types.h"

#include <stdio.h>

namespace Tv5725 {

InputFormatter::InputFormatter() : lineUnits_(0), doubled_(false) {}

const uint16_t InputFormatter::LineCounterMax;
const uint16_t InputFormatter::DoubleBelowLines;
const uint16_t InputFormatter::DoubledTailBlanking;

uint16_t InputFormatter::lineCounterFor(uint16_t divider, bool lineDoubled)
{
    return lineDoubled ? (uint16_t)(divider / 2) : divider;
}

VideoSourceLine InputFormatter::capturableLine(const HsyncPulse &pulse) const
{
    return VideoSourceLine::forDuty(lineUnits_, pulse, doubled_);
}

VideoSourceLine InputFormatter::capturableFrame(uint16_t sourceLines) const
{
    return VideoSourceLine::frame(doubled_ ? (uint16_t)(2 * (sourceLines + 1))
                                           : (uint16_t)(sourceLines + 1));
}

uint16_t InputFormatter::verticalPeriod()
{
    if (!GBS::STATUS_IF_VT_OK::read())
        return 0;
    return GBS::VPERIOD_IF::read();
}

uint16_t InputFormatter::linePeriod()
{
    return GBS::HPERIOD_IF::read();
}


bool InputFormatter::shouldDoubleLine(uint16_t sourceLines,
                                      uint16_t showableUnits)
{
    if (sourceLines == 0)
        return true;
    if (sourceLines >= DoubleBelowLines)
        return false;
    if (showableUnits == 0)
        return true;

    // The IF counts half-lines with the doubler in, so the doubled frame asks
    // for twice the source's own count.
    return 2u * ((uint32_t)sourceLines + 1u) <= showableUnits;
}

void InputFormatter::init()
{
    IF_IN_DREG_BYPS::write(0x0);                 // s1_00[0:0]
    IF_UV_REVERT::write(0x0);                    // s1_00[2:2]
    IF_SEL_656::write(0x0);                      // s1_00[3:3]

    // RD-5725-1.1: "choose the periodical or virtual vertical timing". Every
    // source wants the virtual one -- vertical regenerated from what arrives
    // rather than assumed to be periodic.
    IF_VS_SEL::write(0x0);                       // s1_00[5:5]
    IF_SEL16BIT::write(0x0);                     // s1_00[4:4]
    IF_HS_FLIP::write(0x0);                      // s1_00[7:7]
    IF_VS_FLIP::write(0x1);                      // s1_01[0:0]
    IF_UV_FLIP::write(0x0);                      // s1_01[1:1]
    IF_U_DELAY::write(0x0);                      // s1_01[2:2]
    IF_V_DELAY::write(0x0);                      // s1_01[3:3]
    IF_TAP6_BYPS::write(0x0);                    // s1_01[4:4]
    IF_Y_DELAY::write(0x3);                      // s1_01[6:5]
    IF_SEL24BIT::write(0x1);                     // s1_01[7:7]

    // doPostPresetLoadSteps() leaves these to `if (rto->inputIsYpBpR)` and to
    // the standard 3/4/8/9 branch, and 15 kHz RGB into a non-custom preset takes
    // neither, so the table was the only writer. 0 and 3 are what all twelve
    // ship, bar IF_HS_Y_PDELAY 2 in ntsc_1920x1080, which the YPbPr branch still
    // asks for afterwards.
    IF_HS_INT_LPF_BYPS::write(0x0);              // s1_02[0:0]
    // The path every load starts from. applyScan() owns it from here
    // on, writing the scan mode's own value on every solve.
    IF_HS_SEL_LPF::write(0x1);                   // s1_02[1:1]
    IF_HS_PSHIFT_BYPS::write(0x1);               // s1_02[3:3]
    IF_HS_TAP11_BYPS::write(0x0);                // s1_02[4:4]
    IF_HS_Y_PDELAY::write(0x3);                  // s1_02[6:5]
    IF_HS_UV_SIGN2UNSIGN::write(0x0);            // s1_02[7:7]
    IF_LD_WRST_SEL::write(0x1);                  // s1_28[1:1]
    IF_HS_RATE_SEG0::write(0x0);                 // s1_03[7:0]
    IF_HS_RATE_SEG1::write(0x0);                 // s1_04[7:0]
    IF_HS_RATE_SEG2::write(0x0);                 // s1_05[7:0]
    IF_HS_RATE_SEG3::write(0x0);                 // s1_06[7:0]
    IF_HS_RATE_SEG4::write(0x0);                 // s1_07[7:0]
    IF_HS_RATE_SEG5::write(0x0);                 // s1_08[7:0]
    IF_HS_RATE_SEG6::write(0x0);                 // s1_09[7:0]
    IF_HS_RATE_SEG7::write(0x0);                 // s1_0a[7:0]
    IF_HS_RATE_LOW::write(0x0);                  // s1_0b[3:0]

    IF_SEL_HSCALE::write(0x1);                   // s1_0b[6:6]

    // The line double's write reset start position (RD-5725-1.1, s1_0c[4:1]).
    // The datasheet says what the counter does and nothing about choosing the
    // value, and the tables offer 5 and 3 with no rule behind either -- not the
    // PAL/NTSC split it looks like, since the PAL six split 5/3/3/5/5/5. 5 is
    // the bench-proven one.
    IF_LD_ST::write(5);              // s1_0c[4:1]

    writeLineCounterStart(0);                    // s1_0c[15:5]

    // Horizontal blanking set 0. The IF module has three sets -- 0 at
    // s1_10/s1_12, 1 at s1_14/s1_16, 2 at s1_18/s1_1a -- with no selector
    // documented between them; VideoPath::write() owns set 2, the capture window
    // and the one that demonstrably moves the picture. Set 1 is deliberately
    // absent, measured inert and asserted absent by test_bringup.cpp
    // (docs/investigations/preset-abandonment-audit.md). Set 0 has not had that
    // experiment, so it is written at what the ten scaling tables ship.
    IF_HB_ST::write(2);                          // s1_10[10:0]
    IF_HB_SP::write(72);                         // s1_12[10:0]

    // applyScan() owns this from here on; what it needs before the first
    // scan mode is decided is a value that is not 0, which blanks the whole line.
    IF_HBIN_SP::write(LineDoubleReset);          // s1_26[11:0]

    // Its start. applyScan() owns this from here on too; what it needs
    // before the first scan mode is decided is a defined value, because the
    // part keeps its registers across an ESP reset.
    IF_HBIN_ST::write(0);                        // s1_24[11:0]

    IF_SEL_ADC_SYNC::write(0x1);                 // s1_28[2:2]

    // Off. toggleIfAutoOffset() turns it on and is the only other writer, so a
    // toggle now lasts until the next bring-up rather than until the next load.
    IF_AUTO_OFST_EN::write(0x0);                 // s1_29[0:0]
    IF_AUTO_OFST_PRD::write(0x0);                // s1_29[1:1]
    IF_AUTO_OFST_U_RANGE::write(0x0);            // s1_2a[3:0]
    IF_AUTO_OFST_V_RANGE::write(0x0);            // s1_2a[7:4]
}

uint16_t InputFormatter::lineUnits() const { return lineUnits_; }

bool InputFormatter::scanIsDoubled() const { return doubled_; }

// Inside every frame any source presents, so it cannot be the window that
// stops the block measuring.
void InputFormatter::writeReferenceVerticalBlank()
{
    IF_VB_ST::write(0);
    IF_VB_SP::write(2);
}

void InputFormatter::writeLineCounterStart(uint16_t pixels)
{
    IF_INI_ST::write(pixels);
}



// The scan is one setting and it goes in whole or not at all. Three registers
// route the line doubler, and the counter and the decimation size the line for
// it -- a count of IF units beside what an IF unit IS -- so a caller that could
// set them apart is a caller that will, and the source's field rate is timed off
// this block's vertical.
//
// The counter takes the low eleven bits of whatever it is handed, so a line that
// does not fit arrives as a different one and reads back as one. Nothing is
// written then, which keeps the five describing the line they already did.
// ../../../docs/investigations/the-field-rate-reads-exactly-double-after-a-sync-reset.md
bool InputFormatter::applyScan(uint16_t divider, bool lineDoubled, bool component)
{
    const uint16_t counter = lineCounterFor(divider, lineDoubled);
    if (counter > LineCounterMax) {
        char line[72];
        snprintf(line, sizeof(line),
                 "if line counter: %u does not fit, holding %u",
                 (unsigned)counter, (unsigned)(lineUnits_ ? lineUnits_ - 1 : 0));
        tv5725Log(line);
        return false;
    }

    const bool progressive = !lineDoubled;

    IF_LD_SEL_PROV::write(progressive ? 1 : 0);
    IF_PRGRSV_CNTRL::write(progressive ? 1 : 0);
    IF_LD_RAM_BYPS::write(progressive ? 1 : 0);
    IF_SEL_WEN::write(progressive ? 1 : 0);
    IF_HS_SEL_LPF::write(progressive ? 0 : 1);
    IF_HS_Y_PDELAY::write(!progressive && component ? 2 : 3);
    IF_HBIN_SP::write(progressive ? NoHeadBlanking : LineDoubleReset);
    IF_HBIN_ST::write(progressive ? 0 : DoubledTailBlanking);

    IF_HSYNC_RST::write(counter);
    IF_HS_DEC_FACTOR::write(lineDoubled ? 1 : 0);

    // The counter wraps one past its last value, so the span is the register
    // plus one.
    lineUnits_ = (uint16_t)(counter + 1);
    doubled_ = lineDoubled;
    return true;
}

}  // namespace Tv5725
