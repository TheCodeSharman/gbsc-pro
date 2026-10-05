# The detection defaults are not the preset path's

`SyncProcessor::prepare()` carried a block of six writes behind an
`rgbhvRoute` parameter -- the manual clamp off, the clamp source on the
reference clock, the clamp held, a default coast window, and auto-coast off.
The parameter was `rgbhvBypass() || scalingRgbhvInForce()`, and the block was
read as something "an RGBHV route owns itself".

It is not about the route. It is the state an **unmeasured** source is counted
through, and it is live at exactly the two callers that enter detection.

## Which caller sees which route

`prepare()` has three callers, and the parameter answers differently at each
for reasons that have nothing to do with the source on the connector.

| caller | `sourceIsRgbhv()` | `scalingRgbhvInForce()` | `rgbhvRoute` |
|---|---|---|---|
| boot, before `applySavedInputSource()` | **false** -- `VideoSourceSelection::selected()` is still `None` | false | **false** |
| `goLowPowerWithInputDetection()` | follows the selection | **false** -- `setResetParameters()` clears it one line earlier | false, unless RGBHV **bypass** |
| `doPostPresetLoadSteps()` | follows the selection | true once a scaling-RGBHV load has run | true on `vga` |

So the block runs at both detection entries on every source but an RGBHV
bypass one, and is skipped on the preset path for the bench's own `vga` source.
A reading that the block "has never run at boot" inverts this: nothing is
selected there, so `isRgbhv()` is false and the block always runs.

## Why detection needs it

`zeroAll()` clears all six segments, so boot reaches detection with no coast
window at all. With the coast and the delta registers clear,
`STATUS_SYNC_PROC_HTOTAL` reads a number that does not move when the divider is
written and latched by hand -- the sync processor counts nothing usable, and
detection has no measurement to refuse or accept.

`setResetParameters()` preserves segment 5, so the low-power entry keeps the
last computed window rather than a zeroed one. It writes `holdClamp()` and
`applyDefaultClampWindow()` itself; what it does not write is the coast pair,
`SP_CLAMP_MANUAL`, `SP_CLP_SRC_SEL` and `SP_HCST_AUTO_EN`.

## Why the preset path does not

Every one of the six has a later writer on that path, and the settled unit
holds the later writer's value in each case:

| field | the block wrote | what writes it afterwards | settled `ypbpr` |
|---|---|---|---|
| `SP_CLAMP_MANUAL` | 0 | `placeClampWindow()` -> `clampManually(!component)` | 0 |
| `SP_CLP_SRC_SEL` | 0 | `clampFromReferenceClock()`, forty statements later | 0 |
| `SP_NO_CLAMP_REG` | 1 | `holdClamp()`, then `releaseClamp()` at the tail | 0 |
| `SP_H_CST_ST` | 0x10 | `acquireCoastWindow()` | 16 |
| `SP_H_CST_SP` | 0x100 | `acquireCoastWindow()`, from the measured line | 1669 |
| `SP_HCST_AUTO_EN` | 0 | `acquireCoastWindow()` | 0 |

`ypbpr` is the route that exercises this, `rgbhvRoute` being false there. No
value the settled unit holds is the block's: three are overridden outright and
three are rewritten to the same number.

## What the split is

`prepare()` loses the parameter and the block; `prepareForDetection()` carries
the six, and the boot and low-power callers ask for it. Measured on `vga` at
320x256@50 either side, with `picstate.py` reading the emitted frame:

| | before | after |
|---|---|---|
| the eleven sync-processor fields | -- | identical |
| `/geometry` | `oh` 237 `eh` 688 `ov` 72 `ev` 512 `ch` 1101 `cv` 624 | identical |
| capture margins | 242/254/2/0 | 242/254/2/0 |
| capture spread / luma | 109.4 / 81.3 | 109.3 / 81.2 |

and on `ypbpr` with the Wii in 480i, every field identical bar the two below
and `SP_H_CST_SP`, which follows the measured line rate.

One difference is deliberate: an RGBHV **bypass** source now readies the clamp
and the coast window on its way into detection, where the parameter used to
skip it. Nothing stated why that route alone should enter detection on the
previous source's window.

## The coast pair is not an oracle field

`SP_PRE_COAST` and `SP_POST_COAST` read 9/9 on some `ypbpr` acquisitions and
7/6 on others, on one build. 7/6 is `CompositePreCoastLines`/
`CompositePostCoastLines`, which `applyForSyncType()` writes; 9/9 is
`SerratedCoastLines`, reachable only through `SyncRecovery::CoastWindow` --
a **recovery ladder** step. Measured across five legs on the same build: 7/6,
7/6, 9/9, 9/9, 9/9, acquiring in 20.6 s, 33.8 s, 35.8 s, 35.8 s and 36.8 s.
Four `vga` legs beside them read 0/0 every time, acquiring in 6.1 s to 8.7 s.

So a reading of the pair taken after acquisition says whether the ladder ran,
not what the setup wrote, and two such readings are not a comparison. This is
the same shape as the overflow protect's toggle parity.
[`the-overflow-protect-had-four-writers.md`](the-overflow-protect-had-four-writers.md).
