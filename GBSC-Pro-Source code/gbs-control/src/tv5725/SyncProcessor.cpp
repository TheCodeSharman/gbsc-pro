#include "SyncProcessor.h"

#include <Arduino.h>   // delayMicroseconds(), a hardware settling time

#include "Adc.h"
#include "Chip.h"

#include "../../gbs_types.h"

namespace Tv5725 {

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

void SyncProcessor::applyDefaultCoastWindow()
{
    SP_H_CST_ST::write(0x10);
    SP_H_CST_SP::write(0x100);
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

namespace {

// The line length HPERIOD_IF reports, in 27 MHz counts, or 0 when the readings
// will not agree. The register holds a quarter of the count less one, so the
// mean is scaled back up rather than the samples.
uint32_t measuredLineLength(bool (*stable)())
{
    uint32_t accumulated = 0;
    uint16_t previous = GBS::HPERIOD_IF::read();
    for (uint8_t i = 0; i < SyncProcessor::CoastSamples; ++i) {
        const uint16_t sample = GBS::HPERIOD_IF::read();
        if (sample <= previous - SyncProcessor::CoastAgreement
            || sample >= previous + SyncProcessor::CoastAgreement)
            return 0;
        if (stable != nullptr && !stable())
            return 0;
        accumulated += sample;
        previous = sample;
    }
    return (accumulated * 4) / SyncProcessor::CoastSamples;
}

// A line long enough to be a line. Above the first the reading is the NTSC one
// whatever it says; below the second there is no window to place.
const uint32_t LineLengthCeiling = 2040;
const uint32_t NtscLineLength = 1716;
const uint32_t LineLengthFloor = 32;

// A line this short with a plausible vertical count is the horizontal
// measurement having collapsed rather than a short line, and a long window is a
// better guess than a window at a fraction of nothing.
const uint32_t CollapsedLineLength = 240;
const uint16_t CollapsedStandsIn = 2000;
const uint16_t PlausibleVerticalCount = 322;

}  // namespace

namespace {

// The line the clamp is placed on, or 0 when the readings will not agree.
// Unlike the coast window's this is a mean of the samples as read, because the
// fractions below were fitted to each path's own units.
uint32_t clampLineLength(bool csync, bool (*stable)())
{
    uint32_t accumulated = 0;
    uint16_t previous = csync ? GBS::HPERIOD_IF::read()
                              : GBS::STATUS_SYNC_PROC_HTOTAL::read();
    for (uint8_t i = 0; i < SyncProcessor::ClampSamples; ++i) {
        const uint16_t sample = csync ? GBS::HPERIOD_IF::read()
                                      : GBS::STATUS_SYNC_PROC_HTOTAL::read();
        if (sample <= previous - SyncProcessor::CoastAgreement
            || sample >= previous + SyncProcessor::CoastAgreement)
            return 0;
        if (stable != nullptr && !stable())
            return 0;
        accumulated += sample;
        previous = sample;
        delayMicroseconds(100);
    }
    return accumulated / SyncProcessor::ClampSamples;
}

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
const float ClampStopCsync = 0.174f;
const float ClampStopSeparate = 0.058f;

// Above this the reading is not a line length the twelve-bit window can hold.
const uint32_t ClampLineCeiling = 4095;

}  // namespace

bool SyncProcessor::acquireClampWindow(bool csync, bool component,
                                       uint16_t offset, bool (*stable)())
{
    const uint32_t lineLength = clampLineLength(csync, stable);
    if (lineLength == 0 || lineLength > ClampLineCeiling)
        return false;

    float startFraction;
    if (component)
        startFraction = csync ? ClampStartCsyncComponent
                              : ClampStartSeparateComponent;
    else
        startFraction = csync ? ClampStartCsync : ClampStartSeparate;

    const uint16_t start =
        (uint16_t)(1 + lineLength * startFraction) + offset;
    const uint16_t stop =
        (uint16_t)(2 + lineLength * (csync ? ClampStopCsync : ClampStopSeparate))
        + offset;

    clampPlaced_ = true;

    if (withinOneOf(start, SP_CS_CLP_ST::read())
        && withinOneOf(stop, SP_CS_CLP_SP::read()))
        return true;

    SP_CS_CLP_ST::write(start);
    SP_CS_CLP_SP::write(stop);
    return true;
}

bool SyncProcessor::acquireCoastWindow(bool autoCoast, bool (*stable)())
{
    uint32_t lineLength = measuredLineLength(stable);
    if (lineLength == 0)
        return false;

    if (lineLength >= LineLengthCeiling)
        lineLength = NtscLineLength;

    if (lineLength <= CollapsedLineLength
        && GBS::STATUS_SYNC_PROC_VTOTAL::read() <= PlausibleVerticalCount) {
        delay(4);
        if (GBS::STATUS_SYNC_PROC_VTOTAL::read() <= PlausibleVerticalCount)
            lineLength = CollapsedStandsIn;
    }

    if (lineLength <= LineLengthFloor)
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
    SP_TEST_EN::write(0x1);                      // s5_63[0:0]
    SP_TEST_MODULE::write(0x7);                  // s5_63[3:1]
    SP_TEST_SIGNAL_SEL::write(0x0);              // s5_63[6:4]
}

void SyncProcessor::writeRetimeStop(uint16_t samples)
{
    SP_RT_HS_SP::write(samples);
}

}  // namespace Tv5725
