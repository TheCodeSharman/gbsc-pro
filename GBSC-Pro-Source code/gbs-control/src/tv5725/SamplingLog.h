#ifndef TV5725_SAMPLING_LOG_H_
#define TV5725_SAMPLING_LOG_H_

#include <stdint.h>

namespace Tv5725 {

// Logs what the source measurements do, sampled from loop().
//
// **HTTP CANNOT ANSWER WHAT THIS EXISTS FOR.** Polled from the host a register
// samples at tens of hertz, which cannot separate a value that genuinely dithers
// from one read torn across two states -- STATUS_MISC_PLLAD_LOCK reads a clean 1
// at one divider and 0..1 at the next over HTTP, and that difference decides
// whether anything may gate on it. Nor can it see how long a reading takes to
// settle after a latch, which is the number that says how long a mode change
// must actually wait.
//
// Two stimuli, one log. `monitor` follows a source that changes underneath it,
// which is what the mode-cycling script on the source supplies. `sweep` drives
// PLLAD_MD itself, for the question of which measurements depend on it.
//
// Non-blocking throughout: begin arms, poll advances, so the loop keeps running
// and the unit stays reachable.
class SamplingLog {
public:
    // 3008 took the unit off the network entirely, and only a power cycle
    // brought it back. docs/investigations/adc-pll-lock-range.md
    static const uint16_t DividerCeiling = 2900;

    SamplingLog();

    // Log every `intervalMs` for `durationMs`, touching nothing.
    void monitor(uint32_t nowMs, uint16_t intervalMs, uint32_t durationMs);

    // Walk the divider, logging `dwellMs` at each step from the latch onward so
    // the settling is in the record. The divider held on entry goes back at the
    // end, however the walk ends.
    //
    // The line rate the post divider is picked from comes from HPERIOD_IF,
    // which is the point: it is measured against the chip's own 27 MHz and does
    // not move with PLLAD_MD, so a sweep of PLLAD_MD cannot corrupt the one
    // input it needs. docs/tv5725-chip.md
    void sweep(uint32_t nowMs, uint16_t low, uint16_t high, uint16_t step,
               uint16_t dwellMs, uint8_t oversample);

    // 27 MHz / ((HPERIOD_IF + 1) * 4), measured across ten modes to a mean 29 ns.
    static uint32_t lineRateFromHPeriod(uint16_t hperiod);

    bool active() const;
    void poll(uint32_t nowMs);

private:
    void applyStep(uint32_t nowMs);
    void emit(uint32_t nowMs);
    void finish(uint32_t nowMs);

    enum Mode : uint8_t { Idle, Monitoring, Sweeping };

    Mode mode_;
    uint16_t low_, high_, step_, dwellMs_, interval_, restoreDivider_;
    uint16_t divider_;
    uint32_t lineRateHz_;
    uint8_t oversample_;
    uint32_t durationMs_;
    uint32_t startedMs_, stepStartedMs_, lastSampleMs_;
};

}  // namespace Tv5725

#endif  // TV5725_SAMPLING_LOG_H_
