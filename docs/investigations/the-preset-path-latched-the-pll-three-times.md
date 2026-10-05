# The preset path latched the ADC PLL three times

`SyncOnGreen::putInForce()` is four acts under one name: it writes the sync
separator's level, applies both sampling phases, latches the ADC PLL group and
acknowledges every interrupt. `doPostPresetLoadSteps()` called it three times,
and the name said the separator level each time while two of the three were
there for the latch and the third for nothing.

## What each site needed

| site | what precedes it | what it is for |
|---|---|---|
| after `Adc::choosePhaseSyncProcessor(8)` | both phases chosen, and `SyncOnGreen::choose(24)` on a csync scaling-RGBHV source | the level and the phases -- the separator's own output is read through `TestBus` forty lines below |
| after `applyFrameBufferRequests()` | `applyClockGroup()`, which writes the lock enable, both loop filters and the decimator modes | the group `PLLAD_LAT` loads on a rising edge |
| at the tail | the clamp work and `applySyncProcessorDynamic(0)` | **nothing**, and `Interrupts::acknowledgeAll()` was already on the next line |

So the first site puts the level in force and applies the phases, the second
latches, and the third is gone.

## A bare latch on an unchanged group is inert

This is what the third site turned on, because a latch is an *edge* and an edge
is not testable by comparing register values either side of it.
`Adc::restartPll()` carries the measurement that applying the group leaves the
PLL unlocked **even at the value it already held**, which reads as a latch that
needs a restart after it -- and the tail had none.

Measured on a settled `vga` source at 320x256@50, automation frozen, writing
`PLLAD_LAT` 0 then 1 and changing nothing else:

| | before | t+0.6 s through t+3.6 s |
|---|---|---|
| `PLLAD_MD` | 2200 | 2200 |
| `STATUS_SYNC_PROC_HTOTAL` | 2200 | 2200 at every sample |
| `STATUS_SYNC_PROC_VTOTAL` | 311 | 311 at every sample |
| capture margins | 242/254/2/0 | 242/254/2/0 |
| capture spread | 109.36 | 109.32 |

So the restart is required where the group was **written**, and re-loading
values already in force costs the 128 µs edge and nothing else. The third site
was dead rather than harmful, and the second site's latch does not need a
restart behind it.

## What the removal changed

Six bench states, scored off the HDMI capture inside its own borders, before and
after:

| state | before | after |
|---|---|---|
| `vga` boot | 242/254/2/0, spread 109.4 | 242/255/2/0, 109.4 |
| `vga` `/sc?~` | 241/254/2/0, 109.4 | 242/255/2/0, 109.4 |
| `vga` `SYNC 1` | 313/254/0/0, 108.8 | 314/255/0/0, 108.7 |
| `vga` `SYNC 0` | 241/254/2/0, 109.4 | 242/255/2/0, 109.4 |
| `ypbpr`, Wii in 480i | 300/284/8/52, 92.1 | 300/285/8/57, 92.2 |
| `vga` again | 242/255/2/0, 109.5 | 241/254/2/0, 109.4 |

A margin moves by a column between two acquisitions of the same state, so the
±1 either way is the capture and not the change. `STATUS_SYNC_PROC_HTOTAL`
equalled `PLLAD_MD` in all twelve readings.

`presets=` on the settled load went 188 ms to 184..187 ms over three `/sc?~`
cycles -- the two `delay(2)`s inside the removed `Adc::applyPhases()` -- and the
armed load, which brings the chip up, stayed at 903 ms.

## What stays

`putInForce()` keeps its callers. The sync separator's own walks take it as a
function pointer precisely because moving the level costs a phase re-latch and
an acknowledge, and a caller that steps the level has no business knowing that.
What went is the *preset path* reaching for the bundle where it wanted one of
the four.
