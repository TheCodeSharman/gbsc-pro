#include "SyncProcessor.h"

#include <Arduino.h>   // delayMicroseconds(), a hardware settling time

#include "Adc.h"
#include "Chip.h"

#include "../../gbs_types.h"

namespace Tv5725 {

const uint16_t SyncProcessor::RetimeStopPercent;

uint16_t SyncProcessor::retimeStopFor(uint16_t divider)
{
    return (uint16_t)(((uint32_t)divider * RetimeStopPercent) / 100);
}

void SyncProcessor::driveTestBus(uint8_t module, uint8_t signal)
{
    Tv5725::Tie<SP_TEST_EN, SP_TEST_MODULE, SP_TEST_SIGNAL_SEL>::write(1, module,
                                                                       signal);
}

namespace {

// How many lines either side of the vertical interval a serrated source is
// coasted over, and the pulse-ignore width at or above which a real sync pulse
// can be hiding behind it.
// How long the soft reset is held. 10 us is 270 cycles of the 27 MHz
// reference, which is what the block sees the reset for.
const unsigned int ResetHoldUs = 10;

const uint8_t SerratedCoastLines = 9;
const uint16_t PulseWidthDifference = 0xC0;
bool coastPlaced_ = false;
bool clampPlaced_ = false;
const uint8_t OwnVsyncPulseIgnore = 0xff;
const uint8_t SerratedPulseIgnore = 0x6b;
const uint8_t UnserratedPulseIgnore = 0x02;
const uint8_t WidestUsefulPulseIgnore = 0x33;

// What the search asks for while nothing is counting: the least the field can
// hide, so every pulse reaches the separator. The H timer beside it is measured
// as a don't-care across its whole range and is here to be written from one
// place rather than because a value was chosen.
const uint8_t SearchPulseIgnore = 0x02;
const uint8_t SearchHTimerValue = 0x3a;

}  // namespace

void SyncProcessor::writeSdVsyncStart(uint16_t start)
{
    SP_SDCS_VSST_REG_H::write(start >> 8);
    SP_SDCS_VSST_REG_L::write(start & 0xff);
}

void SyncProcessor::writeSdVsyncStop(uint16_t stop)
{
    SP_SDCS_VSSP_REG_H::write(stop >> 8);
    SP_SDCS_VSSP_REG_L::write(stop & 0xff);
}

void SyncProcessor::applySdVsyncPosition()
{
    writeSdVsyncStart(SdVsyncStart);
    writeSdVsyncStop(SdVsyncStop);
}

void SyncProcessor::holdClamp()
{
    SP_NO_CLAMP_REG::write(1);
}

void SyncProcessor::releaseClamp()
{
    SP_NO_CLAMP_REG::write(0);
}

bool SyncProcessor::clampHeld()
{
    return SP_NO_CLAMP_REG::read() == 1;
}

void SyncProcessor::applyDefaultClampWindow()
{
    SP_CS_CLP_ST::write(32);
    SP_CS_CLP_SP::write(48);
}

void SyncProcessor::reset()
{
    Chip::SFTRST_SYNC_RSTZ::write(0);
    delayMicroseconds(ResetHoldUs);
    Chip::SFTRST_SYNC_RSTZ::write(1);
}

bool SyncProcessor::coastPlaced() { return coastPlaced_; }

bool SyncProcessor::clampPlaced() { return clampPlaced_; }

void SyncProcessor::forgetPositions()
{
    coastPlaced_ = false;
    clampPlaced_ = false;
}

void SyncProcessor::adoptClampPlacement()
{
    clampPlaced_ = true;
}

void SyncProcessor::applyPulseIgnore(bool csync, bool serrated)
{
    if (!csync)
        SP_H_PULSE_IGNOR::write(OwnVsyncPulseIgnore);
    else
        SP_H_PULSE_IGNOR::write(serrated ? SerratedPulseIgnore
                                         : UnserratedPulseIgnore);
}

void SyncProcessor::applyPulseWidthDifference()
{
    SP_DLT_REG::write(PulseWidthDifference);
}

void SyncProcessor::widenCoast()
{
    SP_PRE_COAST::write(SerratedCoastLines);
    SP_POST_COAST::write(SerratedCoastLines);
}

void SyncProcessor::widenCoastForSerration()
{
    widenCoast();

    const uint8_t ignore = (uint8_t)SP_H_PULSE_IGNOR::read();
    if (ignore >= WidestUsefulPulseIgnore)
        SP_H_PULSE_IGNOR::write(ignore / 2);
}

void SyncProcessor::applyForSearch(bool csync)
{
    applyPulseWidthDifference();
    SP_H_PULSE_IGNOR::write(SearchPulseIgnore);
    applyDefaultCoastWindow();
    SP_H_COAST::write(0);
    SP_H_TIMER_VAL::write(SearchHTimerValue);
    if (csync)
        setCoastInvert(true);
    forgetPositions();
}

void SyncProcessor::applyDynamic(const Dynamic &source)
{
    if (source.searching) {
        if (source.hunting)
            applyForSearch(source.csync);
        else
            applyPulseWidthDifference();
        return;
    }

    if (source.csync)
        setCoastInvert(false);

    if (source.pathSource) {
        applySeparationThresholds(source.csync);
    } else if (source.present) {
        applyPulseWidthDifference();
        applyPulseIgnore(source.csync, source.serrated);
    }
}

void SyncProcessor::applyDefaultCoastWindow()
{
    SP_H_CST_ST::write(0x10);
    SP_H_CST_SP::write(0x100);
}

void SyncProcessor::prepare(bool csync, bool serrated, bool rgbhvRoute)
{
    SP_SOG_P_ATO::write(0);
    SP_JITTER_SYNC::write(0);

    applyPulseWidthDifference();
    applyPulseIgnore(csync, serrated);

    SP_H_TOTAL_EQ_THD::write(3);

    applySdVsyncPosition();

    SP_CS_HS_ST::write(0x10);
    SP_CS_HS_SP::write(0x00);

    if (!rgbhvRoute) {
        SP_CLAMP_MANUAL::write(0);
        clampFromReferenceClock();
        holdClamp();
        applyDefaultCoastWindow();
        setHsyncOverflowProtect(true);
        SP_HCST_AUTO_EN::write(0);
    }

    setSubCoast(serrated);

    SP_HS_REG::write(1);
    SP_HS_PROC_INV_REG::write(0);
    SP_VS_PROC_INV_REG::write(0);
}

void SyncProcessor::applyForPassThrough()
{
    setCoastInvert(false);
    setSubCoast(false);
    SP_SOG_P_ATO::write(1);

    SP_HS_PROC_INV_REG::write(0);
    SP_VS_PROC_INV_REG::write(0);
    SP_CS_P_SWAP::write(0);
    SP_HS2PLL_INV_REG::write(0);
}

void SyncProcessor::applyForSyncType(bool csync)
{
    // No ordering constraint between these fields is established, so the two
    // branches keep their own write order.
    if (csync) {
        SP_SOG_SRC_SEL::write(0);
        SP_EXT_SYNC_SEL::write(1);
        Adc::ADC_SOGEN::write(1);
        SP_SOG_MODE::write(1);
        SP_NO_COAST_REG::write(0);
        SP_PRE_COAST::write(SerratedPreCoastLines);
        SP_POST_COAST::write(SerratedPostCoastLines);
        SP_SYNC_BYPS::write(0);
        SP_HS_LOOP_SEL::write(1);
        SP_H_PROTECT::write(1);
    } else {
        SP_SOG_SRC_SEL::write(0);
        Adc::ADC_SOGEN::write(1);
        SP_EXT_SYNC_SEL::write(0);
        SP_SOG_MODE::write(0);
        SP_NO_COAST_REG::write(1);
        SP_PRE_COAST::write(0);
        SP_POST_COAST::write(0);
        applyPulseIgnore(csync, false);
        SP_SYNC_BYPS::write(0);
        SP_HS_POL_ATO::write(1);
        SP_VS_POL_ATO::write(1);
        SP_HS_LOOP_SEL::write(1);
        SP_H_PROTECT::write(0);
    }
}

bool SyncProcessor::hsyncActive()
{
    return GBS::STATUS_SYNC_PROC_HSACT::read() == 1;
}

bool SyncProcessor::vsyncActive()
{
    return GBS::STATUS_SYNC_PROC_VSACT::read() == 1;
}

uint16_t SyncProcessor::lineCount()
{
    return GBS::STATUS_SYNC_PROC_VTOTAL::read();
}

uint16_t SyncProcessor::lineSamples()
{
    return GBS::STATUS_SYNC_PROC_HTOTAL::read();
}

uint16_t SyncProcessor::hsyncLowSamples()
{
    return GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
}

bool SyncProcessor::hsyncPositive()
{
    return GBS::STATUS_SYNC_PROC_HSPOL::read() != 0;
}

bool SyncProcessor::hsyncFound()
{
    return GBS::STATUS_SYNC_PROC_HSACT::read() == 1;
}

void SyncProcessor::normaliseHsyncPolarity(bool found, bool positive)
{
    if (!found)
        return;
    SP_HS_INV_REG::write(positive ? 1 : 0);
    SP_HS2PLL_INV_REG::write(0);
}

namespace {

// A line long enough to be a line, and short enough to hold in the window's
// twelve bits.
const uint32_t LineLengthCeiling = 4095;
const uint32_t LineLengthFloor = 32;

}  // namespace

namespace {


bool withinOneOf(uint16_t wanted, uint16_t inForce)
{
    return wanted >= inForce - 1 && wanted <= inForce + 1;
}

// The window as a fraction of the line, per path. The starts differ because the
// sync tip they clear differs; the stop is the same fraction whatever is being
// clamped.
const float ClampStartCsync = 0.032f;
const float ClampStartSeparate = 0.010f;
const float ClampStartCsyncComponent = 0.089f;
const float ClampStartSeparateComponent = 0.032f;
// How long the clamp holds, in ADC samples. A settling time and not a fraction
// of the line: written as a fraction it widens with the divider and reaches
// into active video, where the black level comes off picture. Measured on the
// bench, RiscPC 640x480@60 at divider 1566 over a back porch of about 120
// samples -- 16..92 leaves the picture discoloured, 16..20 clears it.
const uint16_t ClampWidthSamples = 4;

// Above this the reading is not a line length the twelve-bit window can hold.
const uint32_t ClampLineCeiling = 4095;

}  // namespace

// THE SOURCE'S LINE, FROM THE RATE THE ENGINE SOLVED, never from HPERIOD_IF.
// The register reports the same quantity and rails; this cannot, and the two
// agree exactly where it is healthy -- measured on the bench, 15625 Hz gives
// 1728 against a reading of 431 whose (431+1) x 4 is also 1728.
//
// Two scalings because the blocks count differently: the coast window is a
// fraction of the whole line in 27 MHz counts, and the clamp's composite path
// was fitted to the quarter-count the register itself holds.
uint32_t SyncProcessor::coastLineFor(uint32_t lineRateHz)
{
    return lineRateHz == 0 ? 0 : 27000000u / lineRateHz;
}

// A separate-sync source is counted in ADC samples, so its line IS the divider
// the solve applied. STATUS_SYNC_PROC_HTOTAL only echoes that back.
uint32_t SyncProcessor::clampLineFor(bool csync, uint32_t lineRateHz, uint16_t divider)
{
    if (!csync)
        return divider;
    return lineRateHz == 0 ? 0 : 27000000u / (4u * lineRateHz) - 1u;
}

uint16_t SyncProcessor::clampStartFor(uint32_t lineLength, bool csync, bool component)
{
    float fraction;
    if (component)
        fraction = csync ? ClampStartCsyncComponent : ClampStartSeparateComponent;
    else
        fraction = csync ? ClampStartCsync : ClampStartSeparate;
    return (uint16_t)(1 + lineLength * fraction);
}

uint16_t SyncProcessor::clampStopFor(uint32_t lineLength, bool csync, bool component)
{
    return (uint16_t)(clampStartFor(lineLength, csync, component) + ClampWidthSamples);
}

void SyncProcessor::placeClampFor(uint32_t lineLength, bool csync, bool component)
{
    if (lineLength == 0 || lineLength > ClampLineCeiling)
        return;

    SP_CS_CLP_ST::write(clampStartFor(lineLength, csync, component));
    SP_CS_CLP_SP::write(clampStopFor(lineLength, csync, component));
}

bool SyncProcessor::acquireClampWindow(bool csync, bool component,
                                       uint32_t lineLength, uint16_t offset)
{
    if (lineLength == 0 || lineLength > ClampLineCeiling)
        return false;

    if (!hsyncActive())
        return false;

    const uint16_t start = clampStartFor(lineLength, csync, component) + offset;
    const uint16_t stop = clampStopFor(lineLength, csync, component) + offset;

    clampPlaced_ = true;

    if (withinOneOf(start, SP_CS_CLP_ST::read())
        && withinOneOf(stop, SP_CS_CLP_SP::read()))
        return true;

    SP_CS_CLP_ST::write(start);
    SP_CS_CLP_SP::write(stop);
    return true;
}

bool SyncProcessor::acquireCoastWindow(bool autoCoast, uint32_t lineRateHz)
{
    const uint32_t lineLength = coastLineFor(lineRateHz);
    if (lineLength <= LineLengthFloor || lineLength >= LineLengthCeiling)
        return false;

    if (!hsyncActive())
        return false;

    if (autoCoast) {
        SP_H_CST_ST::write((uint16_t)(lineLength * 0.0562f));
        SP_H_CST_SP::write((uint16_t)(lineLength * 0.1550f));
        SP_HCST_AUTO_EN::write(1);
    } else {
        SP_H_CST_ST::write(0x10);
        SP_H_CST_SP::write((uint16_t)(lineLength * 0.968f));
        SP_HCST_AUTO_EN::write(0);
    }
    coastPlaced_ = true;
    return true;
}

void SyncProcessor::applyForScalingRgbhv(bool csync)
{
    SP_SOG_P_ATO::write(1);
    writeSdVsyncStart(ScalingRgbhvVsyncStart);
    writeSdVsyncStop(ScalingRgbhvVsyncStop);
    forgetPositions();

    if (csync) {
        SP_SOG_MODE::write(1);
        SP_H_CST_ST::write(0x10);
        SP_H_CST_SP::write(ScalingRgbhvCoastStop);
        setHsyncOverflowProtect(true);
    } else {
        SP_SOG_MODE::write(0);
        SP_CLAMP_MANUAL::write(1);
        SP_NO_COAST_REG::write(1);
    }
}

void SyncProcessor::applySeparationThresholds(bool csync)
{
    if (csync) {
        SP_PRE_COAST::write(SerratedPreCoastLines);
        SP_POST_COAST::write(SerratedPostCoastLines);
        applyPulseWidthDifference();
        applyPulseIgnore(true, false);
    } else {
        SP_PRE_COAST::write(0x00);
        SP_POST_COAST::write(0x00);
        applyPulseIgnore(false, false);
        SP_DLT_REG::write(0x00);
    }
}

void SyncProcessor::clampFromReferenceClock()
{
    SP_CLP_SRC_SEL::write(0);
}

void SyncProcessor::setHsyncOverflowProtect(bool wanted)
{
    SP_H_PROTECT::write(wanted ? 1 : 0);
}

void SyncProcessor::toggleHsyncOverflowProtect()
{
    SP_H_PROTECT::write(SP_H_PROTECT::read() ? 0 : 1);
}

void SyncProcessor::setCoastInvert(bool wanted)
{
    SP_COAST_INV_REG::write(wanted ? 1 : 0);
}

void SyncProcessor::setSubCoast(bool wanted)
{
    SP_DIS_SUB_COAST::write(wanted ? 0 : 1);
}

void SyncProcessor::clampManually(bool manual)
{
    SP_CLAMP_MANUAL::write(manual ? 1 : 0);
}

void SyncProcessor::selectExternalSync(uint8_t sel)
{
    SP_EXT_SYNC_SEL::write(sel);
}

void SyncProcessor::init()
{
    SP_SOG_P_INV::write(0x0);                    // s5_20[2:2]
    // The retime window's START. Its partner SP_RT_HS_SP is 93% of PLLAD_MD and
    // belongs to SourceMeasurement, which holds all three of that quantity's
    // registers off one divider.
    SP_RT_HS_ST::write(0x0);                     // s5_4a[11:0]
    SP_SYNC_TGL_THD::write(0x18);                // s5_21[7:0]
    SP_L_DLT_REG::write(0xF);                    // s5_22[7:0]
    SP_T_DLT_REG::write(0x40);                   // s5_24[11:0]
    SP_SYNC_PD_THD::write(0x4);                  // s5_26[11:0]
    SP_PRD_EQ_THD::write(0xF);                   // s5_2a[7:0]
    SP_VSYNC_TGL_THD::write(0x3);                // s5_2d[7:0]
    SP_SYNC_WIDTH_DTHD::write(0x0);              // s5_2e[7:0]
    SP_V_PRD_EQ_THD::write(0x2);                 // s5_2f[7:0]
    SP_VT_DLT_REG::write(0x2F);                  // s5_31[7:0]
    SP_VSIN_INV_REG::write(0x0);                 // s5_32[0:0]
    SYNC_PROC_5_23::write(0x0);                  // s5_23, no documented field
    SP_H_TIMER_VAL::write(0x3A);                 // s5_33[7:0]
    SP_V_TIMER_VAL::write(0x6);                  // s5_34[7:0]
    SP_CS_P_SWAP::write(0x0);                    // s5_3e[0:0]
    SP_HD_MODE::write(0x0);                      // s5_3e[1:1]
    SP_CS_INV_REG::write(0x0);                   // s5_3e[3:3]
    SP_RT_VS_ST::write(0x2);                     // s5_51[11:0]
    SP_RT_VS_SP::write(0x0);                     // s5_53[11:0]
    SP_HS_EP_DLY_SEL::write(0x0);                // s5_55[2:0]
    SP_HS_INV_REG::write(0x0);                   // s5_55[3:3]
    SP_VS_INV_REG::write(0x0);                   // s5_55[5:5]

    // The retiming module's auto-polarity, which only the separate-sync path
    // ever set -- so a csync source had nobody writing it, and the module went
    // on correcting polarity for the previous mode. 0 is the resting state, and
    // the separate-sync branch still wins because this runs first.
    SP_HS_POL_ATO::write(0x0);                   // s5_55[4:4]
    SP_VS_POL_ATO::write(0x0);                   // s5_55[6:6]
    SP_CLAMP_INV_REG::write(0x0);                // s5_56[7:7]
    SP_COAST_VALUE_REG::write(0x0);              // s5_57[3:3]
    SP_HT_DIFF_REG::write(0x5);                  // s5_58[11:0]
    SP_VT_DIFF_REG::write(0x1);                  // s5_5a[10:0]
    SP_STBLE_CNT_REG::write(0x3);                // s5_5c[7:0]
    SYNC_PROC_5_5D::write(0x2);                  // s5_5d, absent from RD-5725-1.1
    SP_TEST_EN::write(0x1);                      // s5_63[0:0]
    SP_TEST_MODULE::write(0x7);                  // s5_63[3:1]
    SP_TEST_SIGNAL_SEL::write(0x0);              // s5_63[6:4]
}

void SyncProcessor::writeRetimeStop(uint16_t samples)
{
    SP_RT_HS_SP::write(samples);
}

void SyncProcessor::disableOutput() { Chip::PAD_SYNC_OUT_ENZ::write(1); }

void SyncProcessor::enableOutput() { Chip::PAD_SYNC_OUT_ENZ::write(0); }

}  // namespace Tv5725
