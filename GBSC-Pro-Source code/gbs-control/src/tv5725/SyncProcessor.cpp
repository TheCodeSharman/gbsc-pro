#include "SyncProcessor.h"

#include "TestBus.h"

#include <Arduino.h>   // delayMicroseconds(), a hardware settling time
#include <math.h>

#include "Adc.h"
#include "Chip.h"

#include "../../gbs_types.h"

namespace Tv5725 {

const uint16_t SyncProcessor::DefaultCoastStop;
const uint16_t SyncProcessor::RetimeOriginSamples;
const uint16_t SyncProcessor::UndecimatedOriginSamples;
const uint16_t SyncProcessor::InvertedPulseWidthSamples;
const float SyncProcessor::SourceVideoDelayPixels = 4.4f;
const float SyncProcessor::CalibratedPixelSamples = 1.12f;

uint16_t SyncProcessor::retimeStopFor(uint16_t divider, const HsyncPulse &pulse,
                                     uint8_t oversample, float pixelSamples)
{
    if (!pulse.isPulse() || divider == 0)
        return 0;

    const long lag = pixelSamples > 0.0f
        ? lrintf(SourceVideoDelayPixels * (pixelSamples - CalibratedPixelSamples))
        : 0L;
    const uint16_t origin = (uint16_t)(
        (long)RetimeOriginSamples + lag
        + (oversample <= 1 ? (long)UndecimatedOriginSamples : 0L));
    const uint32_t sync = (uint32_t)lrintf((float)divider * pulse.syncDuty());

    // A pulse narrower than the origin carries the subtraction past the end of
    // the line, where the register is inert. The stop is a PHASE, so the same
    // phase one line earlier is the modulo: a whole line of horizontal shift is
    // no horizontal shift at all.
    const uint32_t stop = (divider + origin - sync) % divider;

    // 0 is this function's "nothing measured" answer, so a phase landing there
    // takes the next sample instead -- one ADC sample, against an origin whose
    // own measurement spread is +/-1.5.
    return (uint16_t)(stop == 0 ? 1 : stop);
}

uint16_t SyncProcessor::retimeOriginFor(uint16_t divider, uint8_t oversample)
{
    if (divider == 0)
        return 0;

    const uint16_t origin =
        RetimeOriginSamples + (oversample <= 1 ? UndecimatedOriginSamples : 0);
    return (uint16_t)(origin % divider == 0 ? 1 : origin % divider);
}

void SyncProcessor::driveTestBus(uint8_t module, uint8_t signal)
{
    Tv5725::Tie<SP_TEST_EN, SP_TEST_MODULE, SP_TEST_SIGNAL_SEL>::write(1, module,
                                                                       signal);
}

namespace {

// How long the soft reset is held. 10 us is 270 cycles of the 27 MHz
// reference, which is what the block sees the reset for.
const unsigned int ResetHoldUs = 10;

const uint16_t PulseWidthDifference = 0xC0;
bool coastPlaced_ = false;
bool clampPlaced_ = false;
bool sogInverted_ = false;
bool leadingEdge_ = false;
bool coastOverridden_ = false;
uint8_t preCoastOverride_ = 0;
uint8_t postCoastOverride_ = 0;

// Held until measured otherwise, because the two errors are not equal: a source
// needing the hold and denied it emits nothing, where one given it that does not
// need it is displaced in under a percent of frames.
bool longPostCoast_ = true;

// 107 through a 2200-sample line is the value measured to read PAL 576i. Below
// 3.68% of the line the separator counts that source's serrations as lines and
// reads 319 against a true 310.
const uint16_t SerratedPulseIgnorePerThousand = 49;

// What a source with nothing in its vertical interval can take, which is less:
// 4.9% lands inside the dither on 640x512@50 composite, where the whole band
// 2.75..2.81% holds the count on every unserrated source measured -- the RISC
// PC at 320x256@50 and 640x512@50 at both of its dividers, and the Wii at
// 480p.
// ../../../../docs/investigations/the-composite-count-dithers-where-the-ignore-nears-the-pulse.md
const uint16_t UnserratedPulseIgnorePerThousand = 28;

// Under the narrowest sync pulse the block can be counting while the divider is
// not the line: 31 samples, a 7.3% pulse at half the 27 MHz reference.
const uint8_t SearchPulseIgnore = 16;

uint8_t pulseIgnoreFor(uint16_t divider, bool serrated)
{
    const uint16_t perThousand = serrated ? SerratedPulseIgnorePerThousand
                                          : UnserratedPulseIgnorePerThousand;
    return (uint8_t)((uint32_t)divider * perThousand / 1000u);
}

// THE SEARCH ASSUMES SERRATIONS, because what it is hunting has not been
// measured and the two errors are not equal: the higher threshold costs an
// unserrated source nothing it cannot recover once the probe has run, where
// the lower one leaves a serrated source counting its vertical interval.
uint8_t searchPulseIgnoreFor(bool latched, uint16_t divider)
{
    return latched ? pulseIgnoreFor(divider, true) : SearchPulseIgnore;
}

// The H timer the search asks for, measured as a don't-care across its whole
// range and here to be written from one place rather than because a value was
// chosen.
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

void SyncProcessor::releaseClampIfPlaced()
{
    if (clampPlaced() && clampHeld())
        releaseClamp();
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

void SyncProcessor::overrideCoast(uint8_t pre, uint8_t post)
{
    coastOverridden_ = true;
    preCoastOverride_ = pre;
    postCoastOverride_ = post;
}

void SyncProcessor::forgetCoastOverride() { coastOverridden_ = false; }

void SyncProcessor::useLongPostCoast(bool needed) { longPostCoast_ = needed; }

bool SyncProcessor::longPostCoastInUse() { return longPostCoast_; }

bool SyncProcessor::coastOverridden() { return coastOverridden_; }

uint8_t SyncProcessor::preCoastLines()
{
    return coastOverridden_ ? preCoastOverride_ : CompositePreCoastLines;
}

uint8_t SyncProcessor::postCoastLines()
{
    if (coastOverridden_)
        return postCoastOverride_;
    return longPostCoast_ ? LongPostCoastLines : ShortPostCoastLines;
}

void SyncProcessor::applyPulseIgnore(uint16_t divider, bool serrated)
{
    SP_H_PULSE_IGNOR::write(pulseIgnoreFor(divider, serrated));
}

void SyncProcessor::applyPulseWidthDifference()
{
    SP_DLT_REG::write(PulseWidthDifference);
}

void SyncProcessor::applyForSearch(const Dynamic &source)
{
    applyPulseWidthDifference();
    SP_H_PULSE_IGNOR::write(searchPulseIgnoreFor(source.latched, source.divider));
    applyDefaultCoastWindow();
    SP_H_COAST::write(0);
    SP_H_TIMER_VAL::write(SearchHTimerValue);
    forgetPositions();
}

void SyncProcessor::applyDynamic(const Dynamic &source)
{
    // AHEAD OF THE SEARCHING BRANCH, because the branch returns. The inversion
    // compensates for a coast window still at its default, which is the state a
    // source with no measured line length is in: without it the block counts
    // nothing at all, and with it standing after a count arrives the serrations
    // are counted as lines and the hsync handed to the ADC PLL will not lock.
    // ../../../../docs/investigations/the-search-coast-inversion-outlived-the-search.md
    if (source.csync)
        setCoastInvert(source.searching);

    if (source.searching) {
        applyForSearch(source);
        return;
    }

    if (source.pathSource)
        applySeparationThresholds(source.csync);
    else if (source.present)
        applyPulseWidthDifference();

    // THE THRESHOLD FOLLOWS THE LINE ON EVERY ROUTE, and waits for the source
    // to be present because the fraction is of the LATCHED line.
    // ../../../../docs/investigations/the-pulse-ignore-outlived-the-channels-entry.md
    if (source.present)
        applyPulseIgnore(source.divider, source.serrated);
}

void SyncProcessor::applyForOffsetCalibration()
{
    SP_CS_CLP_ST::write(0x00);
    SP_CS_CLP_SP::write(0x00);
    SP_SOG_MODE::write(1);
    SP_HS2PLL_INV_REG::write(0);
    SP_CLAMP_MANUAL::write(1);
    SP_CLP_SRC_SEL::write(0);
    SP_SYNC_BYPS::write(0);
    SP_HS_PROC_INV_REG::write(0);
    SP_VS_PROC_INV_REG::write(0);
    SP_CLAMP_INV_REG::write(0);
    SP_NO_CLAMP_REG::write(0);
    SP_COAST_INV_REG::write(0);
    SP_NO_COAST_REG::write(0);
    SP_COAST_VALUE_REG::write(0);
    SP_HS_LOOP_SEL::write(0);
    SP_HS_REG::write(1);
}

void SyncProcessor::applyDefaultCoastWindow()
{
    SP_H_CST_ST::write(0x10);
    SP_H_CST_SP::write(DefaultCoastStop);
}

void SyncProcessor::prepare(uint16_t divider)
{
    SP_SOG_P_ATO::write(0);
    SP_JITTER_SYNC::write(0);

    applyPulseWidthDifference();
    // SERRATED UNTIL MEASURED OTHERWISE. Nothing has probed the arriving
    // source yet, and the higher threshold is the recoverable error of the two.
    applyPulseIgnore(divider, true);

    SP_H_TOTAL_EQ_THD::write(3);

    applySdVsyncPosition();

    SP_CS_HS_ST::write(0x10);

    SP_HS_REG::write(1);
    SP_HS_PROC_INV_REG::write(0);
    SP_VS_PROC_INV_REG::write(0);
}

void SyncProcessor::prepareForDetection()
{
    SP_CLAMP_MANUAL::write(0);
    clampFromReferenceClock();
    holdClamp();
    applyDefaultCoastWindow();
    SP_HCST_AUTO_EN::write(0);
}

void SyncProcessor::applyForPassThrough(bool syncOnGreen)
{
    invertSogReference(syncOnGreen);
    // The chip's own polarity auto-correct stays OFF, as it is on the scaling
    // path. normaliseHsyncPolarity() already makes the polarity one shape, so
    // this is a second owner of it -- and it displaces where the sync processor
    // starts counting by a whole pulse.
    // ../../../docs/investigations/the-sog-polarity-auto-correct-displaced-pass-through.md
    SP_SOG_P_ATO::write(0);

    SP_HS_PROC_INV_REG::write(0);
    SP_VS_PROC_INV_REG::write(0);
    referenceLeadingEdge(false);
}

void SyncProcessor::applyForSyncType(bool csync, uint16_t divider)
{
    // THE SYNC TYPE IS WHAT DECIDES THE SUB-COAST, and the line rate has no job
    // in it. Composite sync carries equalisation and serration pulses in the
    // vertical interval whatever the line rate, and suppressing the horizontal
    // count through them is measured harmless at 37.6 kHz and required at 15.7;
    // a source with its own V sync has nothing to suppress, and enabling it
    // there lets the vertical edges into the horizontal count.
    // ../../../docs/investigations/serrated-sync-is-not-line-rate.md
    setSubCoast(csync);

    // No ordering constraint between these fields is established, so the two
    // branches keep their own write order.
    invertSogReference(false);
    referenceLeadingEdge(csync);

    // OUTSIDE THE BRANCH, because the separation threshold is a fraction of the
    // line rather than a property of the arrangement. Serrated here for the
    // same reason prepare() is: the arrangement runs ahead of the probe.
    applyPulseIgnore(divider, true);

    if (csync) {
        SP_SOG_SRC_SEL::write(0);
        SP_EXT_SYNC_SEL::write(1);
        Adc::ADC_SOGEN::write(1);
        SP_SOG_MODE::write(1);
        SP_NO_COAST_REG::write(0);
        SP_PRE_COAST::write(preCoastLines());
        SP_POST_COAST::write(postCoastLines());
        applyPulseWidthDifference();
        SP_SYNC_BYPS::write(0);
        SP_HS_LOOP_SEL::write(0);
        SP_H_PROTECT::write(1);
    } else {
        SP_SOG_SRC_SEL::write(0);
        Adc::ADC_SOGEN::write(1);
        SP_EXT_SYNC_SEL::write(0);
        SP_SOG_MODE::write(0);
        SP_NO_COAST_REG::write(1);
        SP_PRE_COAST::write(0);
        SP_POST_COAST::write(0);
        SP_SYNC_BYPS::write(0);
        SP_HS_POL_ATO::write(1);
        SP_VS_POL_ATO::write(1);
        SP_HS_LOOP_SEL::write(0);
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

uint16_t SyncProcessor::frameLines()
{
    const uint16_t counted = GBS::STATUS_SYNC_PROC_VTOTAL::read();
    return counted == 0 ? 0 : (uint16_t)(counted + 1);
}

uint16_t SyncProcessor::lineSamples()
{
    return GBS::STATUS_SYNC_PROC_HTOTAL::read();
}

uint16_t SyncProcessor::hsyncLowSamples()
{
    return GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
}

uint16_t SyncProcessor::hsyncSourceSamples(uint16_t lineSamples)
{
    const uint16_t low = hsyncLowSamples();
    if (lineSamples <= low)
        return low;
    const uint16_t complement = (uint16_t)(lineSamples - low);
    return low < complement ? low : complement;
}

// A low-active source read on its own hsync line is the one reading the term
// does not suit: a high-active one is counted through the inversion, and a
// separated one is the separator's output rather than the source's.
uint16_t SyncProcessor::originPulseCorrection(uint16_t pulse, bool positive,
                                              bool separated)
{
    const bool widened = positive || separated;
    if (!widened || pulse <= InvertedPulseWidthSamples)
        return 0;
    return InvertedPulseWidthSamples;
}

uint16_t SyncProcessor::hsyncPulseSamples(uint16_t lineSamples, bool positive,
                                          bool separated)
{
    const uint16_t pulse = hsyncSourceSamples(lineSamples);
    return (uint16_t)(pulse
                      - originPulseCorrection(pulse, positive, separated));
}

bool SyncProcessor::hsyncPositive()
{
    return GBS::STATUS_SYNC_PROC_HSPOL::read() != 0;
}

bool SyncProcessor::vsyncPositive()
{
    return GBS::STATUS_SYNC_PROC_VSPOL::read() != 0;
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
// into active video, where the black level comes off picture and the frame is
// discoloured. test_sync_processor.cpp holds the window measured.
const uint16_t ClampWidthSamples = 4;

// Above this the reading is not a line length the twelve-bit window can hold.
const uint32_t ClampLineCeiling = 4095;

// The line the clamp's fractions are of. A separate-sync line is counted in ADC
// samples, so it is the divider itself; the composite path's fractions were
// fitted to the quarter-count the register holds.
//
// NOT THE CALLER'S TO CHOOSE. Two callers chose differently, and the register
// then held whichever ran last.
uint32_t clampLineFor(bool csync, uint32_t lineRateHz, uint16_t divider)
{
    if (!csync)
        return divider;
    return lineRateHz == 0 ? 0 : 27000000u / (4u * lineRateHz) - 1u;
}

}  // namespace

// THE SOURCE'S LINE, FROM THE RATE THE ENGINE SOLVED, never from HPERIOD_IF.
// The register reports the same quantity and rails; this cannot, and the two
// agree exactly where it is healthy.
//
// A fraction of the whole line in 27 MHz counts.
uint32_t SyncProcessor::coastLineFor(uint32_t lineRateHz)
{
    return lineRateHz == 0 ? 0 : 27000000u / lineRateHz;
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

void SyncProcessor::placeClampFor(bool csync, bool component,
                                 uint32_t lineRateHz, uint16_t divider,
                                 const HsyncPulse &pulse)
{
    const uint32_t lineLength = clampLineFor(csync, lineRateHz, divider);
    if (lineLength == 0 || lineLength > ClampLineCeiling)
        return;

    const uint16_t offset =
        referenceMovedByPulse() ? clampOffsetForPulse(pulse, lineRateHz) : 0;
    SP_CS_CLP_ST::write(clampStartFor(lineLength, csync, component) + offset);
    SP_CS_CLP_SP::write(clampStopFor(lineLength, csync, component) + offset);
}

void SyncProcessor::invertSogReference(bool inverted)
{
    sogInverted_ = inverted;
    SP_SOG_P_INV::write(inverted ? 1 : 0);
}

bool SyncProcessor::sogReferenceInverted() { return sogInverted_; }

void SyncProcessor::referenceLeadingEdge(bool leading)
{
    leadingEdge_ = leading;
    SP_CS_P_SWAP::write(leading ? 1 : 0);
    SP_HS2PLL_INV_REG::write(leading ? 1 : 0);
}

bool SyncProcessor::referencesLeadingEdge() { return leadingEdge_; }

bool SyncProcessor::referenceMovedByPulse()
{
    return sogInverted_ || leadingEdge_;
}

uint16_t SyncProcessor::clampOffsetForPulse(const HsyncPulse &pulse,
                                           uint32_t lineRateHz)
{
    return (uint16_t)lrintf(pulse.syncDuty() * (float)coastLineFor(lineRateHz));
}

bool SyncProcessor::acquireClampWindow(bool csync, bool component,
                                       uint32_t lineRateHz, uint16_t divider,
                                       uint16_t offset)
{
    const uint32_t lineLength = clampLineFor(csync, lineRateHz, divider);
    if (lineLength == 0 || lineLength > ClampLineCeiling)
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

// THE PULSE IGNORE IS NOT HERE. It follows the measured scan rather than the
// sync type, so it is written once per pass from applyDynamic() for every route
// and a second write from here would be a second owner.
void SyncProcessor::applySeparationThresholds(bool csync)
{
    if (csync) {
        SP_PRE_COAST::write(preCoastLines());
        SP_POST_COAST::write(postCoastLines());
        applyPulseWidthDifference();
    } else {
        SP_PRE_COAST::write(0x00);
        SP_POST_COAST::write(0x00);
        SP_DLT_REG::write(0x00);
    }
}

bool SyncProcessor::signalPresent()
{
    const TestBus::Hold held;

    TestBus::select(TestBus::SyncProcessor);
    driveTestBus(TestModuleOutProc, 0);

    return TestBus::read() > SignalPresentAbove;
}

void SyncProcessor::clampFromReferenceClock()
{
    SP_CLP_SRC_SEL::write(0);
}

void SyncProcessor::setHsyncOverflowProtect(bool wanted)
{
    SP_H_PROTECT::write(wanted ? 1 : 0);
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
    invertSogReference(false);                   // s5_20[2:2]
    // The retime window's START. Its partner SP_RT_HS_SP is placed from the
    // source's measured sync width and belongs to SourceMeasurement.
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

void SyncProcessor::placeSeparatorPulseEnd(uint16_t divider, const HsyncPulse &pulse)
{
    const uint16_t samples = leadingEdge_ && pulse.isPulse()
        ? (uint16_t)lrintf((float)divider * pulse.syncDuty()) : 0;
    SP_CS_HS_SP::write(samples);
}

void SyncProcessor::writeRetimeStop(uint16_t samples)
{
    if (samples == 0)
        return;
    SP_RT_HS_SP::write(samples);
}

void SyncProcessor::disableOutput() { Chip::PAD_SYNC_OUT_ENZ::write(1); }

void SyncProcessor::enableOutput() { Chip::PAD_SYNC_OUT_ENZ::write(0); }

}  // namespace Tv5725
