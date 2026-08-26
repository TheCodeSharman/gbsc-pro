#include "SamplingLog.h"

#include <Arduino.h>
#include <stdio.h>

#include "../../gbs_types.h"
#include "Adc.h"
#include "SourceMeasurement.h"

namespace Tv5725 {

namespace {

// One byte, because the console cannot carry five more columns at 60 Hz.
uint8_t ifStatusBits()
{
    return (uint8_t)(GBS::STATUS_IF_HT_OK::read()
                     | (GBS::STATUS_IF_VT_OK::read() << 1)
                     | (GBS::STATUS_IF_HT_BAD::read() << 2)
                     | (GBS::STATUS_IF_VT_BAD::read() << 3)
                     | (GBS::STATUS_IF_NO_SYNC::read() << 4));
}

// Short and fixed-width so a long capture stays readable and parses without a
// schema. One line per sample; the leader is what a log filter greps for.
void emitLine(uint32_t sinceMs, uint16_t divider)
{
    char line[96];
    snprintf(line, sizeof(line), "smp,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u",
             (unsigned long)sinceMs,
             (unsigned)divider,
             (unsigned)GBS::STATUS_MISC_PLLAD_LOCK::read(),
             (unsigned)GBS::STATUS_SYNC_PROC_VTOTAL::read(),
             (unsigned)GBS::STATUS_SYNC_PROC_HTOTAL::read(),
             (unsigned)GBS::HPERIOD_IF::read(),
             (unsigned)GBS::VPERIOD_IF::read(),
             (unsigned)GBS::STATUS_SYNC_PROC_HSACT::read(),
             (unsigned)ifStatusBits(),
             // The whole latched interrupt byte, unacknowledged. RD-5725-1.1
             // documents s0_0F as one 8-bit block, and bit 3 is "input source
             // switch the mode" -- read by nothing in the firmware, and the one
             // signal of a mode change that no divider colours.
             (unsigned)GBS::STATUS_0F::read());
    tv5725Log(line);
}

}  // namespace

void SamplingLog::event(uint32_t nowMs, const char *what, uint16_t lines,
                        uint8_t videoStandardInput)
{
    char line[96];
    snprintf(line, sizeof(line), "evt,%lu,%s,%u,%u", (unsigned long)nowMs, what,
             (unsigned)lines, (unsigned)videoStandardInput);
    tv5725Log(line);
}

SamplingLog::SamplingLog()
    : mode_(Idle), low_(0), high_(0), step_(0), dwellMs_(0), interval_(0),
      restoreDivider_(0), divider_(0), lineRateHz_(0), oversample_(1),
      durationMs_(0), startedMs_(0), stepStartedMs_(0), lastSampleMs_(0)
{
}

bool SamplingLog::active() const { return mode_ != Idle; }

void SamplingLog::monitor(uint32_t nowMs, uint16_t intervalMs,
                          uint32_t durationMs)
{
    mode_ = Monitoring;
    interval_ = intervalMs < 1 ? 1 : intervalMs;
    dwellMs_ = 0;
    startedMs_ = nowMs;
    stepStartedMs_ = startedMs_;
    lastSampleMs_ = startedMs_ - interval_;
    durationMs_ = durationMs;
    tv5725Log("smp,header,ms,divider,pllad_lock,sp_vtotal,sp_htotal,"
              "hperiod_if,vperiod_if,hsact,ifbits,intstatus");
}

uint32_t SamplingLog::lineRateFromHPeriod(uint16_t hperiod)
{
    return 27000000u / (((uint32_t)hperiod + 1u) * 4u);
}

void SamplingLog::sweep(uint32_t nowMs, uint16_t low, uint16_t high,
                        uint16_t step, uint16_t dwellMs, uint8_t oversample)
{
    if (step == 0)
        step = 1;
    if (high > DividerCeiling)
        high = DividerCeiling;
    if (low > high)
        low = high;

    mode_ = Sweeping;
    low_ = low;
    high_ = high;
    step_ = step;
    dwellMs_ = dwellMs;
    interval_ = 2;
    lineRateHz_ = lineRateFromHPeriod(GBS::HPERIOD_IF::read());
    oversample_ = oversample < 1 ? 1 : oversample;
    restoreDivider_ = GBS::PLLAD_MD::read();
    divider_ = low;
    tv5725Log("smp,header,ms_since_latch,divider,pllad_lock,sp_vtotal,"
              "sp_htotal,hperiod_if,vperiod_if,hsact,ifbits,intstatus");
    applyStep(nowMs);
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

void SamplingLog::emit(uint32_t nowMs)
{
    lastSampleMs_ = nowMs;
    emitLine(lastSampleMs_ - stepStartedMs_,
             mode_ == Sweeping ? divider_ : (uint16_t)GBS::PLLAD_MD::read());
}

void SamplingLog::finish(uint32_t nowMs)
{
    if (mode_ == Sweeping) {
        divider_ = restoreDivider_;
        applyStep(nowMs);
    }
    mode_ = Idle;
    tv5725Log("smp,done");
}

void SamplingLog::poll(uint32_t nowMs)
{
    if (mode_ == Idle)
        return;

    const uint32_t now = nowMs;
    if ((uint32_t)(now - lastSampleMs_) < interval_)
        return;

    if (mode_ == Monitoring) {
        if ((uint32_t)(now - startedMs_) >= durationMs_) {
            finish(now);
            return;
        }
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
