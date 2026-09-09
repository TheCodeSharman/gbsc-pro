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
    // `lineRateHz` is the caller's HELD measurement, which is what picks the
    // post divider at each step. HPERIOD_IF cannot supply it: it rails on the
    // scaling path with a perfect picture, and in the unlocked state this walk
    // exists to interrogate it reads 10 -- a 613 kHz line, which puts the post
    // divider at 0 and collapses the oversampling, so the walk would move the
    // whole clock group instead of PLLAD_MD alone.
    // docs/investigations/hperiod-if-railing.md
    //
    // A rate of 0 means nothing is held, and each step then writes the divider
    // and latches, touching nothing else.
    void sweep(uint32_t nowMs, uint16_t low, uint16_t high, uint16_t step,
               uint16_t dwellMs, uint8_t oversample, uint32_t lineRateHz);

    // A decision, as it is taken. The sync watcher chooses between scaling and
    // bypass on a line count, inside loop(), and the choice is over before any
    // HTTP read can see it -- a dump afterwards shows where the firmware
    // arrived, never why. Nothing is read from the chip here: the caller passes
    // what it decided on, because that is the value the branch actually used.
    static void event(uint32_t nowMs, const char *what, uint16_t lines,
                      uint8_t videoStandardInput);

    // The longest branch name the sketch passes, plus room. A name that does
    // not fit is truncated for the comparison only, so two long names sharing a
    // prefix would read as one decision -- none do.
    static const uint8_t BranchNameMax = 24;

    bool active() const;

    // Whether the DIVIDER WALK is running, which is not the same question as
    // active(). The walk writes PLLAD_MD, which the engine owns and re-derives
    // from held state, so the two must not both be writing it -- while a
    // monitor run only reads, and watching a live engine is the point of it.
    bool sweeping() const;
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

    // What the last emitted event said. A decision the branch takes again is
    // not news, and repeating it drowns the console: measured at 37 identical
    // lines a second on a locked source.
    static char lastWhat_[BranchNameMax];
    static uint16_t lastLines_;
    static uint8_t lastStandard_;
    static bool lastValid_;
};

}  // namespace Tv5725

#endif  // TV5725_SAMPLING_LOG_H_
