# Leaving bypass needs a count the divider cannot give

Reproduced on the bench, 2026-09-13, by accident: drive the RISC PC to 1920x1080
on `vga`, then back to 320x256. The unit enters bypass on the way up and **never
leaves it on the way down**.

## What it looks like

The console, for as long as you care to watch:

```
evt,31292746,rgbhv-leave-bypass,251,15
evt,31293375,rgbhv-leave-bypass,250,15
evt,31293461,rgbhv-leave-bypass,255,15
evt,31293505,rgbhv-leave-bypass,247,15
...
```

about nine times a second, with the line count wandering 234..267 and the
standard byte stuck at 15.

Registers at the same moment:

| | reads | should read |
|---|---|---|
| `PLLAD_MD` | 2039 | 2208 |
| `STATUS_SYNC_PROC_VTOTAL` | 239..267, never still | 311 |
| `STATUS_MISC_PLLAD_LOCK` | 0 | 1 |
| `OUT_SYNC_SEL` | 1 | 0 |

## The deadlock

**The leave-bypass arm is not gated out — it runs.** It fires on every pass and
gets nowhere, which is a different fault from the one
`the-rgbhv-ladder-is-the-only-one-on-that-path.md` describes, and it is worth
keeping the two apart:

- the arm needs a **settled line count** before it will act;
- the count comes from the sync processor, which counts in ADC clocks;
- the ADC PLL is still divided for the mode being LEFT — 2039 suits a 67.5 kHz
  line, the source is now 15.6 kHz — so it does not lock and the count wanders;
- nothing re-derives the divider, because that is what the solve does and the
  solve is what the arm is trying to reach.

**The divider is an input to the measurement that is supposed to replace it.**
That is the shape of the fault, and it is the same circularity as
`docs/sync-type-selection.md`'s: a decision taken through state that only reads
correctly once the decision is already right.

## What does and does not clear it

| tried | result |
|---|---|
| waiting 50 s with the source settled | no change |
| `/sc?~` | **no change** — it does not recover this |
| `/input?src=vga` | no change |
| `/sampleclock?md=2208&os=4` | **clears it at once** — PLL locks, `VTOTAL` settles to 311, the engine acquires at 15625 and the picture comes back |

**`/sc?~` failing here is the notable half.** `CLAUDE.md` carries it as the
recovery for a stuck divider, and that entry is about a divider stuck on the
scaling path. A divider stuck while the output is in BYPASS is not the same
state and the same recovery does not reach it.

`/sampleclock` works because it writes the whole ADC clock group through the
call the bypass switch makes and restarts the PLL afterwards — it supplies the
divider the solve would have chosen, from outside the loop that cannot reach it.
It is behind `GBS_DEBUG`, so it is not a recovery a user has.

## Why it matters to the plan

A measurement-driven escalation has to be able to run when the measurement is
the thing that is broken. The reference sampling clock exists for exactly this —
`SourceMeasurement::applyReferenceSampling()` puts a known divider on the part
so a count can be taken through something other than the last mode's clock — and
the leave-bypass arm does not use it.

**The first reading taken after a source event must be taken through a reference
divider, not through the divider the previous mode left.** That is already the
rule on the scaling path. Bypass is the path it has not reached.

## THE OUTCOME IS NO PICTURE, AND THE SINK SAYS SO

The television reports **"No signal"**, not a mode it cannot place. That is the
distinction `CLAUDE.md` draws for the stale-timing fault: a sink that rejected a
mode reports no mode, a sink holding a stale one names the old rate. So the
scaler is emitting timing the sink will not lock to at all -- which is what a
1915 x 1125 raster clocked from the 81 MHz HD-bypass seed is.

**This is a regression against the firmware this forked from**, which did not
lose the picture on a source mode change. The recovery is not merely slow, it is
absent: nothing the user can reach puts the picture back.

## What is actually left stale, measured with the full solve set

The first pass at this compared the output registers and found them identical,
which left the HDMI encoder as the only suspect. **That conclusion was an
artefact of an incomplete set.** Re-run with `SamplingLog`'s widened solve line,
the return from 1920x1080 to 320x256 leaves this:

| register | stuck at | due | what it is |
|---|---|---|---|
| `PLL648_CONTROL_01` | **53** (`0x35`) | 117 (`0x75`) | the HD-bypass display clock seed, not the external generator |
| `IF_HBIN_SP` | **2** | 272 | the line doubler's own line reset, which PLACES the picture |
| `IF_HB_SP2` / `IF_HB_ST2` | 205 / 1160 | 129 / 1084 | the capture window, still sized for the mode that was left |
| `VDS_VSCALE` | 266 | 533 | half |
| `PLLAD_MD` | 1886 | 2208 | the sampling divider |
| `OUT_SYNC_SEL` | 1 | 0 | still routed to the HD bypass channel |

Every one is a register the engine owns, and two of them move the picture by
themselves:

- **A different clock seed is a different output pixel clock**, so the sink is
  shown a different MODE while `VDS_HSYNC_RST` and the display window read
  exactly the same. Nothing downstream has to misbehave for the picture to be
  re-framed.
- **`IF_HBIN_SP` places the picture horizontally on a line-doubled source**,
  which the bench source is. 2 against 272 is not a subtle difference.

`/geometry` reports `state: absent` and a line rate of 85882 throughout, so the
engine knows it has not solved. It simply has no route back.

**A PARTIAL RECOVERY IS ITS OWN STATE, and it is what produces a picture that
looks merely wrong rather than absent.** `/sampleclock` restores the sampling
divider, the count settles and the engine reaches `acquired` -- but only the
registers that solve writes are corrected. Photographed in that state the
picture is present, panned about 59 output pixels from where a full solve puts
it, with a grey bar down the right where the playback stage fetches past the end
of what was written. A following `/sc?~` clears both. So a picture that is
present but misplaced after an excursion is an INCOMPLETE solve, and the fields
to read are the ones a raster comparison does not carry.

**The output sync pulses are NOT the cause here**, and it is worth saying so
because they are the usual one: `VDS_HS_ST` and `VDS_HS_SP` held 0 and 32 across
the whole excursion. The gap between `VDS_HS_SP` and `VDS_DIS_HB_SP` is the back
porch the sink counts to find active video, so moving either does pan the
picture -- it just did not happen here.

**Recovered by `/sampleclock?md=2208&os=4`**, after which every value in the
table above is back where it belongs, because the solve that finally runs writes
all of them.

## The claim this refutes, and the one it does not

`the-encoder-reframes-the-output.md` stands: it probed by INTERVENTION, moving
the output window 60 px by hand, confirming the registers held the new value at
the moment of the photograph, and observing that the bar did not move. That is
evidence about a static difference between two output modes.

What does not survive is the separate round-trip claim -- *"the raster registers
identical either side, therefore the encoder re-acquires"*. The raster registers
are not a sufficient set. `PLL648_CONTROL_01` and `IF_HBIN_SP` differ across a
bypass round trip while every raster register agrees, and either is enough to
move the picture.

**An encoder explanation needs an intervention, not an absence of difference.**
Nothing on this board can configure the MS9288A, so attributing an effect to it
closes an investigation rather than advancing one -- and the effect here was in
registers we own the whole way.

## The obvious fix does not work, and that is the finding

The deadlock is `countHeldStill()`: it wants 30 readings agreeing within 3, and
the count is being read through the divider the previous mode chose. The rule
for that is already in the engine -- `SourceMeasurement::applyReferenceSampling()`,
which `VideoPath::prepareToMeasure()` calls on every scaling-path mode change so
a count is never taken through the last mode's clock.

Adding exactly that to the leave-bypass arm, ahead of the count, **does not fix
it.** Tried and reverted:

```cpp
sourceSampling.applyReferenceSampling(rto->osr);
sourceLines = Tv5725::SourceMeasurement::measureSourceLines();
const uint16_t heldLines = SourceMeasurement::countHeldStill(sourceLines);
```

The divider does move -- the wandering count shifts from 234..267 to 200..238 --
and it still never holds. So the reference clock is necessary and not
sufficient: the sync path is still configured for bypass alongside it, and how
much else is missing is not enumerable, because there is no single definition of
"handle a mode change" for this path to be measured against.

**That is the point.** The scaling path handles a source that moved through
`VideoSourceAcquisition::poll()` -> `VideoPath::prepareToMeasure()` ->
`solveFromMeasurement()`. The bypassed path handles it in `runSyncWatcher()`'s
RGBHV block, which reads the count raw, runs its own hold, spells out its own
sync-processor setup, resets the display PLL by hand with a 320 ms delay,
measures its own field rate, and loads its own preset. **Two implementations of
one operation**, and only one of them has been getting the fixes.

Patching the second to match the first, call by call, is how the divergence gets
preserved rather than removed. `CLAUDE.md`, *Conventions*: an unexplained
divergence is not a risk to preserve carefully, it IS the complexity.

## RESOLVED: the layer watches a bypassed output

The acquisition layer had blinded itself. `VideoSourceAcquisition::sourceMoved()`
forgot its reference count the moment `outputMode()->isBypass()` and returned
before measuring anything, so **no source event was ever armed while bypassed**
-- and `VideoPath` had `enterBypass()` with nothing to leave it by. Leaving was
the sketch's RGBHV block alone, which is the parallel implementation this page
is about.

The line count is a property of the SOURCE and pass-through does not change it,
so the count the change into bypass settled on is a valid reference for a later
move. Keeping it is what arms the event, and arming the event is what puts the
change through `VideoPath::prepareToMeasure()` -- the sync type, the scan mode
and the reference sampling clock, all before anything is counted. That is the
rule the scaling path already had and this path could not reach.

`solveFromMeasurement()` then re-answers pass-through instead of assuming it. A
source that still suits it stays and solves nothing; one that does not leaves,
to the output bypass displaced, which `enterBypass()` now keeps.

**Measured on the reproduction above**, RISC PC 1920x1080 -> 320x256 on `vga`:
`state: acquired` six seconds after the mode change, unaided, with the whole
solve set back to its pre-excursion values byte for byte.

**BUT THE ENGINE DOES NOT DO IT ALONE, AND THE ORDERING SAYS OTHERWISE.** The
engine's solve lands before the sketch's `rgbhv-leave-bypass` event, which reads
as the engine having recovered it. Deleting the sketch's arms refutes that: with
them gone the same round trip leaves

```
sampling: 311 lines x 0.00 Hz -> line rate 0
```

for as long as you watch. The COUNT is correct and steady at 311 -- the fix
above works -- and the **field rate cannot be measured at all**, so
`measureLineRate()` refuses, `measureSource()` fails, and
`solveFromMeasurement()` is never reached. Neither `/sampleclock` nor `/sc?~`
clears that state; only a reflash did.

So the arm was doing load-bearing work before its own measurement, and the
engine was riding on it.

**The reference sampling clock was necessary and not sufficient, and still is** --
the section below stands as written, and so does its point that the sync path is
configured for bypass alongside the clock.

### What the leave path still needs, now enumerable

That section says how much else is missing "is not enumerable, because there is
no single definition of the operation". The deletion enumerates it: what the arm
did between the count and the field rate, and what `prepareToMeasure()` does not:

```cpp
Tv5725::SyncProcessor::applyForScalingRgbhv(csync);
if (!csync) {
    GBS::ADC_5_00::write(0x10);
    GBS::PLL_IS::write(0);
    GBS::PLL_VCORST::write(1);
    delay(320);
}
delay(4);
```

**`getSourceFieldRate()` returning 0.00 is the signature that this is missing.**
The vertical measurement runs through a path bypass leaves configured for
itself, so the rate is unmeasurable until the sync processor is put back on the
scaling configuration and the display PLL is restarted. Until that moves into
the engine, deleting the sketch's arms substitutes one deadlock for a worse one:
the original at least held a wandering count, this one holds a correct count and
no rate, and no user-reachable route clears it.

### What it did not fix

The picture comes back panned about 48 columns, with the display window running
past what the capture wrote. `two-instruments-decide-one-raster.md` has the
measurements: the raster does not determine the framing, a full dump differs in
five fields none of which position the picture against output sync, and the
remaining candidate is outside the register map.

## What actually removes it

Step 10 of `docs/video-source-acquisition.md`. The two paths exist because
`sourceIsRgbhv()` is `videoStandardInput == 14`, and that predicate is what
selects the parallel block -- `getVideoMode()` opens by short-circuiting on it.
The byte is what makes RGBHV a KIND OF SOURCE with its own handler instead of an
input selection with an output mode.

Once the source is measured and an output mode is chosen from the measurement,
there is no bypass-specific mode-change path left to be missing a step from.
Until then, a source mode change out of a bypassed mode has no route back and
the television reports no signal.

