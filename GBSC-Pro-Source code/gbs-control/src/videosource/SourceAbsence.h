#ifndef VIDEOSOURCE_SOURCE_ABSENCE_H_
#define VIDEOSOURCE_SOURCE_ABSENCE_H_

// How long detection has been finding nothing, and whether that is yet a reason
// to power the chip down.
//
// **ABSENCE HAS TO PERSIST.** A mux that has just moved looks exactly like an
// empty socket: the input route is queued to loop() and the switch is the
// HC32's over a UART with no readback, so measured over ten input changes 0.16
// to 1.12 s passes before detection even looks. Tearing the chip down on one
// pass costs the acquisition twice over, because setResetParameters() zeroes
// segments 0 and 2 and the rate measured through the result is then rejected
// for seconds afterwards.
//
// Powering down later costs nothing but power -- an empty socket stays an empty
// socket -- so the run only ever errs long.
// ../../../../docs/acquisition-migration-plan.md

#include <stdint.h>

class SourceAbsence {
public:
    static const uint8_t PassesBeforeLowPower = 5;

    SourceAbsence();

    // Detection claimed a source.
    void found();

    // Detection claimed nothing and no signal is reaching the sync processor.
    void missed();

    // The input was deliberately changed. **THE RUN'S PATIENCE IS FOR A DROPPED
    // MEASUREMENT AND THIS IS NOT ONE**, so it is spent up front: the first
    // pass that then finds nothing powers the chip down rather than the fifth.
    // Measured on the bench, the teardown is what makes the source appear after
    // an input change -- four passes see nothing on either instrument, the run
    // reaches its threshold, and sync is there 0.6 s after the reset.
    void selectionChanged();

    // Detection claimed nothing while a signal IS reaching it. **THE RUN MUST
    // ADVANCE ON THIS**, because the teardown is what repairs it: the chip is
    // misconfigured rather than the socket empty, and a run that treats the
    // state as no evidence holds `state: absent` with the source sitting there
    // until something outside the engine forces one.
    void undecided();

    // The caller has performed the teardown. **THE RUN RE-ARMS RATHER THAN
    // ENDING**, because a teardown that did not bring the source back is one to
    // make again: a run latched at its threshold asks for nothing further, and a
    // caller that acts once then guards itself off leaves `state: absent`
    // standing with the source present. Recovery is retried, never abandoned.
    void poweredDown();

    bool shouldPowerDown() const;

    uint8_t passes() const;

private:
    uint8_t passes_;
};

#endif  // VIDEOSOURCE_SOURCE_ABSENCE_H_
