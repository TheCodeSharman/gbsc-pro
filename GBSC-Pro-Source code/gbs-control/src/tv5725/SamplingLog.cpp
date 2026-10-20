#include <string.h>

#include "SamplingLog.h"

#include "TestBusRateMeasurement.h"

#include <Arduino.h>
#include <stdio.h>

#include "../../gbs_types.h"
#include "Adc.h"
#include "SourceMeasurement.h"
#include "SyncOnGreen.h"
#include "Tv5725Log.h"

namespace Tv5725 {

namespace {

// One byte, because the console cannot carry five more columns at 60 Hz.
uint8_t ifStatusBits()
{
    return (uint8_t)(GBS::STATUS_IF_HT_OK::read()
                     | (GBS::STATUS_IF_VT_OK::read() << 1)
                     | (GBS::STATUS_IF_HT_BAD::read() << 2)
                     | (GBS::STATUS_IF_VT_BAD::read() << 3)
                     | (GBS::STATUS_IF_NO_SYNC::read() << 4)
                     | (GBS::STATUS_IF_INP_NTSC_INT::read() << 5)
                     | (GBS::STATUS_IF_INP_PAL_INT::read() << 6));
}

// Short and fixed-width so a long capture stays readable and parses without a
// schema. One line per sample; the leader is what a log filter greps for.
void readSample(uint16_t out[SamplingLog::SampleFields], uint16_t divider)
{
    out[SamplingLog::SampleDivider] = divider;
    out[SamplingLog::SamplePlladLock] = GBS::STATUS_MISC_PLLAD_LOCK::read();
    out[SamplingLog::SampleVtotal] = GBS::STATUS_SYNC_PROC_VTOTAL::read();
    out[SamplingLog::SampleHtotal] = GBS::STATUS_SYNC_PROC_HTOTAL::read();
    out[SamplingLog::SampleHlowLen] = GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
    out[SamplingLog::SampleHperiodIf] = GBS::HPERIOD_IF::read();
    out[SamplingLog::SampleVperiodIf] = GBS::VPERIOD_IF::read();
    out[SamplingLog::SampleHsact] = GBS::STATUS_SYNC_PROC_HSACT::read();
    out[SamplingLog::SampleIfBits] = ifStatusBits();
    // The whole latched interrupt byte, unacknowledged. RD-5725-1.1 documents
    // s0_0F as one 8-bit block, and bit 3 is "input source switch the mode" --
    // read by nothing in the firmware, and the one signal of a mode change that
    // no divider colours.
    out[SamplingLog::SampleIntStatus] = GBS::STATUS_0F::read();
}

void emitSample(uint32_t sinceMs, const uint16_t values[SamplingLog::SampleFields])
{
    char line[96];
    int at = snprintf(line, sizeof(line), "smp,%lu", (unsigned long)sinceMs);
    for (uint8_t i = 0;
         i < SamplingLog::SampleFields && at > 0 && at < (int)sizeof(line); ++i)
        at += snprintf(line + at, sizeof(line) - at, ",%u", (unsigned)values[i]);
    tv5725Log(line);
}

// The solved OUTPUT. Every one of these the engine calculated from held state,
// so together they say where a solve landed -- the raster, both scales, both
// display windows, both output sync pulses, and the input line the capture is
// counted in. The sync starts are here because an output sync start is a pan,
// and a comparison that leaves them out cannot see the picture move.
void readSolve(uint16_t (&into)[SamplingLog::SolveFields])
{
    into[0] = (uint16_t)GBS::VDS_HSYNC_RST::read();
    into[1] = (uint16_t)GBS::VDS_VSYNC_RST::read();
    into[2] = (uint16_t)GBS::VDS_HSCALE::read();
    into[3] = (uint16_t)GBS::VDS_VSCALE::read();
    into[4] = (uint16_t)GBS::VDS_DIS_HB_ST::read();
    into[5] = (uint16_t)GBS::VDS_DIS_HB_SP::read();
    into[6] = (uint16_t)GBS::VDS_DIS_VB_ST::read();
    into[7] = (uint16_t)GBS::VDS_DIS_VB_SP::read();
    into[8] = (uint16_t)GBS::VDS_HS_ST::read();
    into[9] = (uint16_t)GBS::VDS_HS_SP::read();
    into[10] = (uint16_t)GBS::VDS_VS_ST::read();
    into[11] = (uint16_t)GBS::VDS_VS_SP::read();
    into[12] = (uint16_t)GBS::IF_HSYNC_RST::read();
    into[13] = (uint16_t)GBS::IF_HBIN_SP::read();

    // The memory window. The display window sits inside it, and the strip
    // between the two shows whatever the playback stage fetches rather than
    // black -- which is a bar nothing in the display window can explain.
    into[14] = (uint16_t)GBS::VDS_HB_ST::read();
    into[15] = (uint16_t)GBS::VDS_HB_SP::read();
    into[16] = (uint16_t)GBS::VDS_VB_ST::read();
    into[17] = (uint16_t)GBS::VDS_VB_SP::read();

    // The capture window and the measurement the framing is anchored to. The
    // framing is held as a proportion and expanded against a sync pulse read
    // live, so a one-unit move in it shifts the near edge and the width in
    // opposite directions with every other register unchanged.
    into[18] = (uint16_t)GBS::IF_HB_SP2::read();
    into[19] = (uint16_t)GBS::IF_HB_ST2::read();
    // Which clock the display runs on. An unchanged raster on a changed clock
    // is a changed output MODE, and the sink rescales.
    into[20] = (uint16_t)GBS::PLL648_CONTROL_01::read();

    // LAST, and outside the comparison: it dithers by a unit continuously, so
    // triggering on it emits a line twice a second saying nothing moved. What
    // matters is where it CROSSES, and a crossing moves the capture window
    // above, which does trigger.
    into[21] = (uint16_t)GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
}

}  // namespace

char SamplingLog::lastWhat_[SamplingLog::BranchNameMax] = {0};
uint16_t SamplingLog::lastLines_ = 0;
bool SamplingLog::lastValid_ = false;

void SamplingLog::event(uint32_t nowMs, const char *what, uint16_t lines)
{
    if (lastValid_ && lines == lastLines_
        && strncmp(what, lastWhat_, BranchNameMax - 1) == 0)
        return;

    strncpy(lastWhat_, what, BranchNameMax - 1);
    lastWhat_[BranchNameMax - 1] = '\0';
    lastLines_ = lines;
    lastValid_ = true;

    char line[96];
    snprintf(line, sizeof(line), "evt,%lu,%s,%u", (unsigned long)nowMs, what,
             (unsigned)lines);
    tv5725Log(line);
}

SamplingLog::SamplingLog()
    : mode_(Idle), low_(0), high_(0), step_(0), dwellMs_(0), interval_(0),
      restoreDivider_(0), divider_(0), lineRateHz_(0), oversample_(1),
      durationMs_(0), startedMs_(0), stepStartedMs_(0), lastSampleMs_(0),
      sampleValid_(false), changesOnly_(false), samples_(0),
      windowMs_(0), bandLow_(0), bandHigh_(0), windowStartedMs_(0),
      windowReadings_(0), windowBelow_(0), windowAbove_(0), windowTorn_(0),
      windowLowest_(0), windowHighest_(0),
      lastSolveMs_(0), solveValid_(false)
{
    memset(solve_, 0, sizeof(solve_));
}

bool SamplingLog::active() const { return mode_ != Idle; }

bool SamplingLog::sweeping() const { return mode_ == Sweeping; }

void SamplingLog::monitor(uint32_t nowMs, uint16_t intervalMs,
                          uint32_t durationMs, bool changesOnly)
{
    mode_ = Monitoring;
    interval_ = intervalMs < 1 ? 1 : intervalMs;
    dwellMs_ = 0;
    startedMs_ = nowMs;
    stepStartedMs_ = startedMs_;
    lastSampleMs_ = startedMs_ - interval_;
    durationMs_ = durationMs;
    solveValid_ = false;
    sampleValid_ = false;
    changesOnly_ = changesOnly;
    samples_ = 0;
    tv5725Log("smp,header,ms,divider,pllad_lock,sp_vtotal,sp_htotal,"
              "hlow_len,hperiod_if,vperiod_if,hsact,ifbits,intstatus");
    tv5725Log("sol,header,ms,vds_hsync_rst,vds_vsync_rst,vds_hscale,vds_vscale,"
              "dis_hb_st,dis_hb_sp,dis_vb_st,dis_vb_sp,hs_st,hs_sp,vs_st,vs_sp,"
              "if_hsync_rst,if_hbin_sp,hb_st,hb_sp,vb_st,vb_sp,if_hb_sp2,"
              "if_hb_st2,pll648,hlow_len");
}

void SamplingLog::startWindow()
{
    windowReadings_ = 0;
    windowBelow_ = 0;
    windowAbove_ = 0;
    windowTorn_ = 0;
    windowLowest_ = 0xFFFF;
    windowHighest_ = 0;
}

void SamplingLog::widths(uint32_t nowMs, uint16_t windowMs, uint32_t durationMs,
                         uint16_t low, uint16_t high)
{
    mode_ = Widths;
    changesOnly_ = false;
    samples_ = 0;
    interval_ = 0;
    startedMs_ = nowMs;
    lastSampleMs_ = nowMs;
    durationMs_ = durationMs;
    windowMs_ = windowMs < 1 ? 1 : windowMs;
    bandLow_ = low;
    bandHigh_ = high;
    windowStartedMs_ = nowMs;
    startWindow();
    tv5725Log("wid,header,ms,n,min,max,below,above,torn");
}

void SamplingLog::emitWindow(uint32_t nowMs)
{
    char line[80];
    snprintf(line, sizeof(line), "wid,%lu,%lu,%u,%u,%lu,%lu,%lu",
             (unsigned long)(nowMs - startedMs_),
             (unsigned long)windowReadings_,
             (unsigned)(windowReadings_ ? windowLowest_ : 0),
             (unsigned)windowHighest_,
             (unsigned long)windowBelow_, (unsigned long)windowAbove_,
             (unsigned long)windowTorn_);
    tv5725Log(line);
}

void SamplingLog::sweep(uint32_t nowMs, uint16_t low, uint16_t high,
                        uint16_t step, uint16_t dwellMs, uint8_t oversample,
                        uint32_t lineRateHz)
{
    if (step == 0)
        step = 1;
    if (high > DividerCeiling)
        high = DividerCeiling;
    if (low > high)
        low = high;

    mode_ = Sweeping;
    changesOnly_ = false;
    samples_ = 0;
    startedMs_ = nowMs;
    low_ = low;
    high_ = high;
    step_ = step;
    dwellMs_ = dwellMs;
    interval_ = 2;
    lineRateHz_ = lineRateHz;
    oversample_ = oversample < 1 ? 1 : oversample;
    restoreDivider_ = GBS::PLLAD_MD::read();
    divider_ = low;
    tv5725Log("smp,header,ms_since_latch,divider,pllad_lock,sp_vtotal,"
              "sp_htotal,hlow_len,hperiod_if,vperiod_if,hsact,ifbits,intstatus");
    applyStep(nowMs);
}

void SamplingLog::rates(uint32_t nowMs, uint16_t count, bool useSyncProcessorBus,
                        uint8_t pulses)
{
    mode_ = Rating;
    changesOnly_ = false;
    samples_ = 0;
    remaining_ = count;
    interval_ = 0;
    startedMs_ = nowMs;
    lastSampleMs_ = nowMs;
    syncProcessorBus_ = useSyncProcessorBus;
    readsPerLevel_ = pulses;
    tv5725Log("rate,header,ms,sp_vtotal,field_rate_mhz");
    // readsPerLevel_ carries the pulse span here, which is what a run is
    // compared across: a reading spans N pulses, so a spurious edge costs 1/N.

}

void SamplingLog::separator(uint32_t nowMs, uint8_t low, uint8_t high,
                            uint16_t reads)
{
    mode_ = Separating;
    changesOnly_ = false;
    samples_ = 0;
    low_ = low;
    high_ = high > SyncOnGreen::LevelMax ? SyncOnGreen::LevelMax : high;
    readsPerLevel_ = reads < 1 ? 1 : (reads > 255 ? 255 : (uint8_t)reads);
    remaining_ = readsPerLevel_;
    interval_ = 0;
    startedMs_ = nowMs;
    lastSampleMs_ = nowMs;
    restoreLevel_ = SyncOnGreen::level();
    level_ = low;
    tv5725Log("sog,header,level,ms,sp_vtotal,field_rate_mhz");
    SyncOnGreen::choose(level_);
    SyncOnGreen::putInForce();
}

void SamplingLog::applyStep(uint32_t nowMs)
{
    // Through Adc, so the write and the latch stay inseparable here as
    // everywhere else. A divider written without a rising edge on PLLAD_LAT
    // leaves the PLL on the old value with every register reading correct.
    Adc::applySampleRate(divider_, lineRateHz_, oversample_);
    stepStartedMs_ = nowMs;
    lastSampleMs_ = stepStartedMs_ - interval_;
}

bool SamplingLog::sampleMoved(const uint16_t taken[SampleFields],
                              const uint16_t last[SampleFields])
{
    for (uint8_t i = 0; i < SampleFields; ++i) {
        if (i == SamplePlladLock || i == SampleHperiodIf)
            continue;
        if (taken[i] != last[i])
            return true;
    }
    return false;
}

void SamplingLog::emit(uint32_t nowMs)
{
    lastSampleMs_ = nowMs;

    uint16_t taken[SampleFields];
    readSample(taken,
               mode_ == Sweeping ? divider_ : (uint16_t)GBS::PLLAD_MD::read());
    ++samples_;
    if (changesOnly_ && sampleValid_ && !sampleMoved(taken, sample_))
        return;

    memcpy(sample_, taken, sizeof(sample_));
    sampleValid_ = true;
    emitSample(lastSampleMs_ - stepStartedMs_, taken);
}

void SamplingLog::reportSolve(uint32_t nowMs)
{
    if (solveValid_ && (uint32_t)(nowMs - lastSolveMs_) < SolveIntervalMs)
        return;
    lastSolveMs_ = nowMs;

    uint16_t solved[SolveFields];
    readSolve(solved);
    if (solveValid_
        && memcmp(solved, solve_, SolveTriggerFields * sizeof(solved[0])) == 0)
        return;
    memcpy(solve_, solved, sizeof(solve_));
    solveValid_ = true;

    char line[176];
    int at = snprintf(line, sizeof(line), "sol,%lu", (unsigned long)nowMs);
    for (uint8_t i = 0; i < SolveFields && at > 0 && at < (int)sizeof(line); ++i)
        at += snprintf(line + at, sizeof(line) - at, ",%u", (unsigned)solved[i]);
    tv5725Log(line);
}

void SamplingLog::finish(uint32_t nowMs)
{
    if (mode_ == Sweeping) {
        divider_ = restoreDivider_;
        applyStep(nowMs);
    }
    if (mode_ == Separating) {
        SyncOnGreen::choose(restoreLevel_);
        SyncOnGreen::putInForce();
    }
    mode_ = Idle;

    // The rate a run achieved is what every count taken from it is divided by,
    // and a dropped line cannot be told from a sample never taken.
    char line[48];
    snprintf(line, sizeof(line), "smp,done,%lu,%lu",
             (unsigned long)samples_, (unsigned long)(nowMs - startedMs_));
    tv5725Log(line);
}

void SamplingLog::poll(uint32_t nowMs)
{
    if (mode_ == Idle)
        return;

    const uint32_t now = nowMs;
    if ((uint32_t)(now - lastSampleMs_) < interval_)
        return;

    if (mode_ == Widths) {
        for (uint8_t taken = 0; taken < WidthReadsPerPoll; ++taken) {
            const uint16_t width = GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
            if (width != GBS::STATUS_SYNC_PROC_HLOW_LEN::read()) {
                ++windowTorn_;
                continue;
            }
            ++windowReadings_;
            if (width < windowLowest_)
                windowLowest_ = width;
            if (width > windowHighest_)
                windowHighest_ = width;
            if (width < bandLow_)
                ++windowBelow_;
            else if (width > bandHigh_)
                ++windowAbove_;
        }
        if ((uint32_t)(now - windowStartedMs_) >= windowMs_) {
            emitWindow(now);
            windowStartedMs_ = now;
            startWindow();
        }
        if ((uint32_t)(now - startedMs_) >= durationMs_)
            finish(now);
        return;
    }

    if (mode_ == Rating) {
        if (remaining_ == 0) {
            finish(now);
            return;
        }
        --remaining_;
        lastSampleMs_ = now;
        // Millihertz, because the whole question is a spread of parts per
        // thousand and %f on this part costs more than the reading is worth.
        const float hz = TestBusRateMeasurement::sourceFieldRateHz(
            syncProcessorBus_, readsPerLevel_ == 0 ? 1 : readsPerLevel_);
        char line[64];
        snprintf(line, sizeof(line), "rate,%lu,%u,%lu",
                 (unsigned long)(now - startedMs_),
                 (unsigned)GBS::STATUS_SYNC_PROC_VTOTAL::read(),
                 (unsigned long)(hz * 1000.0f + 0.5f));
        tv5725Log(line);
        return;
    }

    if (mode_ == Separating) {
        if (remaining_ == 0) {
            if (level_ >= high_) {
                finish(now);
                return;
            }
            level_ = (uint8_t)(level_ + 1);
            remaining_ = readsPerLevel_;
            SyncOnGreen::choose(level_);
            SyncOnGreen::putInForce();
            return;
        }
        --remaining_;
        lastSampleMs_ = now;

        // Re-asserted per reading rather than per step: the recovery ladder
        // writes this register too, so a level set once at the step can be
        // walked out from under the readings taken at it.
        SyncOnGreen::choose(level_);
        SyncOnGreen::apply();

        const float hz = TestBusRateMeasurement::sourceFieldRateHz(false);
        char line[64];
        snprintf(line, sizeof(line), "sog,%u,%lu,%u,%lu",
                 (unsigned)level_,
                 (unsigned long)(now - startedMs_),
                 (unsigned)GBS::STATUS_SYNC_PROC_VTOTAL::read(),
                 (unsigned long)(hz * 1000.0f + 0.5f));
        tv5725Log(line);
        return;
    }

    if (mode_ == Monitoring) {
        if ((uint32_t)(now - startedMs_) >= durationMs_) {
            finish(now);
            return;
        }
        reportSolve(now);
        emit(now);
        return;
    }

    if ((uint32_t)(now - stepStartedMs_) >= dwellMs_) {
        if (divider_ >= high_) {
            finish(now);
            return;
        }
        divider_ = (uint16_t)(divider_ + step_ > high_ ? high_ : divider_ + step_);
        applyStep(now);
        return;
    }
    emit(now);
}

}  // namespace Tv5725
