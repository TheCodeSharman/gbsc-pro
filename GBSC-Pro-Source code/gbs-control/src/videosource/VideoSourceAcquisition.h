#ifndef VIDEOSOURCE_VIDEO_SOURCE_ACQUISITION_H_
#define VIDEOSOURCE_VIDEO_SOURCE_ACQUISITION_H_

// Decide where video comes from and keep it coming. Owns the tick, measures the
// video source, and calls Tv5725::VideoPath to solve the registers from the
// reading. docs/video-source-acquisition.md

#include <stdint.h>

#include "../tv5725/SyncMeasurement.h"
#include "../tv5725/SourceMeasurement.h"
#include "../tv5725/VideoPath.h"
#include "SourceMaintenance.h"
#include "VideoSourceSelection.h"

class VideoSourceAcquisition {
public:

    // How far the corroborating field-rate measurement may sit from the solved
    // line rate before the disagreement is believed. A second reading of the
    // same quantity by the same instrument, so what it forgives is the
    // instrument's own spread -- measured at 0.000% over 250 samples a mode.
    // ../../../docs/investigations/the-rate-tolerance-answered-five-questions.md
    // What the corroborating reading has to differ by before the held rate is
    // treated as the source's no longer. Sized by THE INSTRUMENT, not by source
    // identity: a settled reading repeats to the digit across 24 acquisitions and
    // the rate is quantised to hundredths of a hertz, 0.2 per mille at 50 Hz,
    // while a reading taken mid-change sits in a 1.3% band. This sits in the gap.
    //
    // **AND AT OR BELOW Tv5725::SourceIdentityPerThousand**, asserted in the .cpp:
    // this is the arm, so a rate change big enough to move the key has to be big
    // enough to arm the solve that follows it.
    //
    // The gross-error net acceptance uses is RateFollowsCountPerThousand, which
    // cannot see any of that band -- and it must stay wide, because a source may
    // change rate at a constant count and a RISC PC does, measured at 7.87 per
    // thousand behind one 320x256@50.
    // ../../../docs/investigations/the-rate-tolerance-answered-five-questions.md
    static const uint16_t RateCorroborationPerThousand = 2;

    // How long a settled source goes before the held rate is checked against a
    // fresh reading. A vsync spin every few hundred passes, against a fault
    // that is otherwise permanent.
    static const uint16_t RateRecheckPasses = 500;

    // How far the boot's held rate and a fresh reading of it may sit apart and
    // still be the same measurement, in parts per thousand.
    //
    // Tighter than RateCorroborationPerThousand because BOTH readings are
    // spanned over SourceMeasurement::UnconfirmedRatePulses: a single pulse
    // carries a whole source line of edge latency -- 3.2 per thousand at 311
    // lines -- and spanning divides that to 0.4, so a boot error of about one
    // per thousand is visible here where the drift tolerance cannot see it.
    // Measured, eight boots of one source agree to 0.2 per thousand.
    static const uint16_t BootRateConfirmPerThousand = 1;
    // What a pass decided that this layer cannot carry out. The frame time
    // lock and the external clock generator are the sketch's, so they are
    // REPORTED, the shape Tv5725::Deinterlacer::steer() already uses.
    struct Report {
        // The output frame time moved, so anything locked to it has to start
        // again. **NOT the same as the stamp below**: resetting the lock on
        // every pass a source is unsettled means it never establishes at all,
        // and the picture goes dark with every register reading correct.
        bool frameTimingMoved;

        // The source is not steady enough to arm a frame time lock against, so
        // the moment it was last worth trying is now. A timestamp, not an act.
        bool vsyncLockStale;

        // The output settled at a new rate, so the external clock generator can
        // be re-matched to it.
        bool outputRateSettled;
    };
    // Three answers, not two. A steady line count is the vertical half only: a
    // source can hold a correct count while the ADC samples a line it is not
    // locked to. Absent and Unlocked both want recovery, but only Unlocked is
    // worth re-probing the sync type on. docs/video-source-acquisition.md
    enum SourceState {
        SourceAbsent,
        SourceUnlocked,
        SourceAcquired,
    };

    VideoSourceAcquisition(Tv5725::SourceMeasurement &sampling,
                     Tv5725::VideoPath &videoPath);

    // One pass, from loop(). True when a mode change completes.
    bool poll(uint32_t nowMs);

    // What the last pass decided that the caller has to carry out. Cleared at
    // the start of every pass, so a caller that reads it once a pass sees each
    // decision exactly once.
    const Report &report() const;

    // Stop the pass running at all, for a bench measurement that has frozen
    // automation. An outstanding mode change survives the gate shutting.
    void useRunGate(bool (*mayRun)());

    // How the output route is moved into pass-through. Deciding to is this
    // class's, because pass-through is a statement about the measured source;
    // moving the route is a chip-wide switch the sketch still owns.
    void usePassThroughSwitch(void (*enter)());

    // The platform's clock, for the blocking walks below: they wait out
    // settling windows a poll's tick cannot express, so they need a reading
    // taken as they run rather than the one the pass started with.
    //
    // The platform is one fact for the whole firmware rather than one per
    // instance, which is what lets the separator walk below be handed to
    // Tv5725::SyncOnGreen as a plain function.
    static void useClock(uint32_t (*nowMs)());

    // The platform's watchdog feed. The sampling-phase search latches and
    // scores 34 phases with twenty readings each, which is long enough to be
    // reset out of; a file here reaching for ESP.wdtFeed() is a design signal
    // rather than a dependency to admit.
    static void useWatchdogFeed(void (*feed)());

    // Choose both sampling phases for the source in force, and put them in
    // force. False means nothing was worth choosing and neither phase moved.
    //
    // **THE GATE IS THE REASON THIS IS HERE.** The search scores each phase by
    // how steadily the sync processor counts the line, and what it counts is
    // what the ADC is RUNNING -- which is the LATCHED divider, not the one
    // PLLAD_MD reports. Run against an unlatched one, every phase reads bad and
    // the search picks noise. Eight samples rather than the two the other sites
    // take: this asks whether a sweep is worth running at all.
    bool acquireSamplingPhase();

    // The sync processor's three per-source writes, each gated on the same
    // question: a window measured off a line nobody is sending clamps to
    // picture or coasts over the wrong part of it, and a dynamic write made
    // while nothing is counted configures the block for a source that is not
    // there. sourceIsSearching() is that question and this class holds it.
    //
    // `autoCoast` brackets the sync tip rather than spanning the line, which is
    // what a source with its own vertical sync wants.
    void placeCoastWindow(bool autoCoast);
    void placeClampWindow();

    void applySyncProcessorDynamic();

    // Keep the dynamic configuration following whether the source can be
    // counted, BOTH WAYS. An edge in front of the write, so a pass that changes
    // nothing writes nothing.
    //
    // **A ONE-WAY TAKE-BACK LEAVES THE SEARCH CONFIGURATION UNREACHABLE.** What
    // ends a search is the count, and nothing wrote the configuration again
    // after one -- so a source that fell back into searching kept the settled
    // configuration and the coast inversion with it, measured on the Wii in
    // 480i as SP_COAST_INV_REG 0 with STATUS_SYNC_PROC_VTOTAL at 97. Nothing
    // else reaches it on a component source: the separator tuning that applies
    // these settings is skipped there.
    void followDynamicFacts();

    // Walk the level for the source in force, starting from what the input is
    // due: a component source runs sync on green, which is weaker than a
    // dedicated sync line, so it starts one step wider. The walk only DESCENDS,
    // so a level above the starting guess is out of reach -- nothing on the
    // bench has yet needed one, and what a level moves is not reproducible
    // across two acquisitions of the same source.
    // ../../../docs/investigations/the-composite-sync-count-re-arms-its-own-solve.md
    // Nothing is walked on a board that may not be there.
    static void acquireSeparatorLevel();

    // How long each block reset is given before the next write.
    static const uint8_t BlockResetSettleMs = 8;

    // Whether keeping the source coming is wanted at all. Off while detection
    // owns the input -- it runs a heavier search of its own -- and while the
    // user has the automatic path switched off. Told rather than measured,
    // because neither is this layer's fact yet.
    void allowMaintenance(bool allowed);

    // Whether pass-through is offerable at all. The interim stand-in for a
    // per-source override -- a single boolean cannot express one.
    // docs/video-source-acquisition.md
    void allowPassThrough(bool allowed);

    // Re-derive every register from the framing held and the source as it reads
    // NOW: measure, hand the reading over, and have the engine solve from it.
    // Measures, so it must run from loop().
    bool resolveFromSource(uint32_t nowMs);

    // The output resolution the user asked for. Held HERE, because what the
    // output should do is decided here: pass-through suspends the resolution
    // rather than replacing it, so the way back is to this rather than to one
    // nobody chose -- and while it is suspended this only records, because the
    // measurement is what decides when the output returns.
    //
    // False where the caller has to load a preset instead.
    bool setOutputResolution(const Tv5725::OutputMode *mode);

    // How often the source is counted. Every steadiness run below is counted in
    // these, so loop()'s own rate must not reach them.
    static const uint32_t DetectionIntervalMs = 20;

    // How many passes of this ladder a latch's settle spans. The settle is a
    // duration -- the PLL relocks in its own time whatever the pass rate is --
    // so the pass count is derived from the cadence rather than fitted to it.
    static const uint16_t LatchSettlePasses =
        Tv5725::SourceMeasurement::LatchSettleMs / DetectionIntervalMs;

    // How many failed passes a reading gets before the clock it was taken
    // through is blamed. DERIVED FROM THE MEASUREMENT rather than tuned: it is
    // what a reading takes to settle, so a correction that can complete has
    // completed by then.
    static const uint16_t ReferenceRetryPasses =
        2 * LatchSettlePasses + Tv5725::SourceMeasurement::SteadySamples;




    // The least a transition keeps the output sync away. Long enough for the
    // encoder to see the sync go, short enough to stay inside the sink's first
    // re-acquisition attempt: the panel's dark period quantises at about
    // 3.7 s, so a blank that runs past one costs a whole second attempt.
    // docs/investigations/the-transition-is-mostly-the-encoder.md
    static const uint32_t MinimumSyncAwayMs = 300;
    // How long the source must read unacquired before the OUTPUT is dropped.
    //
    // DERIVED FROM WHAT THE DROP COSTS rather than chosen: presentWhenSettled()
    // holds the pad away for MinimumSyncAwayMs once it goes, so a blank the
    // engine will undo sooner than that shortens nothing and buys an encoder
    // relock. A count that alternates by more than the steadiness run holds
    // reads absent for the passes it takes to re-earn, and dropping on each one
    // is a panel that flickers under a solve that never moved.
    static const uint16_t OutputBlankPasses = MinimumSyncAwayMs / DetectionIntervalMs;

    // Where the acquired run stops counting. Every threshold keyed on it is
    // well below this, and a run that saturates says the same thing as one that
    // keeps going: the source has been good for a long time.
    static const uint16_t AcquiredPassCeiling = 255;

    float sourceFieldRateHz() const;
    uint32_t sourceLineRateHz() const;

    // Whether the source runs a 15 kHz line. Survives a bypass switch, which
    // measures nothing, so it answers with the rate from the mode before it.
    bool sourceLowLineRate() const;

    SourceState sourceState() const;

    // The run starts again, for a selection that gives the arriving source its
    // own budget rather than spending what the departing one used.
    void restartRecovery();

    // The two halves of the same run, counted off the same measurement. Passes
    // since the source was last acquired, and passes since it stopped being --
    // one of them is always zero. Maintenance keys off the first and the
    // escalation ladder off the second.
    uint16_t acquiredPasses() const;
    uint16_t unmeasuredPasses() const;

    // Acquired, and nothing outstanding against it. The second half matters: a
    // mode change in flight leaves the verdict taken before the source moved.
    bool sourceIsPresent() const;

    // Neither a live count nor the run says a source is there. TWO
    // INDEPENDENT NEGATIVES, because either alone is wrong in one direction:
    // countIsSource() only asks whether a count falls in range and an unlocked
    // sync processor produces garbage inside it, while the run is still
    // re-earning itself across a mode change.
    //
    // **WHAT THIS GATES IS A SWEEP OR A TWEAK, NEVER RECOVERY.** Being wrong
    // costs one pass here; withholding recovery on the same question
    // suppressed it for 80 s on a genuinely unlocked source.
    // docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md
    bool sourceIsSearching() const;

    // Whether the pass just run advanced the run. Everything keyed on the count
    // has to happen once per count, so this is what the maintenance a caller
    // still owns is gated on -- a timer of its own beside this one drifts, and
    // then a count is answered twice or not at all.
    bool runAdvanced() const;

    // Tell it the chip latched a disturbance. Opens the expensive half of the
    // rate question, which is what sees a source returning at the same line
    // count and a different field rate. NOT an arm: the engine's own writes
    // raise the same latch, so only a rate that moved is a source that moved.
    void sourceInterrupted();

    // What a load leaves the sketch holding. Pointers rather than values
    // because the sequence sets them part way through, and the bypass leg
    // deliberately leaves applyPresetDoneStage alone.
    struct LoadState {
        bool *sourceDisconnected;
        uint8_t *applyPresetDoneStage;
        bool syncWatcherEnabled;

        // A source property the sketch still owns, so it is asked rather than
        // re-derived here.
        bool scalingRgbhv;
    };

    // Configure the path for the load that has just landed: the sync path, the
    // separator and phases, the clamp and coast windows, and the solve armed
    // against the arriving source.
    //
    // `forgetFrameLock` drops the frame time lock's stale state part way
    // through the sequence. It is handed in because the lock is constructed
    // around this class, so depending on it would close a cycle.
    void configureAfterLoad(void (*forgetFrameLock)(), const LoadState &state);

private:
    // The resolution to RUN: the one chosen wherever the encoder can transmit it
    // at the rate measured, and the tallest that fits where it cannot. 0 where
    // this layer was never told one, in which case the output mode belongs to
    // whoever set it on the path directly and there is no choice to reconcile.
    const Tv5725::OutputMode *carriedResolution() const;

    // Where the output has to move to, or 0 where it is already there. Answered
    // per measurement, because the rate moves while the choice stands still.
    const Tv5725::OutputMode *resolutionToMoveTo() const;

    // Put the output on the resolution the rate just measured can be carried at,
    // ahead of the divider: the divider is bounded by the capture that output's
    // raster can show, and the scan mode by its height.
    void moveOutputForMeasuredRate();

    bool selectionMoved();
    bool detectionDue(uint32_t nowMs);

    bool sourceMoved();

    // Report a source event and spend the latched disturbance on it, whichever
    // reason is being reported.
    // `mux` says a DIFFERENT SOURCE is arriving rather than the same one
    // having moved, which is what decides whether anything held about the
    // previous source is still evidence. Stated by the caller rather than
    // matched on `why`, so a renamed reason cannot silently change the kind.
    bool armMove(const char *why, uint16_t lines, bool mux);

    // Re-apply the pass-through sampling for the source as last measured.
    bool installSamplingForMeasurement();
    void installPassThroughClock();
    void resizePassThrough();

    // Whether the source just measured arrives intact only by being handed
    // over. Both halves are the measurement's: a raster the line doubler is not
    // needed for, at a rate that reaches the sink. docs/capture-limits.md
    bool passThroughSuitsSource() const;
    bool passThroughSourceMoved() const;
    bool gaveBackPassThrough();

    // Whether video routes around the VDS, which the mode in force says.
    bool outputIsPassedThrough() const;

    // Hand the source to the panel, moving the route only where it is not
    // already there: every measurement re-answers pass-through, so staying is
    // the usual answer and re-running the switch would drop sync output on a
    // picture that is working.
    bool passSourceThrough();

    bool rateMoved();
    bool countHeld(uint16_t lines);
    void holdSolvedSource();


    // Measure the source, through the state prepareToMeasure() just established,
    // installing the sampling clock the rate asks for between the two halves.
    // `settling` distinguishes a source that cannot be read YET from one that
    // cannot be read at all; only the second is absent.
    bool measureSource(uint32_t nowMs, bool &settling);

    // What one half's answer means for the pass. Both halves are judged the
    // same way, and neither is a measurement on its own.
    bool reading(Tv5725::SourceMeasurement::MeasurementStatus status, bool &settling);


    // One pass, with the run gate already asked and the ladder's count still
    // to advance. Split out so every return from it is counted.
    bool runPass(uint32_t nowMs, bool &detectionPass);

    Tv5725::SourceMeasurement &sampling_;
    Tv5725::VideoPath &videoPath_;
    // Whether a per-source write is worth making at all: something to write to,
    // and a source being counted to measure it against.
    bool mayWriteForSource() const;

    // What a settled source is due, and when. Holds its own cadence.
    SourceMaintenance maintenance_;
    Report report_;
    bool maintenanceAllowed_;

    // Everything a pass does beyond measuring and solving: the pre-emptive
    // separator tuning, the maintenance a settled source is due, the ladder a
    // lost one is, and the channel's sync service.
    void keepSourceComing(uint32_t nowMs);

    // The maintenance a settled source is due, and the ladder a lost one is.
    void maintainSource();
    // What a pass that could not measure the source records. NOT A RECOVERY:
    // every measurement a source change needs is taken when the source changes,
    // so there is nothing left for a timer to re-try. A ladder firing block
    // resets at a source it could not measure destroyed the reading it was
    // trying to rescue -- measured on a component source at 480p, a correct
    // count of 524 reset to 97 within ten seconds and never returning.
    void noteSourceAbsent();

    // The channel's emitted sync polarity, and the latched bit that freshens
    // it. Both are due on a wall-clock cadence of their own rather than on the
    // run, because the bit latches and reports NOW only for a reader clearing
    // it.
    void serviceChannelSync(uint32_t nowMs);

    // How often the channel's sync polarity is re-ordered and the latched
    // SOG-bad bit freshened. A wall-clock cadence rather than the run's,
    // because the bit is about the separator and not about the measurement.
    static const uint32_t ChannelSyncIntervalMs = 900;

    bool channelSyncServicedEver_;
    uint32_t channelSyncServicedMs_;

    // Present the output once a transition has settled: the source acquired
    // with nothing outstanding, the sync pad away for at least
    // MinimumSyncAwayMs, the divider latched and the sampling phase searched.
    // A blank that never took the pad -- the source flickering absent for a
    // pass -- is lifted as soon as the source is back.
    void presentWhenSettled(uint32_t nowMs);

    bool syncAwaySeen_;
    uint32_t syncAwayMs_;
    bool phaseSearched_;

    bool (*mayRun_)();
    void (*passThroughSwitch_)();
    bool passThroughAllowed_;
    const Tv5725::OutputMode *resolution_;
    uint32_t detectedMs_;
    bool detectedEver_;

    // What the last solve ran against, which is what a fresh reading is compared
    // against. 0 lines means nothing has been solved, which is what bypass
    // leaves too.
    uint16_t solvedLines_;
    uint32_t solvedLineRateHz_;

    Tv5725::SteadyRun idle_;
    bool unusableCountArmed_;

    // Whether the re-probe rung found the source carrying its own V sync. Proof
    // of a source, so the input toggle leaves the mux alone.

    // Whether a dynamic configuration has been applied at all, and whether the
    // one in force was built for a searching source. Held rather than read
    // back: the chip echoes what it was told, and half these settings are
    // indistinguishable from the ones a settled source wants.
    bool dynamicApplied_;
    bool dynamicForSearch_;
    SourceState sourceState_;
    uint16_t solvedLinePeriod_;
    uint8_t rateRun_;
    uint16_t recheckPasses_;

    bool sourceInterrupted_;

    // Consecutive polls the source has not read acquired on, which is what the
    // output blank waits out.
    uint16_t unacquiredPasses_;

    // Consecutive detection passes whose line count was inside the source
    // bounds and never settled. What it has to sit through is noise: one
    // disagreeing sample restarts the idle run, so a single glitch costs a
    // whole run to recover and this clears four of them back to back.
    //
    // It is the whole remaining cost of the deadlock, paid at
    // DetectionIntervalMs a pass, so it is derived rather than chosen: at 150
    // it was 3.0 s against a leg that measured 3.1 s.
    static const uint16_t UnsettledArmPasses =
        4 * Tv5725::SourceMeasurement::SteadySamples;

    uint16_t unsettledPasses_;
    bool unsettledArmed_;

    // Consecutive passes on which the source held as separate-sync was not
    // driving V. Sized from the window the probe gives the sync processor to
    // reacquire V after the path moves, because that is the same reacquisition
    // seen from the other side -- anything shorter answers before the processor
    // has, which is the error the probe it re-arms exists to avoid.
    static const uint16_t VsyncAbsentArmPasses =
        Tv5725::SyncMeasurement::OwnVsyncSettleMs / DetectionIntervalMs;

    uint16_t vsyncAbsentPasses_;
    bool vsyncAbsentArmed_;


    // Consecutive passes that did not reach an acquired source.
    // The field rate pass-through was last sized for, which is what a later
    // reading is the same source as. Zero where the route is not taken.
    float passThroughFieldRateHz_;
    uint16_t unmeasuredPasses_;

    // Passes since a reference clock was last advanced, so each one gets its
    // own run of passes to be measured through. Without it the whole list is
    // walked in a few hundred milliseconds, and every advance re-latches the
    // ADC PLL and forgets the sync processor's positions.
    //
    // **COUNTED, NOT STAMPED AGAINST unmeasuredPasses_.** That counter stops at
    // AcquiredPassCeiling, so the gap between it and a stamp taken from it
    // stops growing five seconds in -- and the retry stops with it, leaving a
    // source that cannot be measured with nothing that ever tries again.
    uint16_t referenceRetryPasses_;

    uint16_t acquiredPasses_;


    // The act whose window the run is in, and the one already run in it. An act
    // is due on the EDGE: without this it would run every pass of the ten
    // seconds it is given to work in.

    // Whether the pass just run could measure the source's rate.
    bool sourceMeasured_;

    // WHICH INPUT THE LAST OBSERVATION SAW, AND WHETHER THERE HAS BEEN ONE.
    // The baseline cannot be taken at construction: this layer is built before
    // setup() applies the saved input, and runPass() reaches sourceMoved() only
    // once a mode change has finished -- so the boot's own selection is still
    // unseen when its first acquisition completes, and is then read as a source
    // change that re-acquires what that acquisition just established.
    VideoSourceSelection::Id selectionSeen_;
    bool selectionEstablished_;
    bool runAdvanced_;
};

#endif  // VIDEOSOURCE_VIDEO_SOURCE_ACQUISITION_H_
