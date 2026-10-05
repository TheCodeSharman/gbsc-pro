# Finishing the acquisition migration

`src/videosource/` and `src/tv5725/` hold the engine. The entry half of the
acquisition path is still in `gbs-control.ino`, and **that is where the faults
are**. This page is the order to move it in, and the seam each step cuts on.

## What is left, measured

    260  doPostPresetLoadSteps
    256  detectAndSwitchToActiveInput
    151  applyPresets
     70  inputAndSyncDetect
     27  loadComputedPreset
     24  applySavedInputSource
     20  runSourceRecovery
     18  goLowPowerWithInputDetection
     15  CheckInputFrequency
      7  syncTypeHasOwnVsync        7  prepareSyncProcessor
      5  advancePhase               4  sourceHasOwnVsync
      4  setAndLatchPhaseSP         4  setAndLatchPhaseADC
      4  detectionMayChangeInput
    ---
    880  in the sketch, against 1433 in src/videosource/

## Why it keeps costing sessions

**The sketch is not host-compiled, so nothing in that 880 lines has a unit
test, and every fault in it costs a flash cycle and the bench.** The faults
found so far make the point:

| fault | where | what it cost |
|---|---|---|
| the divider limit-cycles | `VideoPath` | one failing host test, fixed in a cycle |
| the 450 ms hsync wait never waits | `detectAndSwitchToActiveInput` | hardware round trips |
| a single sample powers the chip down | `inputAndSyncDetect` | reflashing the OLD build to prove the test red |
| the separator search cannot exit early | `detectAndSwitchToActiveInput` | filed, untestable where it lives |
| the held sync type outlives the source | spread across both | a session |
| leaving low power is a flag, not an act | `setResetParameters`, with the only build-up in `doPostPresetLoadSteps` | an evening, and it was diagnosed only from the console -- every register read correct throughout |

That last one is the shape of the whole page. The act of waking the chip lived
in a 262-line sketch function reached from five `/uc?` sites, so when the engine
stopped loading presets the build-up went with it and nothing said so. The
repair is two lines in two owned classes, each with a host test:
`Adc::applySampleRate()` restarts the PLL it configures and
`VideoPath::prepareToMeasure()` builds the chip up when it finds it armed.
`docs/investigations/the-ladder-never-restarts-the-adc-pll.md`.

The one fault in the engine was the cheapest to find, prove and fix. **Where a
thing lives decides what it costs to get wrong.**

## The order

Each step names the seam, so a step can be judged before it is taken. The rule
throughout: **move the DECISION, leave the blocking to the caller.** A pure
decision is host-testable; a `delay()` is not.

### 1. The absence decision -- DONE

`SourceAbsence` holds the run and answers `shouldPowerDown()`. The sketch keeps
the `goLowPowerWithInputDetection()` call, which is the blocking half.

**The caller has THREE cases, not two**, and the third was expressed only by the
absence of an assignment: detection claimed nothing while a signal IS reaching
the sync processor. That is neither evidence, so `undecided()` neither advances
the run nor ends it.

The run saturates. It was a `uint8_t` incremented without a ceiling, so an
absence lasting 256 passes withdrew a verdict already reached and the chip came
back up for five more passes before reaching it again.

### 2. The detection pass's shape

`detectAndSwitchToActiveInput()` is one `while` whose body always returns, two
6000 ms searches and a ratchet. What is testable is the SEQUENCE: which branch a
pass takes, given the selection, what the sync processor reports, and how long
it has been waiting.

Seam: a class answering "what should this pass do next" -- wait, take the RGB
branch, take the component branch, give up -- from injected readings and a
clock. The sketch performs the answer and owns `delay()` and `handleWiFi()`.

Retires: the 450 ms wait that never waits, and the search whose early exit
cannot fire, both as host tests.

Note: `SyncSearch` is this pattern already, extracted and host-tested. Follow it.

### 3. The sync arrangement

Which sync a connector carries is already knowable --
`VideoSourceSelection::syncTypeMustBeMeasured()` answers it, and only VGA can
present either. What is NOT settled is when the arrangement is re-applied: a
held answer can outlive the source, and an engine that has already reached
`acquired` on a count measured through the wrong path does not re-probe.

Seam: one owner for "the sync arrangement in force and what it was chosen for",
alongside `SourceMeasurement`'s ownership of the measured facts. A source
identity change invalidates it, the same way a divider is held against the rate
it was sized from.

**Take the measurement first.** Trace the sync-type decision across an input
change with `SamplingLog::event()` before writing this one; the shape of the fix
depends on whether the probe is skipped, refused, or never re-armed.

### 4. The preset load

`doPostPresetLoadSteps()` is the largest single block and it is mostly register
writes already owned by `src/tv5725/` classes, called in order. It is also where
the user-preference bits are established, which is why a register with no owner
on the path that ran carries whatever survived the last reset.

Seam: move it one subsystem at a time -- each group of writes becomes that
class's `apply...()`, called in the same order. The order is the risk, so change
nothing about it in the same commit as a move.

**The four blocking waits are gone, and none of them became a state anywhere.**
Instrumented on the unit before removal, they were dead: the 2002 ms wait for
hsync is entered with hsync already active on every path including the boot, the
separator walk its timeout arms is unreachable, the 30 ms precedes placements
that are undone twice before the function returns, and the 400 ms follows a
reset release the part needs no settle for. A vga boot's preset load went from
615 ms to 188 ms.
`investigations/the-preset-path-waits-for-nothing.md`.

### 5. The recovery ladder's entry

`runSourceRecovery()` is already thin and gates on a 500 ms interval. Moving it
is mostly moving the gate.

## What keeps it from coming back

- **A register has one owner, and the owner is a class in `src/`.** A field
  written from the sketch has no test and no owner.
- **The sketch only shrinks.** A new decision goes in `src/` even when its
  caller is in the sketch.
- **Move the decision, not the blocking.** `delay()`, `handleWiFi()` and the
  UART stay with the caller; what is moved is what can be asked a question.
- **A move is behaviour-preserving and says so.** The order of register writes
  is load-bearing and undocumented in places; changing it belongs in its own
  commit with its own evidence.
