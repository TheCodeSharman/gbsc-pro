#include <string.h>

#include "SamplingLog.h"

#include <Arduino.h>
#include <stdio.h>

#include "../../gbs_types.h"
#include "Adc.h"
#include "SourceMeasurement.h"
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
}

}  // namespace

char SamplingLog::lastWhat_[SamplingLog::BranchNameMax] = {0};
uint16_t SamplingLog::lastLines_ = 0;
uint8_t SamplingLog::lastStandard_ = 0;
bool SamplingLog::lastValid_ = false;

void SamplingLog::event(uint32_t nowMs, const char *what, uint16_t lines,
                        uint8_t videoStandardInput)
{
    if (lastValid_ && lines == lastLines_ && videoStandardInput == lastStandard_
        && strncmp(what, lastWhat_, BranchNameMax - 1) == 0)
        return;

    strncpy(lastWhat_, what, BranchNameMax - 1);
    lastWhat_[BranchNameMax - 1] = '\0';
    lastLines_ = lines;
    lastStandard_ = videoStandardInput;
    lastValid_ = true;

    char line[96];
    snprintf(line, sizeof(line), "evt,%lu,%s,%u,%u", (unsigned long)nowMs, what,
             (unsigned)lines, (unsigned)videoStandardInput);
    tv5725Log(line);
}

SamplingLog::SamplingLog()
    : mode_(Idle), low_(0), high_(0), step_(0), dwellMs_(0), interval_(0),
      restoreDivider_(0), divider_(0), lineRateHz_(0), oversample_(1),
      durationMs_(0), startedMs_(0), stepStartedMs_(0), lastSampleMs_(0),
      lastSolveMs_(0), solveValid_(false)
{
    memset(solve_, 0, sizeof(solve_));
}

bool SamplingLog::active() const { return mode_ != Idle; }

bool SamplingLog::sweeping() const { return mode_ == Sweeping; }

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
    solveValid_ = false;
    tv5725Log("smp,header,ms,divider,pllad_lock,sp_vtotal,sp_htotal,"
              "hperiod_if,vperiod_if,hsact,ifbits,intstatus");
    tv5725Log("sol,header,ms,vds_hsync_rst,vds_vsync_rst,vds_hscale,vds_vscale,"
              "dis_hb_st,dis_hb_sp,dis_vb_st,dis_vb_sp,hs_st,hs_sp,vs_st,vs_sp,"
              "if_hsync_rst,if_hbin_sp");
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

void SamplingLog::reportSolve(uint32_t nowMs)
{
    if (solveValid_ && (uint32_t)(nowMs - lastSolveMs_) < SolveIntervalMs)
        return;
    lastSolveMs_ = nowMs;

    uint16_t solved[SolveFields];
    readSolve(solved);
    if (solveValid_ && memcmp(solved, solve_, sizeof(solved)) == 0)
        return;
    memcpy(solve_, solved, sizeof(solve_));
    solveValid_ = true;

    char line[112];
    snprintf(line, sizeof(line),
             "sol,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
             (unsigned long)nowMs, (unsigned)solved[0], (unsigned)solved[1],
             (unsigned)solved[2], (unsigned)solved[3], (unsigned)solved[4],
             (unsigned)solved[5], (unsigned)solved[6], (unsigned)solved[7],
             (unsigned)solved[8], (unsigned)solved[9], (unsigned)solved[10],
             (unsigned)solved[11], (unsigned)solved[12], (unsigned)solved[13]);
    tv5725Log(line);
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
