# The sampling phase is searched before the source settles, so its answer is the wrong band's

`PA_SP_S` is the phase adjuster on the ADC PLL's reference. `Adc::acquirePhase()`
walks all 32 of its values, scores each by how often the sync processor
miscounts the line, and takes `middleOfWidestCleanRun()`. The search works. **It
runs too early, and the band it scores then is not the band the settled source
has.**

Both profiles read off the device, one character per phase, `.` being all twenty
samples reading the divider exactly:

| when | `phase scan:` | dithering band | the answer |
|---|---|---|---|
| +0.6 s after the bypass switch | `................334behidfg8121..` | **16..29** | **7** |
| the same source, settled | `......13567eadc99a81............` | **6..19** | 29 |

They are very nearly complementary, so the early answer is reliably one the
settled source calls bad. Measured on `vga`, RISC PC at 1280x720@60 in
pass-through, `PLLAD_MD` 2038, arriving from the Wii on `ypbpr`.

**AND NOTHING REVISITS IT.** The early search SUCCEEDS, so `phaseFound()` is
true and `SourceMaintenance::dueAt()`'s rule -- which needs
`!source.samplingPhaseFound` -- never fires. Two scans at +0.6 s and +0.9 s,
then none in the following 95 s.

## Which band is real: the per-line wander

The scan's own metric cannot answer that, so the emitted frame does. The
measure is the per-line position of four vertical edges across frames, each
frame's whole-picture pan removed first so a displacement cannot read as a
wander.

| `PA_SP_S` | 0 | 4 | 7 | 8 | 12 | 16 | 20 | 24 | 28 | 30 |
|---|---|---|---|---|---|---|---|---|---|---|
| wander, px | 0.118 | 0.118 | **0.223** | 0.274 | **0.358** | 0.349 | 0.130 | 0.119 | 0.119 | 0.117 |

The settled band 6..19 is where the picture actually moves, so the settled scan
is the one that matters and the early one is scoring a transient.

**THE COUNT METRIC IS NOT BLIND, WHICH IS WORTH STATING BECAUSE THE CODE
COMMENT INVITES THE SUSPICION.** On the settled source the dithering band and
the wander band agree -- 6..19 against 7, 8, 12 and 16 reading 0.22..0.36 where
0, 4, 20, 24, 28 and 30 read 0.117..0.130. The search's scores are sound. The
timing of when it takes them is not.

## The band is a property of the divider, and reproducible

Two sweeps at two dividers, one acquisition apart, on the settled source:

| `PA_SP_S` | 0 | 4 | 8 | 12 | 16 | 20 | 24 | 28 |
|---|---|---|---|---|---|---|---|---|
| wander, px, `PLLAD_MD` 1740 | 0.161 | 0.096 | 0.093 | **0.070** | 0.072 | 0.073 | 0.109 | 0.199 |
| wander, px, `PLLAD_MD` 2038 | 0.122 | 0.121 | 0.243 | **0.350** | 0.335 | 0.125 | 0.120 | 0.123 |

**The quiet bands are nearly complementary**, so 12..20 is the best region at
one divider and the worst at the other. Unlike the ADC phase curve, whose offset
re-rolls per acquisition, **this band does not roll**: the 2038 row repeats to a
few thousandths across two acquisitions.

That is what makes a carried-over value actively harmful rather than merely
unoptimised, and it is why a `/phase?sp=` override is dangerous -- it is held
against the engine and survives re-acquisition, so a value tuned at one divider
stays in force after the engine re-solves to another. Measured at 0.366 px
against 0.10, with the picture visibly jittering on an ordinary desktop image.

## What the fix buys, measured

`SourceMaintenance`'s window is re-opened by reporting an unsettled search's
answer as provisional, so a later search replaces it. Ten ypbpr -> vga round
trips, reading `PA_SP_S` after the window:

| build | result |
|---|---|
| no provisional rule | **2 of 2 bad** -- `PA_SP_S` 6 both times |
| `c3d2872b4`, provisional keyed on the acquired run | **3 of 10 bad** -- 7 good at 30, and 8, 7, 8 |
| `c3d2872b4`, re-read with `phase_after_input_change.py` | **1 of 3 bad** -- 30, 8, 30 |
| `ce9098f38`, settledness on the sync pad | **2 of 10 bad** -- 30 30 31 **8** 26 31 31 30 30 **8** |
| `ce9098f38`, two further legs | **1 of 2 bad** -- **8**, 29, so 3 of 12 on that build |

The good cases land on the settled band's answer and the bad ones on the
transient band's. **The last row is not an improvement on the row above it**:
2 of 10 against 3 of 10 is inside the noise of ten legs, and the fault stands.
What the arriving-source rule removed is two ways the window could never open
at all -- it did not make the answer it opens for a good one.

### What is left, and it is now exact

**THE FIRST SEARCH TAKEN WITH THE PAD DRIVEN IS FINAL, AND THE BAND IS STILL
TRANSIENT THEN.** `presentWhenSettled()` gives the pad back immediately after
the arm's own search, so every later search is settled by construction -- and
the re-search falls due `PhaseFirstPass` 10 passes after that, 200 ms of tick
on top of the arm's 300 ms. That lands at about the 0.6 s where the band is
measured transient, so an unlucky re-search scores 16..29 exactly as the arm
did, writes its 8, and is then marked final with nothing left to re-open the
window: only a divider change, a line-rate change or a long absence forgets a
phase.

So the two bad legs are not passes where the re-search failed to happen. They
are passes where it happened too early and its answer was believed.

**A SECOND THRESHOLD IS NOT THE ANSWER** -- the first one is what this entry
is about. What is unestablished, and is the reading to take next, is **what the
band DEPENDS on**: the measurement line before the early search reads
`htotal 833` against a divider of 1444 and says `UNLATCHED`, so something the
chip can report is still moving. `STATUS_SYNC_PROC_HTOTAL` against `PLLAD_MD`
is the comparison with a right answer, and `dividerLatched()` already asks a
weaker form of it. Sweep when the search is taken against the band it scores,
and against that comparison, before choosing anything.

**THE PASS COUNT IS THE WRONG PROXY, IN BOTH HALVES OF THE RULE.**
`acquiredPasses_` is zeroed only where `sourceState_` leaves `SourceAcquired`,
so a transition the source sits through -- an output change, the pass-through
switch -- carries the run straight across it. That breaks the rule twice over:
the arm's own search satisfies the settle threshold and marks its answer final,
and above `PhaseLastPass` 60 the re-search window has closed outright, so
nothing replaces it for the life of the lock. `ClampReadyPasses` 4 and
`CoastReadyPasses` 7 both sit below `PhaseFirstPass` 10, so a half-configured
sync processor is ruled out as the explanation.

**AND NO PASS COUNT CAN EXPRESS SETTLEDNESS AT ALL.** The arm holds
`MinimumSyncAwayMs` 300, which is fifteen `DetectionIntervalMs` passes, so the
count is past any threshold by the time the arm's own search runs. It read
below ten on the device only because `loop()` is nowhere near a 20 ms pass
across a selection -- the vga leg reports `DETECT: 3117ms` -- so the threshold
was being satisfied by whatever the loop happened to cost.

So the two halves take the two things that do answer the question.
**Settledness is `!syncOutAway()`**: the sync pad being away IS the transition,
by construction rather than by threshold, and `PhaseSettledPasses` goes. **The
window keeps a count, because its give-up has to be bounded**, and takes one
that resets with the arriving source -- `arrivedPasses_`, zeroed by the
pad-away edge as well as by the source going away, and handed to
`SourceMaintenance` as the one act measured in it. Every other act there stays
in the acquired run, which is the run the source has actually been measurable
for.

**The early search is not removable**, which is what makes this a choice rather
than a deletion: `presentWhenSettled()` gives the sync pad back once the phase
has been searched, so deferring the search defers the picture.

## Models refuted on the way

| model | what killed it |
|---|---|
| the phase is simply INHERITED across the transition | the phase MOVES, 9 on ypbpr to 6 on vga, so something writes it |
| a divider change invalidates it, so key on the divider | `PLLAD_MD` is **2038 for both sources** in pass-through -- the channel writes its own divider and it is one value for every source, so the key never fires |
| add the source line rate to that key | the early search runs AFTER both the divider and the rate have reached their new values, so neither term differs by then |
| the search's count metric is blind to the wander | on the settled source the two bands agree; see above |
| `SP_H_PULSE_IGNOR` left on the previous divider's value | 81 and 48 give identical wander and an identical count spread of 1739..1741 |
| the sync-on-green level gate skipped the search | `ADC_SOGCTRL` reads 12 on the separate-sync source, above `StarvedLevel` 2, so the sweep ran |
| a half-configured sync processor | the clamp and coast are placed by pass 7, below the phase's pass 10 |

The divider and rate keys are kept regardless, being correct in their own right:
a phase searched at one sampling clock is not an answer at another, and nothing
else enforced that.

## Instruments that lied, and what to use instead

**THE WEBSOCKET CONSOLE CANNOT SEE THIS TRANSITION.** The vga selection reports
`DETECT: 3117ms` against ypbpr's `100ms`, which holds `loop()` long enough that
`webSocket.loop()` never runs and the client is dropped -- measured at 0% packet
loss, the socket closing ~23 s after the request and the whole selection's log
going with it. **Four captures read as "the engine logged nothing" and were
this.** USB serial at `/dev/ttyUSB0` carries it in full; `stty -F /dev/ttyUSB0
115200 -hupcl raw -echo` first so opening the port does not reset the board.

**A CONSOLE HARNESS MUST REPORT A CLOSED SOCKET RATHER THAN SWALLOW IT.** A
`try/except Exception: pass` around `recv()` makes a dropped connection
indistinguishable from a quiet firmware, and spins on a dead socket for the rest
of the capture.

**`STATUS_SYNC_PROC_HTOTAL`'s DITHER IS NOT THE INSTRUMENT FOR THIS.** It reads
the divider within one in every state above, good and bad alike, so a count
dither cannot separate them -- and plus or minus one is inside the healthy band
anyway.

**AND `sampling phase: chosen` IS LOGGED WHEN AN OVERRIDE CHOSE NOTHING.**
`acquirePhase()`'s override branch returns true, so a `/phase?sp=` left set
reads on the console exactly like a search that ran. The absence of
`phase scan:` beside it is the only tell, `reportScan()` being called in the
sweep branch alone. It cost one capture.
