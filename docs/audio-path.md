# The audio path

The board embeds analog stereo audio into the HDMI stream without any help from
the ESP beyond volume. **Confirmed on the bench 2026-08-23**: a line-level stereo
source into the 3.5 mm jack arrives at the sink's speakers, both channels clean.
**No external audio embedder is needed.**

## The chain

| Stage | Where |
|---|---|
| 3.5 mm stereo jack (SJ1-3533NG), plus audio pins on the RGBS connector | sheets 12 and 3, nets `ALIN_IN`/`ARIN_IN` |
| PT2257 stereo volume controller, U22, I²C `0x44` on the ESP bus via `MSDA`/`MSCLK` | sheet 9 |
| `OUT_L`/`OUT_R` into a 10 K:10 K divider (`R50`/`R51`, `R52`/`R53`) and 3.3 µF coupling caps | sheet 4 |
| MS9288A `ALIN` (pin 30) and `ARIN` (pin 33), dual 24-bit ADC at 48 kHz | sheet 4 |

Sheet 4 joins the divider to the pins by net label, not by a drawn wire — the
labelled stubs leave U6 at the top left, among the VGA sync labels.

The encoder embeds the audio with no configuration from the ESP; it has none
available. See CLAUDE.md on why the MS9288A is unreachable.

## What the firmware does

`OSD_TV/PT2257.h` is the driver. `PT_2257(x)` sets **x dB of attenuation**,
splitting it into the PT2257's two command bytes — `0xE0 | tens` in 10 dB steps
and `0xD0 | units` in 1 dB steps, valid to 79 dB total. `PT_MUTE(0x78)` clears
mute (`0111100M`, M=0).

`Audio::LineVolume` owns the mapping from the setting to decibels, and it is the
only thing that does -- the sketch's loop, the overlay's two step keys and
Settings' bound all ask it. **The setting IS the attenuation in decibels**, so 0
asks the part for 0 dB and `Maximum` is 50. The overlay shows `Maximum -
setting`, so a larger number on screen is louder, and the volume-up key
DECREMENTS the setting.

`setup()` unmutes and writes `PT_2257(70)`. `loop()` then writes
`PT_2257(LineVolume::attenuationDb(Volume))` every 400 ms, where `Volume` comes
from the OSD's "Line input volume" page, the IR remote's volume keys, or the
`volume` setting.

`LineVolume::Default` is 12, which is where a unit with nothing saved starts.
**It is a precaution rather than a measurement**: the encoder's full-scale input
level is unknown, so nothing says whether a hot source clips at 0 dB, and the
default leaves the same 12 dB every unit carried when the floor was hardcoded.
Turning it down costs nothing; a source quieter than the chain expects goes to
0.

## Level budget

**Only the divider is fixed.** At setting 0 the PT2257 attenuates nothing, so
the loudest the encoder can see is the divider's 6 dB below the source — about
500 mVrms from a 1 Vrms line output. At the default of 12 it is 18 dB down,
about 125 mVrms, which is where every unit sits until the setting is moved and
is enough that a line-level source needs most of a television's volume range.

`MS9288A-Datasheet-Rev-B0.pdf` gives the audio ADC's resolution, channel count
and supply current and **no full-scale input level**, so the headroom cannot be
calculated and the default's 12 dB is not derived from one.

**0 dB does not clip the RiscPC's headphone output, measured 2026-10-11** --
heard at the sink with the setting at 0, both channels, no clipping. That is one
source, and a headphone amplifier at full volume is among the hotter things this
jack will see, so it bounds the risk without retiring it: the default keeps the
12 dB because nothing states where the ADC actually runs out.

**The PT2257 only attenuates**, so a source quieter than the chain expects wants
gain ahead of the jack once the setting is already at 0. The 6 dB divider is
hardware and no setting recovers it.

**A headphone output is not the weak end, which is worth stating because it
reads like one.** Consumer line level is nominally ~316 mVrms and a headphone
amplifier driving this input — the PT2257's own, essentially unloaded — normally
swings more than that, so a quiet result from one is the source's own volume
control or this budget rather than a level mismatch at the jack.

## Where to look when it is silent

**Check the source first.** This path is confirmed end to end, so silence is more
likely to be the thing feeding the jack than anything on this board.

The two firmware causes:

- **`setup()` leaves the part at −70 dB**, 9 dB above the PT2257's floor. Only
  the 400 ms write in `loop()` moves it, so a firmware stalled below the loop
  gives a picture with no sound — the same signature as the HTTP-answers-while-
  `loop()`-is-stalled case in CLAUDE.md.
- **`Volume` is bounded at 50 like everything else in the settings file**, so a
  value out of range reads as 0 rather than reaching `PT_2257()`. The positional
  file read it with no clamp at all: a readable but short file returned −1 from
  `f.read()`, giving `Volume` 207 and an attenuation argument of 219, where the
  10 dB command byte goes out of range and the register keeps whatever `setup()`
  left in it. Read `volume` in `/preferences.txt` before diagnosing quiet audio.
  `docs/preferences-file.md`.
