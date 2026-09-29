# Two rasters behind one 320x256@50, and the tolerances that could not see them

A RISC PC on `vga` emits two different rasters behind one nominal 320x256@50.
The monitor definition states one of them. Source identity merged both, so one
framing entry served two rasters and the picture was visibly displaced on
whichever of the two was not the one the entry had been stored against.

| | horizontal total | hsync | line rate | field rate | sync duty |
|---|---|---|---|---|---|
| desktop | **508** | **38** | 15747.9 | 50.4744 | 7.50 - 7.55% |
| `MODE X320 Y256 C256 F50` | **512** | **36** | 15625.0 | 50.0801 | 7.09% |
| Acorn's stock AKF50 states | 512 | 36 | 15625.0 | 50.0801 | 7.031% |

Both totals land on an integer at an exact 8 MHz pixel clock -- 508.00 and
512.00 to two decimals -- so these are two real rasters rather than one raster
read twice.

## What identified them apart

**The sync duty is the measurement that carries this, because it touches no
clock.** `STATUS_SYNC_PROC_HLOW_LEN` and `STATUS_SYNC_PROC_HTOTAL` are both
counts of ADC clocks on the source's own line, so their ratio is immune to the
ESP's cycle counter, to the Si5351 and to the divider. It reads 7.50 - 7.55%
on the desktop raster against 7.09% on the mode file's, a gap of ten ADC counts
where the reading dithers one.

The field rate agrees: 8000000 / 15741 = 508.23, and 8000000 / 15625 = 512.00.

## Four explanations that do not survive

- **The engine's `Acorn` array is wrong.** It is not: all 28 rows, all nine
  fields each, are transcribed exactly from Acorn's stock AKF50, checked
  mechanically against the file.
- **The rate measurement is biased.** It is not. The instrument reads 50.0801 Hz
  on a raster independently known to be 512 x 312 at 8 MHz -- the mode file's
  value to five figures -- and 60.3169 Hz on 800x600@60 where DMT states
  60.3168.
- **The colour depth changes the emitted raster.** It does not. `C16`, `C256`
  and `C16M` at X320 Y256 F50 all give 156/2200 and 15625.
- **A legacy numbered mode is substituted.** Not from BASIC: `MODE 13` and
  `MODE 9` both give the mode file's 512/36. Whatever substitutes the second
  raster does so only when it claims the screen, which makes a game the way to
  reach it.

**A cross-check that looks decisive and is not.** Comparing the output field
rate as measured against the output raster times the display clock constrains
only the PRODUCT of the instrument's bias and the Si5351's scale error, because
the rate steer drives measured-output to measured-source and the bias cancels in
the loop. An instrument reading 0.79% high with a clock 0.79% low fits it
exactly as well. What settles it is a raster whose timing is known
independently, which is what the mode-file A/B above provides.

## The tolerances were sized for an instrument that no longer wobbles

Both identity constants were wide enough to merge the two rasters:
7.87 per thousand of rate against a 50 per thousand tolerance, and 0.0041 of
sync width against 0.005.

Measured across 24 acquisitions -- 320x256@50, 640x480@60 and 800x600@60 on
`vga`, 576i on `ypbpr`, both sync types, each reached by a real mode change --
the field rate repeats to the digit, a spread of 0.00 per thousand, and the sync
width repeats exactly on three of the four modes and dithers one ADC count
(0.00069) on the fourth.

The 60.38 / 60.72 wander the constants and three tests were justified by does
not reproduce on the mode it was taken on. It predates the median-of-three in
`SourceMeasurement::sampleFieldRateHz()`.

Identity is 3 per thousand and 0.002 now. `Tv5725::SourceKey` carries what each
one has to separate.

## Two constants answering two questions, coupled by one assert

`SourceIdentityPerThousand` was floored at `RateFollowsCountPerThousand` by a
static_assert, and those answer different questions:

- *Is this reading plausible?* Has to accept a genuine rate change at a constant
  count -- the 7.87 per thousand above is one -- and reject the 156 per thousand
  a preset load produces. Stays at 50.
- *Is this a different source?* Bounded by the instrument, which does not wander.

The assert's purpose was that a key must never move without a solve behind it.
That now holds `RateCorroborationPerThousand`, the arm, at or below identity, so
the invariant is kept by the arm rather than by leaving identity blunt.

## The persisted framing line had to carry a fraction first

`FramingLine` wrote the rate as `lrintf(key.rateHz())`. A 5% identity always
matched that; 3 per thousand does not, for any mode sitting further from an
integer:

| mode | live | stored | gap |
|---|---|---|---|
| 320x256@50, the second raster | 50.4744 | 50 | 9.49 per thousand |
| 800x600@60 | 60.3169 | 60 | 5.28 |
| 800x600@56 | 56.250 | 56 | 4.46 |

Those records were written and then unreadable, which is a loop rather than a
loss: the framing is reset by hand, saved against a rate rounded to 50, missed on
the next boot, and reset again. `/framing.txt` on the bench held
`311@50/732++` beside `311@50/686++` -- the two rasters correctly separate on
sync width, both claiming a rate of 50.

The rate is written to hundredths now, in integer arithmetic because `%f` is not
dependable on this target, and a record with no fraction is still read as whole
hertz so a file written before this keeps every entry within tolerance of an
integer.

## What is still open

**Which agent substitutes the second raster.** It is not the mode file, not the
depth and not a numbered mode requested from BASIC. A resident module hooking
the mode system fits every negative, and reaching the raster on demand needs
whatever claims the screen.

**The first acquisition after a boot reads 0.45 per thousand low**, which is
inside one identity and so changes no key, but is not harmless: with the frame
time lock enabled it becomes a tear that crawls. `../known-issues.md`.
