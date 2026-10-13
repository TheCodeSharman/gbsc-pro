# The clamp cannot move into the sync pulse

`SP_CS_CLP_ST`/`SP` hold the window in which the black level is sampled. On the
bench they sit at 26..30 of a 2506-sample line whose hsync pulse measures 177
samples, which reads as a window a fifth of the way into the pulse — and that
reading is what makes moving it look attractive. It is wrong, and the picture
says so in one shot.

## The proposal

Place the window at the END of the pulse instead of near its start. The argument
is sound on its face: a source whose sync arrives on a pin of its own carries no
sync on the colour channels, so they sit at blanking for the whole pulse; and a
black level sampled as late as possible has least time to droop before the
picture it sets. It also removes two magic fractions, because the placement
follows the measured pulse rather than a fraction of the line.

Implemented as `stop = pulse - guard`, `start = stop - width`, with sync on green
kept in the back porch because there the pulse is on the clamped channel.

## What the picture did

Measured on the RiscPC at 320x256@50 on `vga`, separate sync, PM5544, the same
camera position and framing either side, a central grey patch of the card
averaged to one pixel:

| clamp window | R | G | B |
|---|---|---|---|
| 26..30 | 210 | 198 | 224 |
| 165..169 | **64** | 203 | **118** |
| 26..30, restored | 210 | 199 | 225 |

Red and blue are crushed and green is untouched. Repeated on a second frame
taken separately: identical to the unit. The whole picture reads as a heavy
green cast.

## What that says

The window did not move later inside the pulse. It moved into content.

The most likely reading is that the count is taken from the pulse's TRAILING
edge rather than its leading one, which makes 26..30 the back porch — the
classic and correct placement — and 165..169 a window well into active video.
Red and blue are then clamped to whatever the picture holds there, and green
survives because the level it lands on happens to be near its own.

**Both halves of that reading are since confirmed, by walking the register
against the emitted frame.** The count is the chip's 27 MHz reference, and count
0 sits about 12 ADC samples after the pulse ends, so the window this page
restored was in the back porch and the one it moved to was in content. The
colour measurement reached the right answer without the units, and the register
was never a fifth of the way into the pulse.
[`the-clamp-had-two-owners-and-two-units.md`](the-clamp-had-two-owners-and-two-units.md).

## Do not reinstate

Two things follow, and the second is the one that costs a session:

- **The existing placement is not "a fifth of the way into the pulse".** Reading
  it that way is what makes it look like a defect to fix.
- **A clamp change cannot be judged from registers.** Every register read
  correct throughout, the source stayed locked, the geometry was unchanged, and
  the only instrument that noticed was a photograph of a grey patch.

The concern the proposal came from is still live: on a mode with no front porch,
the border can reach a window placed too far from the sync edge. The measurement
it asked for — where the window sits relative to the trailing edge — exists now
for one source: on the Wii at 480p the usable range is 0..66 and the picture
starts at 67, against a placed window of 19. What is still missing is the
counter's origin on a second source, which is what would turn that into a bound
rather than one reading.
