# Code-first chip initialisation

How the TV5725 gets configured, why the preset tables are being deleted rather
than tidied, and what `Tv5725::` is for.

Read this before adding a register write anywhere.

## The problem

Upstream gbs-control configures the chip by copying byte arrays. Twelve of them,
432 bytes each, one per output mode, applied by `writeProgramArrayNew()`. They
are the only record of most of the chip's configuration, and they explain none
of it: somebody once had a working picture, saved the register file, and shipped
it. There is no derivation for any value in them and no way to tell a deliberate
setting from an accident of whatever state that unit happened to be in.

That has concrete costs, all of them paid here already:

- **Values that are simply wrong ship forever.** Two tables carry
  `PB_GENERAL_FLAG_REG = 0` where the other ten carry 58–61. A zero watermark is
  not a tuning choice.
- **The same quantity gets a different value per mode for no reason.** The SDRAM
  bus delays are nanosecond trims on board traces — a property of the PCB — and
  the twelve tables split them two ways. A trace does not get longer when the
  output resolution does.
- **Nothing can be reasoned about.** Asking "is this in spec?" has no answer
  when the only justification is that it worked for someone.

## Code first

**After the blob has been reverse engineered, the code is the authority.**

The tables were the only record, so decoding them was worth doing once. That job
*ends*, per subsystem. What replaces a table is not a prettier table — it is
hand-written code that says why each value is what it is, checked against the
datasheets for the TV5725 and for the parts around it.

Two consequences that are easy to get wrong:

- **A deliberate divergence from the tables is a result, not a discrepancy.**
  `Tv5725::MemoryBus` programs `PLL_MS = 3` where every table says 7 or 2. That
  is the point. Nothing should "reconcile" it.
- **Generated code does not count.** A generator that emits an equally opaque
  block from the same blob has moved the problem, not solved it. See
  *Nothing here is generated* below.

The target for every value, in order of preference:

1. **Derived from a datasheet.** `Tv5725::SdramTiming` computes the memory clock
   from the EM638325TS-6's tCK, tRCD and tRP; 162 MHz falls out, and 185 MHz is
   rejected with a reason. Change the part and the number changes itself.
2. **Derived from measurement**, with the measurement recorded. The SDRAM bus
   trims were swept to both ends of their range on the bench under the heaviest
   playback load, with a positive control proving the path was live.
3. **Carried as a constant with an explicit note that it has no derivation
   yet.** `MEM_DQM_DLY_REG = 5` is honest; it is marked in place, in the class
   that owns it, where the next person will see it.

(3) is acceptable. What is not acceptable is (3) *disguised* as (1) by living in
a file nobody is supposed to read.

## `Tv5725::` owns the chip registers

**One class per subsystem, in namespace `Tv5725`, with an `init()`.**

```
Tv5725::MemoryBus::init()     the SDRAM bus: clock, mode register, timing,
                              address mapping, arbitration, pin delay trim
Tv5725::FrameBuffer::apply()  the SDRAM map and its guards
Tv5725::Geometry              geometry: capture, scale, windows
```

Rules that come with it:

- **A class holds the whole of its subsystem.** Values with a derivation and
  values still carried as constants sit together. Splitting them across two
  classes gives one piece of hardware two owners, and this project has already
  paid for that: `CAP_SAFE_GUARD_EN` was written 1 by `FrameBuffer::apply()` and
  0 by an uncommented line further down `doPostPresetLoadSteps()`, which won by
  running later. Every check that compared the map against the tables passed.
- **Don't split responsibility too thinly.** The one split that is justified is
  pure arithmetic versus register traffic, and only because the pure half then
  host-compiles: `MemoryMap`/`FrameBuffer`, `SdramTiming`/`MemoryBus`. If a
  split does not buy a test, it is not worth the second file.
- **A subsystem does not read its own inputs back off the chip.** An early
  `MemoryBus` read `MEM_ACT_CYCLE` to derive the clock from it, which quietly
  made it depend on whatever wrote that field last — and the whole point is that
  soon nothing will. Derive forwards: pick the clock, compute the counts.
- **One name per field.** See CLAUDE.md; an alias is a second owner.

### Ordering

`Init` runs subsystems in **address order**, and `Chip` goes first because it
holds the `SFTRST_*_RSTZ` fields: releasing a block's reset after configuring it
discards the configuration.

The full sequence is **table → bring-up → raster → clock → windows → rate
steer**. `doPostPresetLoadSteps()` does the first two and then says
`Geometry::modeChanged()`; the rest is `Geometry::poll()`'s, which runs it in
that order once the source has settled into the new mode. Two ordering facts
that cost time to find:

- the raster is solved before the display clock is steered from it, because the
  clock's frequency comes from the seed the raster chose.
- Steering the frame rate early gave a 31 Hz frame and a black screen.

## Nothing here is generated

Every subsystem is a hand-written `Tv5725::<Subsystem>::init()` whose values
carry a derivation or an explicit note that they have none. Generated code is
the anti-pattern this campaign exists to remove, so there is no emitter and no
blob — the code is the source of truth about what the chip is told.

## The omission hazard, and why it decides the order of work

**A preset table still loads first, and the bring-up writes over the top.**

So the dangerous failure is not a wrong value — it is a **missing** one. A field
the engine forgets to write is silently supplied by the blob, and the picture
looks perfect. That error is invisible on hardware, and it stays invisible right
up until the tables are deleted, at which point it appears across nine
subsystems at once.

Two things follow:

- **Reviewing the code before the blobs are gone reviews something whose
  correctness has not been tested.** This is why the code review is scheduled
  for after preset removal, not before.
- **The host tests are what make it visible early.** Poison every bank, run
  `init()`, and ask the fake which registers were actually touched — a
  millisecond, no board, and an omission fails an assertion instead of hiding.
  `docs/testing.md`.

## Where it stands

| | |
|---|---|
| preset gap | **0** — no named field and no unnamed bit |
| owned | every subsystem, as an ordinary `Tv5725::<Subsystem>::init()` |

**The gap closed on 2026-08-15.** The last seven were the FIFO request
watermarks — `PB_MAST_FLAG_REG`, `PB_GENERAL_FLAG_REG`, `RFF_MASTER_FLAG`,
`RFF_GENERAL_FLAG`, `WFF_FF_HALF_REQ`, `WFF_LINE_FLIP` — plus `IF_LD_ST`. They
were last because they are precisely the fields the twelve tables disagree
about, so no reading of what the tables agree on could ever have supplied them.

They are **category (3), constants with no derivation**, and the notes in
`FrameBuffer.cpp` and `InputFormatter.cpp` say so. The datasheet defines
`MASTER_FLAG` as high-request timing and `GENERAL_FLAG` as low-request timing
and offers no units and no formula. What settles the choice is that the tables'
spread does not track output bandwidth — `pal_240p` carries `PB_MAST_FLAG_REG`
34 against `pal_1920x1080`'s 24, at an eighth of the pixel rate — so it is
noise, and two tables carry a `PB_GENERAL_FLAG_REG` of 0, which is a watermark
that can never fire. One set for every mode, taken from the highest-bandwidth
table, which is also the certified glitch-free bench state.

Treat a change to any of them as a bench change. `PB_FETCH_NUM` is the same
family of quantity and tore the picture across 80 of 493 framings.

## What actually blocks deleting the tables

Closing the gap was necessary and is not sufficient. Found on 2026-08-15 when
the deletion was attempted: **the output resolution is still inherited from the
table**, in two separate ways, and neither is in the original plan.

**1. The engine reads its own input back off the chip.** `Geometry::solveRaster()`
opens with

```cpp
const OutputMode *mode = OutputRaster::modeFor(GBS::VDS_VSYNC_RST::read() + 1);
```

and its comment is candid about why that works — *"the only thing taken from
what is already programmed. A preset load has just put it there."* Delete the
table and nothing puts it there: the field holds whatever the previous mode
left, or the chip's reset default on a cold boot, and `modeFor()` returns 0 so
the raster is never solved at all.

This is the anti-pattern this document already warns about two sections up — *a
subsystem does not read its own inputs back off the chip* — sitting in the one
place where it decides what the user sees. `presetPreference` is the real input
and has to be passed in.

**2. Four of the six output resolutions have no engine mode.** Measured from
the archive against `OutputRaster.cpp`:

| `presetPreference` | frame lines (NTSC / PAL) | `OutputMode`? |
|---|---|---|
| 5 `Output1080P` | 1126 / 1126 | **yes**, `Mode1080p` |
| 3 `Output720P` | 751 / 751 | **yes**, `Mode720p` |
| 0 `Output960P` | 1001 / 1001 | no |
| 4 `Output1024P` | 1067 / 1067 | no |
| 1 `Output480P` | 526 / **626** | no |
| 6 `OutputDownscale` | 264 / **314** | no |

So deleting the tables today would leave four of six resolutions with no raster
and two depending on a seed nothing writes. Each missing mode needs real
timings — sync and back porch in ns, vsync and vertical back porch in lines —
derived the way `Mode1080p` and `Mode720p` were.

Note the last two rows also **refute `OutputRaster.h`'s claim** that "frameLines
is a property of the output mode alone" and that each mode's PAL and NTSC pair
agree exactly. It holds for the top four and not for 480p/576p or downscale,
which are genuinely different output modes sharing one preference value.

Then, and only then, `writePresetTable()` and the twelve tables are deleted.
After that, and not before, the competing runtime write sites get
rationalised.

## The transient guard can be defeated by its own cross-check

`Geometry::solveRaster()` refuses a field rate that disagrees with the source's
line count by more than 2%, because a rate measured mid-preset-load is
transient and a raster solved from it is wrong by the ratio of the rates. The
comment there lists three boots of identical firmware landing on 54.47, 55.12
and 66.79 Hz where the source runs 50.08.

**But both readings are taken at the same moment, and the line count is
transient too.** When they are transient *together* they agree, the guard
passes, and the wrong raster is written — and because the solve *succeeded*,
`rasterPending_` is cleared and nothing ever retries it.

Measured 2026-08-15, on a 50 Hz source counting 311 lines. After `/sc?)` the
engine wrote a raster of **1602 x 1126**, which is exactly
`htotalFor(108 MHz, 1126, 59.87 Hz)` — a 60 Hz solve. No divider in
`DisplayClock::hzFor()` gives 50 Hz at that htotal (it would need 90.2 MHz), so
the rate used really was ~60. For that to pass, `STATUS_SYNC_PROC_VTOTAL` must
have read somewhere in 200..290 at the same instant — NTSC-like — making
`nominal` 60 and the error small. The two transients confirmed each other.

The raster stayed at 1602 for the 30 s it was watched and did not self-correct.
`/sc?~` cleared it.

This is **not** caused by the mode becoming an input: `modeForPreference(5)` and
`modeFor(VDS_VSYNC_RST + 1)` both resolve to `Mode1080p` while a table still
loads, so the change cannot alter which branch runs. It was not reproduced
against the previous build, so "pre-existing" is inference from that identity
rather than an A/B.

The fix is not obvious and is not attempted here. Requiring the line count to
be *settled* — two agreeing reads, or a minimum age since the load — would do
it, but the whole point of `rasterPending_` is that the load is too early, so
the cheap version is to make the deferral the default and the immediate solve
the exception.

## What still programs the chip outside the engine

Every address a *preset* writes has an owner — that gap reached zero on
2026-08-15. That is not the same as the engine owning them, which is what
Measured 2026-08-17, before the tool that counted it was deleted:

| owner | distinct fields | call sites | raw byte writes |
|---|---|---|---|
| engine `src/tv5725/` | 108 | 112 | 0 |
| bring-up `Tv5725::<Subsystem>::init()` | 283 | 283 | 0 |
| sketch `gbs-control.ino` | 318 | 1147 | **81** |
| other (OLED, IR, clock) | 34 | 132 | 0 |

`--contested` is the number that matters: **23 fields written by the engine AND
by legacy code**, whichever runs later winning and nothing reporting the loser.

**But the static count overstates the live conflict on a board with an Si5351,
and by a lot.** Three of the largest contested writers cannot execute here:

- `applyBestHTotal()` returns immediately on `rto->extClockGenDetected`, so its
  seven geometry writes are unreachable. The search loop in
  `setExternalClockGenFrequencySmooth()` that calls it for a *return value* is
  inert with it — it gets `true`, decrements a local, measures no change and
  breaks.
- `FrameSync::runVsync()` is the only caller of the `VDS_VSYNC_RST` /
  `VDS_VS_ST` writes in `framesync.h`, and `gbs-control.ino:8143` reads
  `rto->extClockGenDetected ? runFrequency() : runVsync(...)`. With the clock
  generator present only `runFrequency()` runs, and it steers the Si5351
  instead.
- `FrameSync::reset()`'s copies of those writes are guarded by
  `syncLastCorrection != 0`, which only `runVsync()` ever sets.

That is despite `enableFrameTimeLock` being 1 and `frameTimeLockMethod` 0 in
the preferences — the option is on, the clock generator simply takes priority.

**And the 41 writes in `doPostPresetLoadSteps()` are redundant, not
conflicting.** All of them sit above the `geometry.modeChanged()` at the end of
the same function, and the engine writes every field they touch when `poll()`
solves, so it overwrites all 41. That is now asserted rather than assumed by
`test_a_preset_load_leaves_the_engines_values_not_the_sketchs` — which compares
the registers after a preset load against the engine re-solving the same
framing alone, so the sketch winning any of them would show as a difference.

Note the test that `Geometry.h` cited for this,
`test_a_preset_load_recomputes_every_register_from_scratch`, **never existed**.
The invariant the whole ordering rests on was asserted by nobody until now.

So the genuinely live conflicts are the user-action paths — `moveHS()`,
`moveVS()`, `OSD_selectOption()`, `OSD_IR()`, `web_service()` — and the 81 raw
`writeOneByte()`/`writeBytes()` sites, which carry no field name and are
therefore invisible to any check that compares names.

## The order, as it now stands

1. ~~Own the 7 gap fields.~~ Done 2026-08-15.
2. ~~Archive the tables so the audit tooling outlives them.~~ Done 2026-08-15.
3. **Pass the output mode in** rather than reading `VDS_VSYNC_RST` back.
4. **Add the four missing `OutputMode`s**, with derived timings.
5. Delete `writePresetTable()`'s twelve callers and the tables. Safety net: dump
   1536 registers before, reflash, dump again, `snapdiff.py` — every omission
   comes back named, offline. The reference dump is
   `snapshots/BEFORE-preset-deletion-1536-2026-08-15.json`, taken with the
   picture confirmed good.
6. Graduate the eight remaining generated classes.
6a. **Bypass becomes a mode the engine understands**, not a branch that skips
   it. `bypassModeSwitch_RGBHV()` is ~130 hand-written register writes in the
   sketch and `setOutModeHdBypass()` is another; both `return` before
   `doPostPresetLoadSteps()`, so on a bypass mode change the engine is not
   consulted at all and does not even know the mode changed. `Geometry::
   enterBypass()` is the seam that call moves behind. **A bypass mode change has
   to go through the engine, and the engine has to understand bypass** -- the end
   goal is the engine owning every TV5725 register. Note this is the mode the
   RiscPC desktop boots into at
   800x600, so it is not a corner case — it is the first thing the bench sees.
7. Redesign the custom-preset slots to save **the inputs to the calculation** —
   capture, scale, pan, the framing choices — rather than a register dump.
   A slot becomes a text record of the inputs, not of the outputs. Kept as a
   follow-up deliberately so the
   feature keeps working throughout; `writePresetTable()` survives step 5 for
   custom presets alone and dies here.

Step 5 is the milestone that triggers a review pass over `dev`, a
simplification, and resequencing into logical commits for main.
