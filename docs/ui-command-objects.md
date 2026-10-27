# One command object per user intent, with every UI a veneer over it

The OLED icon tree, the described menu the remote drives, the web routes and the
serial letters are four surfaces onto one machine. Where two of them reach the
chip differently, some picture is reachable through one and not the other, and
no reading says which surface put the chip in the state being looked at. That is
the same defect as a second writer on a register, one level up.

## The shape already exists, twice, and it works

**`Tv5725::Controls` is the pattern.** "A user press, routed to the engine and
logged" -- pan, zoom and nudge, returning whether the press moved the picture so
a surface can draw a limit **without reading a register back**. The remote, the
panel and the web pads all call it.

**`VideoSourceSelector` is the same pattern for an input.** It takes the
selection, sends the HC32 frame, resets the sync processor, installs the
reference clock, applies the registers and persists -- in that order, with the
order stated. All three selection paths reach it: a described-menu row and a
knob press through `InputSwHandler` both set `pendingInputSelection`, which is
the same queue `/input?src=` fills, and `loop()` takes it to `InputXXX()` ->
`applyInputSelection()` -> the selector.

So nothing here needs inventing. What is missing is COVERAGE.

## What is not covered

**116 register writes sit outside the engine** -- 108 in `gbs-control.ino`, 8 in
`OLEDMenuImplementation.cpp` -- against the rule that the sketch writes none.
They are not evenly spread, which is what makes this tractable:

| writes | where | belongs to |
|---|---|---|
| 23 | `calibrateAdcOffset()` | `Tv5725::Adc` |
| 21 | `setResetParameters()` | a reset owner; it leaves a state a preset had to undo |
| 20 + 13 | `web_service()`, `handleType2Command()` | the command switch -- a veneer writing registers |
| 4 each | `toggleIfAutoOffset()`, `moveHS()`, `drawOverlayScreens()`, `handleRemoteKey()` | `Tv5725::Controls`, extended |
| the rest | `applyPresets()`, `detectAndSwitchToActiveInput()`, `resetPLL()`, `enterHdBypass()` | engine functions still living in the sketch |
| a few | `writeBytes()`, `serviceRegisterQueue()`, the `/setreg` path | **legitimate transport, and it stays** |

**Two copies of the run-time reset, and they have already DIVERGED.**
`resetRunTimeDefaults()` in the sketch and an inline block in
`OLEDMenuImplementation.cpp` share eleven lines; the menu's copy additionally
calls `SyncMeasurement::set(false)`, `VideoRoute::toScaler()` and sets
`rto->sourceDisconnected`. Nobody decided that difference -- it is what a copy
does over time, and it is the clearest instance of the rule in `CLAUDE.md` that
an unexplained divergence IS the complexity.

**Two input functions bypass the selector.** `InputNULL()` and `InputINFO()`
hand-roll it: send a frame, set `SeleInputSource`, reset the sync processor,
apply registers, save preferences. A second implementation of the one thing
`VideoSourceSelector` exists to own.

## The order to do it in

Each step stands alone and ships alone. Earliest first because the risk is
highest and the change smallest, not because it is the biggest win.

1. **Collapse the duplicated reset onto one owner**, taking the union only where
   each extra line can be justified; where it cannot, the sketch's version is
   the one with no second writer. This is the one item already causing two
   surfaces to leave different state.
2. **`InputNULL()` and `InputINFO()` onto `VideoSourceSelector`**, so input
   selection has one implementation rather than three.
3. **`calibrateAdcOffset()` into `Tv5725::Adc` and `setResetParameters()` to its
   owner** -- 44 of the 116 writes, and pure ownership moves, so the host suite
   plus a byte-identical binary is the whole proof.
4. **Extend `Tv5725::Controls`** to the remaining press-driven writes, so a
   handler names a command and draws what it returns.
5. **Make the command switch dispatch commands rather than write registers.**
   The serial letters stay as the wire form; what a letter names becomes a
   command object, and `web_service()` stops being a place registers are
   written from.
6. **The four surfaces then hold only**: decode an input event, name a command,
   draw what it returned. No register, no ordering, no persistence.

## Where it is going: one declarative table, three surfaces generated

The end state is that a command is DECLARED once -- its name, its type, its
range or its options, what it acts on and what it is called -- and the OLED
tree, the described menu's rows and the web route are all read off that one
declaration rather than written three times. `Osd::MenuTree` is already a table
of `MenuItem`s with an action per row, so the described menu is most of the way
there; what it lacks is being the SAME table the other two surfaces read.

That is the destination rather than the next step, because a declaration can
only describe commands that exist: the table is worth building once the surface
behind it is command objects rather than register writes, and worth nothing
before. Steps 1 to 5 below are what make it describable.

## What each step is judged on

**A pure ownership move is proven by a byte-identical binary** -- stronger than
the suite, because it covers paths no test reaches -- so steps 3 and 4 compare
`build/output/gbs-control.ino.bin` and commit on the host tests alone.

**A command's surfaces can be compared without a bench trip.** `/menu?key=`
drives the described menu, `/ir?key=` injects at the receiver, `/uc?` and `/sc?`
are the letters, and `/input?src=` is the route -- so "the same intent through
two surfaces leaves the same state" is a `snapdiff.py --save` either side of
each, with no knob to turn. The knob's icon tree is the one surface with no
remote driver, so it stays a bench check.

## What not to do

**Do not add a layer beside `Tv5725::Controls` and `VideoSourceSelector`.** A
third pattern for the same job is the defect this page is about.

**A command must not read a register back to decide anything.** The engine holds
the state and derives every register from it; a command takes an intent and
hands it to the engine. `Controls` returning whether the press moved is what
replaces a read-back.

**This is not a fix for a picture fault, and must not be sold as one.** It was
raised while chasing a bad sampling phase, and the input paths were measured to
converge already -- so the phase fault lies elsewhere and closing this will not
move it.
