# The ADC's analog filter corner is above Nyquist at every clock this board runs

`ADC_FLTR` (`s5_03[5:4]`) selects the anti-alias low-pass in front of the ADC
sampler. RD-5725-1.1 documents three values -- 00 is 150 MHz, 01 is 110 MHz, 10
is 70 MHz -- and the firmware carries a fourth, 11 for 40 MHz, which every
shipped scaling preset used.

A low-pass in front of a sampler earns its keep only where its corner is **below
Nyquist**, which is half the sample clock. Above that it removes nothing the
sampler would have folded back, and its only remaining effect is how much
high-frequency content and noise reaches the converter.

## The arithmetic rules it out before the measurement does

The sample clock is `PLLAD_MD x lineRate`, so both bench sources land far below
even the narrowest corner:

| source | divider | line rate | sample clock | Nyquist | narrowest corner |
|---|---|---|---|---|---|
| RISC PC 320x256@50 | 2208 | 15625 Hz | 34.5 MHz | 17.3 MHz | 40 MHz |
| RISC PC 800x600@60 | 2039 | 37879 Hz | 77.2 MHz | 38.6 MHz | 40 MHz |
| Wii 480p, YPbPr | 1096 | 31395 Hz | 34.4 MHz | 17.2 MHz | 40 MHz |

Even at the fastest, the narrowest corner sits above Nyquist. There is no
setting of this field that anti-aliases anything on this board.

## Measured, at both clocks, with a control

`PATTERN PM5544` and a 150 x 70 pixel band over the finest grating bars, scored
as mean absolute difference between horizontally adjacent pixels, normalised by
the band's own dynamic range so the camera's auto-exposure does not read as a
sharpness change.

| corner | 320x256@50 | 800x600@60 |
|---|---|---|
| 150 MHz | 15.45% | 10.22% |
| 110 MHz | 15.06% | 10.24% |
| 70 MHz | 15.26% | 9.95% |
| 40 MHz | 15.37% | 10.59% |
| **150 MHz, shot again** | -- | **9.96%** |

**The control's own repeat spans the whole spread**, and the order is not
monotonic in corner frequency either way. Repeated on the component path at
480p for colour rather than sharpness: R 139.7 against 140.1 with the control
again bracketing the difference, so a wider corner costs no chroma either.

## What follows

`Adc::init()` opens it widest, once, for every source. The widest corner cannot
remove detail that is there, and the sweep says none of the four costs anything
measurable, so there is no rule to derive and no threshold to invent.

`HdBypass` still writes its own corner per standard on the bypass path. That is
a second owner and it goes with the bypass entry points.

**The trap the control caught.** The first shot after any capture reads several
grey levels off the rest -- the camera's exposure is still settling -- so a
sweep scored without repeating one state attributes that to whichever setting
happened to be first. Here it would have shown 150 MHz as measurably worse.
