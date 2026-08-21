# The audio path

The board embeds analog stereo audio into the HDMI stream without any help from
the ESP beyond volume. Nothing here is bench-verified: it is a trace of the
schematic and the firmware, and no sound has been measured through it.

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

`setup()` unmutes and writes `PT_2257(70)`. `loop()` then writes
`PT_2257(Volume + 12)` every 400 ms, where `Volume` is 0..50 from the OSD's
"Line input volume" page, the IR remote's volume keys, or `/preferencesv2.txt`.
The OSD displays `50 - Volume`, so a larger number on screen is louder.

## Level budget

The quietest link in the chain is fixed. `Volume` 0 still asks for 12 dB of
attenuation, and the divider costs 6 dB, so **the loudest the encoder can ever
see is 18 dB below the source** — about 125 mVrms from a 1 Vrms line output.
`MS9288A-Datasheet-Rev-B0.pdf` gives the audio ADC's resolution, channel count
and supply current and no full-scale input level, so whether that is enough is unknown until it is heard. A source that
turns out to be too quiet wants gain ahead of the jack; the PT2257 only
attenuates.

## Two ways to get silence

- **`setup()` leaves the part at −70 dB**, 9 dB above the PT2257's floor. Only
  the 400 ms write in `loop()` moves it, so a firmware stalled below the loop
  gives a picture with no sound — the same signature as the HTTP-answers-while-
  `loop()`-is-stalled case in CLAUDE.md.
- **`Volume` is read from preferences without a clamp**, unlike the fields
  around it. A readable but short file returns −1 from `f.read()`, giving
  `Volume` 207 and an attenuation argument of 219: the 10 dB command byte goes
  out of range and the register keeps whatever `setup()` left in it. Read byte 0
  of `/preferencesv2.txt` before diagnosing quiet audio.
