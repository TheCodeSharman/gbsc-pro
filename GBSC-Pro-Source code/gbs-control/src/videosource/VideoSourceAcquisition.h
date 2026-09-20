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
#include "SyncRecovery.h"

class VideoSourceAcquisition {
public:

    // How far the corroborating field-rate measurement may sit from the solved
    // line rate before the disagreement is believed. A second reading of the
    // same quantity by the same instrument, so what it forgives is the
    // instrument's own spread -- measured at 0.000% over 250 samples a mode.
    // ../../../docs/investigations/the-rate-tolerance-answered-five-questions.md
    static const uint16_t RateCorroborationPerMille = 50;
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

    // `hunting` asks for the search configuration rather than the settled one.
    void applySyncProcessorDynamic(bool hunting);

    // Walk the sync separator's level for the source in force, starting from
    // what the input is due: a component source runs sync on green, which is
    // weaker than a dedicated sync line, so it starts one step wider. Nothing
    // is walked on a board that may not be there.
    static void acquireSeparatorLevel();

    // Run one rung of the escalation ladder. True means the rung SETTLED the
    // question rather than advancing it -- a lock found on the other ADC input,
    // or a sync-type re-probe that found no V sync -- so the run restarts
    // rather than escalating.
    //
    // Which rung is SyncRecovery's. The conditions here are facts about the
    // source rather than about the position, so a rung whose precondition fails
    // costs its turn and the list moves on.
    bool runRecovery(SyncRecovery::Step step, bool modeSettled);

    // Whether the source's vertical interval carries serrations, which is a
    // property of composite sync at a 15 kHz line and not of either alone.
    // docs/investigations/serrated-sync-is-not-line-rate.md
    bool sourceHasSerratedSync() const;

    // Whether detection may cross to the other connector. An explicit
    // selection is a command: a chosen input is selected whether it has a
    // signal or not, so there is nowhere to promote to.
    static bool mayChangeInput();

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
    bool resolveFromSource();

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

    // Which recovery the escalation ladder is due, from this class's own run of
    // failed passes. The count lives here because the measurement that decides
    // it does: rto->noSyncCounter advanced on a source the engine calls
    // present, and walked the ADC and the sync processor off it.
    // docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md
    SyncRecovery::Step recoveryDue() const;

    // The run starts again, for a rung that settled the question rather than
    // advancing it -- a lock found on the other ADC input, or a sync-type
    // re-probe that found no V sync. Whether a step settled anything is the
    // caller's to say; the count cannot tell.
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

    // Tell it the chip latched a disturbance. Arms a re-measure for a source
    // that returns at the same line count and a different field rate.
    void sourceInterrupted();

private:
    bool detectionDue(uint32_t nowMs);

    bool sourceMoved();

    // Report a source event and spend the latched disturbance on it, whichever
    // reason is being reported.
    bool armMove(const char *why, uint16_t lines);

    // Whether the source just measured arrives intact only by being handed
    // over. Both halves are the measurement's: a raster the line doubler is not
    // needed for, at a rate that reaches the sink. docs/capture-limits.md
    bool passThroughSuitsSource() const;

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
    bool measureSource(bool &settling);

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
    void recoverSource();

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

    // How long the output sync stays away when a solve has moved the timing the
    // encoder is locked to. Long enough for it to see the sync go, short enough
    // to stay inside the sink's first re-acquisition attempt: the panel's dark
    // period quantises at about 3.7 s, so a blank that runs past one costs a
    // whole second attempt. docs/investigations/the-transition-is-mostly-the-encoder.md
    static const uint32_t EncoderRelookMs = 300;

    bool encoderLooking_;
    uint32_t encoderLookMs_;

    // Take the sync away when a solve moved the encoder's timing, and give it
    // back once it has been away long enough.
    void serviceEncoderRelook(uint32_t nowMs);

    bool (*mayRun_)();
    void (*passThroughSwitch_)();
    // How long a lock is waited for on the other ADC input before it is given
    // back. Long enough for the sync processor to report an hsync, short
    // enough that a sweep of both inputs is not a visible stall.
    static const uint16_t OtherInputLockMs = 210;

    // The other ADC input, kept only if something locks there quickly.
    static bool tryOtherAdcInput();

    // The separator walk, reopened. `reopen` takes the walk's place with the
    // separator fully open, for a caller that has run out of walks.
    static void reacquireSeparator(bool reopen);
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
    bool ownVsyncFound_;
    SourceState sourceState_;
    uint16_t solvedLinePeriod_;
    uint8_t rateRun_;
    bool sourceInterrupted_;

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


    // Consecutive passes that did not reach an acquired source. Wrapped at the
    // ladder's cycle rather than left to run, so the cycle stays aligned.
    uint16_t unmeasuredPasses_;
    uint16_t acquiredPasses_;
    bool runAdvanced_;
};

#endif  // VIDEOSOURCE_VIDEO_SOURCE_ACQUISITION_H_
