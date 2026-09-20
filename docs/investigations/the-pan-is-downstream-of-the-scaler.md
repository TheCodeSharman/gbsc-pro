# The pan is downstream of the scaler, and it is not the capture lag

The picture's position on the panel varies between acquisitions of one source in
one mode, by up to about 73 output pixels, with the capture window, the divider
and the scale identical. It is **not** a displacement of the captured video: the
capture places content in exactly the same units every time, and what moves is
after the horizontal scaler.

Measured on the bench RiscPC at 320x256@50 on `vga`, output 1080p, `PLLAD_MD`
2206, capture 129..1083, `VDS_HSCALE` 546.

## The measurement that settles which side it is on

`VDS_HSCALE_BYPS` takes the horizontal scaler out, so one acquisition can be
read at two magnifications. The picture's left edge on the panel is

    VDS_DIS_HB_SP + (A - IF_HB_SP2) x 1024 / VDS_HSCALE

where `A` is where the source's content begins in IF units. Reading the edge
scaled and bypassed gives two equations, and `A` falls out -- no reference frame
and no paired acquisitions, so nothing depends on the camera or on comparing two
photographs.

`VDS_HB_SP` is raised off its floor of 8 first, to 300, because the write origin
is `VDS_HB_SP + 55 + 25 x magnification` and dropping the magnification pulls the
picture left off the panel, where the edge reads as a clamp rather than a
measurement.

| acquisition | pan against a clean reference | left edge scaled | bypassed | `A` |
|---|---|---|---|---|
| displaced | **-42 columns** | 264 | 184 | **238.4** |
| clean | +7 | 314 | 234 | **238.4** |
| clean, again | +7 | 314 | 234 | **238.4** |

`A` does not move. Both edges shift by the same 50 columns, so the
scaled-minus-bypassed gap is 80 columns in every case.

**That is what rules the capture out.** A capture-side displacement of `d` units
appears as `d x 1.875` scaled and `d x 1.0` bypassed, so the gap would have to
change between a displaced acquisition and a clean one. It does not change at
all. A constant offset in OUTPUT pixels leaves the gap alone, which is what is
measured.

`A` of 238 against a capture window opening at 129 is not the start of active
video -- the edge detected is the test card's bright border, and the source's
black border lies between. The absolute value is therefore not a measurement of
anything; only its constancy across acquisitions is, and any error in the
write-origin model cancels because the same procedure is repeated.

## It is TWO states, not a range, and the solve re-rolls between them

Filtered to one divider and one capture window -- `PLLAD_MD` 2206,
`IF_HB_SP2` 129 -- six source mode round trips read
**-42, -42, +7, +6, +7, -42**. Across the night the readings cluster at about
-45 (-42, -49) and about +2 (0, -8, +6, +7), tight within one build, roughly
half the transitions each. Readings of -14 and -17 belong to `PLLAD_MD`
2278/2280 with `IF_HB_SP2` 165/166, which is a different capture window and a
different picture placement rather than the same fault.

**The dwell does not matter.** Three round trips at each of 2 s, 10 s and 45 s
held at 800x600, with the return settle fixed, all produce both outcomes:

| dwell | pans | dividers |
|---|---|---|
| 2 s | -42, -42, +7 | 2206, 2206, 2206 |
| 10 s | +6, +7, -17 | 2206, 2206, 2280 |
| 45 s | -17, -42, -14 | 2280, 2206, 2278 |

So nothing is settling and no transition is too fast. Two outcomes, chosen per
solve.

## That makes it a race, and it is why a register cannot reproduce it

The registers are the same in both outcomes at one divider, and writing the
fields that DO differ into a clean acquisition changes nothing (below). A race
is decided by the ORDER things happen in during the solve, so nothing written
afterwards can put the losing side back -- which accounts for every negative on
this page at once: no block reset clears it, a re-solve from total sync loss
lands back on a side, and an ESP reboot re-rolls rather than fixes.

**The instrument is therefore a sequence, not a state.** `SamplingLog::event()`
logs a decision as the branch takes it, which no dump afterwards can show. Two
solves logged with their ADC group writes and each `PLLAD_LAT` edge, timestamped,
are comparable in a way two register dumps are not.

## Two acquisitions DO differ in registers, and the noise floor is zero

Two full 1536-register snapshots taken 20 s apart in ONE acquisition differ in
**0 bytes**. Across a displaced and a clean acquisition, ten fields differ:

    SP_H_CST_SP    256 -> 1667     retiming H coast stop
    PLL_R            1 -> 0        display PLL skew
    PLL_S            2 -> 0        display PLL skew
    PA_SP_S         12 -> 14       PA_PLLAD phase control
    SP_HS_POL_ATO / SP_VS_POL_ATO / SP_SOG_P_ATO
    MD_SEL_VGA60     1 -> 0
    MADPT_Y_MI_DET_BYPS / MADPT_Y_MI_OFFSET

So a register dump DOES distinguish two acquisitions, and the byte-identical
result recorded elsewhere is across an `IF_HBIN_ST` excursion, which is a
different comparison.

**None of them reproduces the pan.** Writing `PLL_R` 1, `PLL_S` 2 and
`SP_H_CST_SP` 256 into a clean acquisition, singly and together, leaves the pan
at +7 and the bar unchanged. They are correlates. The PLL pair is the weaker
negative of the three, being documented as skew control for testing, which may
need a re-lock to take effect -- though `PLL_VCORST` pulsed against a settled
displacement does not move it either.

Two of the ten are owner gaps worth closing on their own account, whatever moves
the picture. `SP_H_CST_SP` 256 is `SyncProcessor::applyDefaultCoastWindow()`'s
`0x100`, left standing because `placeCoastWindow()` returns early without
writing when the line length is under its floor -- so a solve that bails leaves
the coast window sized for no line at all. `PLL_R` and `PLL_S` are written as
bare literals in the sketch and are zero whenever that path does not run.

## The encoder does not choose it

`PAD_SYNC_OUT_ENZ` set for 1.5 s and cleared drops HSOUT/VSOUT and makes the
MS9288A re-acquire, with nothing in the scaler moving: the panel goes dark and
comes back. **Eight forced re-locks in a row move the picture 0 columns**, with
every register read identical across all eight, against a noise floor of 0
columns between two photographs of an untouched unit.

So the encoder, re-locking to an UNCHANGED analog timing, puts the picture in
the same place every time. That is the narrow claim and it is all this shows: a
real solve moves the timing underneath the encoder -- the raster is rewritten,
the divider moves, sync is lost -- so an encoder that latches per acquisition is
not excluded. What is excluded is encoder nondeterminism at fixed timing.

It also makes the toggle free to use inside a round-trip harness, which is what
recovers the picture after a mode change between an SD source and a VGA-class
one, where `useHdmiSyncFix` does not arm.

## Nothing in the part puts it back

Each of these was applied to a settled -42, with the pan measured after:

| | pan after |
|---|---|
| all twelve `SFTRST_*` block resets | -42 |
| `PLLAD_VCORST`, `PLL_VCORST` | -42 |
| `/sc?~`, the low-power re-detect | -42 |
| `SFTRST_SYNC_RSTZ`, which loses sync entirely | **-42**, re-solved from nothing |
| an ESP reboot, which runs `zeroAll()` over all six segments | **-42 twice in three** |

The sync row matters most: sync is lost -- `STATUS_MISC_PLLAD_LOCK` 0,
`STATUS_SYNC_PROC_VTOTAL` 98, the divider gone to 2250 -- and the engine solves
the source again from scratch and lands on the same offset.

## The bar is a separate variable

At one pan of -42 the band beyond the picture has read 13 columns at peak 25 and
51 columns at peak 116. Same pan, same registers, five times the level. So the
pan and the bar's strength are not one quantity: the geometry decides where the
window's tail falls, and what the tail digitises to follows the clamp, as
[the-bar-at-the-right-edge-is-captured-line-tail.md](the-bar-at-the-right-edge-is-captured-line-tail.md)
already says. A dim band also under-reports its own width, because only its
brightest core clears a detector's threshold.

## What is not known

**What moves the picture.** It is after the horizontal scaler and before the
panel, with `VDS_HSCALE`, `VDS_HB_SP`, `VDS_DIS_HB_SP` and `VDS_DIS_HB_ST`
identical across the two states. The remaining output-side registers have not
been walked one at a time against a settled displacement.

**Whether the vertical axis does the same.** Only the horizontal has been
measured.

**The divider race is a different fault.** One source in one mode also solves to
`PLLAD_MD` 2206 or 2278/2280 depending on a duty read off two clocks, and
filtering to 2206 leaves the pan still bimodal -- +0, +49, +49 over three solves.
Two nondeterminisms in one solve, and closing that one leaves this one standing.
[the-duty-that-picks-the-divider-is-read-off-two-clocks.md](the-duty-that-picks-the-divider-is-read-off-two-clocks.md)

**Which ordering.** The two sides are about 72 output pixels apart, and nothing
yet says what is racing. The sequence log is unwritten.

## Method

Use `picture_shift.py`. Profile the strongest of R, G and B per column rather
than luma: the band beyond the picture changes colour, and a saturated blue one
weighs 0.114 in luma and sinks into the black around it, so a luma profile
reports no band at a screen that plainly has one. Measure the pan by
cross-correlation rather than by an edge -- the last lit column in a
bar-present frame is the bar.

A bezel feature around x1212..1233 at peak 33 shows in nearly every frame at
this camera position, including acquisitions with no bar at all. It is not one.
