# The encoder's ceiling is what bounds the output mode, and it is the datasheet's

The output raster had no floor. `OutputMode::solve()` refused a total above
`MaxHorizontalTotal` and `OutputTiming::usable()` checked only for zero, so a
mode too fast to be carried was written out with every register agreeing with
every other. What bounds it is the clock the ENCODER has to transmit, and that is
derivable rather than swept.

## The quantity

The MS9288A resamples our line into the standard's active pixel count, so what it
transmits is the STANDARD's raster at the source's field rate:

    transmitted = totalPx x frameLines x fieldRate

`totalPx` and `frameLines` belong to the output mode, the field rate to the
source, and **no display clock of ours appears in it**. That is the property that
makes it a bound on the MODE: a faster clock buys raster width and does not move
this at all, so a mode the encoder cannot carry cannot be bought back.

`OutputMode::transmittedClockHz()` is the quantity and `encoderCanTransmit()` the
single owner of the rule.

## The ceiling

MS9288A-Datasheet-Rev-B0 states 165 MHz three times -- the part's maximum
conversion rate, 165 MSPS at the ADC, and an integrated HD TMDS transmitter
running at 165 MHz -- and gives the greatest resolution as 1080p@60, whose TMDS
clock is 148.5 MHz and sits just under it. `OutputMode::EncoderCeilingHz`.

**It is a third ceiling and it is not a ceiling on any clock this board runs.**
`WorkingCeilingHz` is what the part will drive, `EngineCeilingHz` what the engine
may ask for, and this one bounds the output MODE against the source's rate. The
three answer different questions; collapsing them loses the distinction that a
faster display clock cannot reach this one.

## What it explains

Four points, three clean and one unusable:

| output | field rate | transmitted | result |
|---|---|---|---|
| 1080p | 50.00 Hz | 123.75 MHz | clean |
| 720p | 84.68 Hz | 104.79 MHz | clean |
| 1080p | 60.00 Hz | 148.50 MHz | clean |
| 1080p | 84.68 Hz | **209.58 MHz** | unusable |

**Nothing above the ceiling has ever been clean, and nothing below it has failed
for a reason that reaches the encoder.** Two further states either side of it
close the set:

- **960p at 75 Hz is 135.0 MHz**, in spec, and is the state the capture-origin
  and blanking measurements were taken through. It is clipped, and that has its
  own derivation and its own entry -- the porch is a duration while the span is a
  fraction, so above 70.9 Hz the two stop fitting the shrinking line.
- **1080p at 75 Hz is 185.6 MHz**, out of spec by 12%, and is recorded as
  arriving corrupt with nothing touched, with capture-origin readings that do not
  repeat between acquisitions -- 800x600@75 read +8, +29, +17 and +15 samples on
  four of them. `known-issues.md` already noted that 1125 lines at 75 Hz is not a
  standard mode. So it is not a state to protect.

## Why the two earlier candidates could not be separated, and this one needs no sweep

The old entry offered the raster width and the output line rate, and could not
choose between them. At a fixed display clock they are reciprocal:

    width x transmitted = clock x totalPx

so every available point moves both, and the only thing that separates the two
readings is `totalPx`, which the measured points never varied independently.
Neither candidate has a number of its own: a width floor and a line-rate floor
are both swept constants, placed by bisecting between 1600 and 1134 px or between
67.4 and 95.3 kHz.

The transmitted clock is not swept. The 165 MHz comes from the datasheet, so the
four points are a TEST of it rather than its source -- three below, one above, and
the verdicts agree. That is a different kind of evidence from a fit.

**A distinguishing prediction, unrun:** 1024p at 84.68 Hz asks 152.4 MHz at an
output line rate of 90.3 kHz. A line-rate floor that refuses 95.3 kHz and admits
67.4 kHz has to be squeezed into 90.3..95.3 kHz to admit it; the transmitted
clock admits it with 12.6 MHz to spare.

## The smeared overlay does not localise the fault to the board

The symptom at 209.6 MHz is the STV9426 overlay smearing into blue bands across
the top of the frame, which was read as a board fault on the grounds that the
overlay is generated on the board against `HS_OUT`/`VS_OUT` and so reads the
output raster directly. **It is keyed into the analog video at U13, which is ahead
of the encoder**, so it reaches the panel through the encoder exactly as the
picture does. A link running 27% outside its rating corrupts the overlay and the
picture together, and the overlay being the sharpest symptom says only that it is
the highest-contrast thing in the frame.

## The fallback

A refusal alone is a black screen, so the resolution gives way instead:
`OutputMode::transmittableFor()` answers the choice wherever the encoder can
carry it, and otherwise the tallest mode that it can carry and that is no taller
than the choice.

**Fall back, never up.** 1024p costs the encoder very slightly less than 960p --
1688 x 1066 against 1800 x 1000 -- which is the one inversion of height against
cost in the set, and it makes the rule observable in a window about 0.03 Hz wide:
960p is refused at 91.68 Hz and 1024p is not.

**The choice is not written over.** `VideoSourceAcquisition::resolution_` stays
what the user asked for and the substitution is answered per measurement, beside
the pass-through route and for the same reason: what the encoder can transmit
depends on the field rate, and the rate moves while the choice stands still. So
the preference returns when the source does, and no new state is held anywhere to
make that work.

Where this layer was never told a resolution the output mode belongs to whoever
set it on the path directly, and nothing is reconciled.

## What each rate lands on, at a 1080p preference

The threshold is `165 MHz / (2200 x 1125)` = **66.67 Hz** for 1080p and 91.7 Hz
for 1024p.

| source rate | lands on | transmitted | raster |
|---|---|---|---|
| 50.48 Hz | 1080p | 124.9 MHz | 1901 px |
| 59.80 Hz | 1080p | 148.0 MHz | 1605 px |
| 66.00 Hz | 1080p | 163.3 MHz | 1454 px |
| 70.00 Hz | 1024p | 126.0 MHz | 1447 px |
| 75.00 Hz | 1024p | 135.0 MHz | 1350 px |
| 84.68 Hz | 1024p | 152.4 MHz | 1196 px |

**Both bench sources are well clear of it** -- the RISC PC at 50.475 Hz and the
Wii's 480p at 59.8 Hz -- so the everyday state is unchanged. What moves is the
70 Hz and faster half of the monitor definition's DMT set, and at 70 Hz the
fallback gives a WIDER raster than the mode it replaces, 1447 against 1371.

## Still open

**Whether the raster WIDTH binds as well is not settled, and it decides whether
the fallback is far enough.** 1024p at 84.68 Hz is 1196 px against the 1134 that
was unusable -- 62 px wider, where the nearest clean point is 1600. If the width
binds anywhere above 1196 the fallback clears the encoder and not the width, and
the reported symptom stays.

The check is one source mode: 630 lines at 84.68 Hz with the preference at 1080p.
Landing on 1024p and clean settles it; landing on 1024p and still smeared says a
width floor is needed beside this one, and 720p's 1700 px is the known-clean
point to place it against.

**A width floor must not be guessed from that one point.** The old entry's own
warning holds: the floor is not the mode's `activePx`, since 1080p solves 1600 at
60 Hz and 1916 at 50 Hz and both are clean, so a floor at 1920 would refuse every
60 Hz mode on the bench.
