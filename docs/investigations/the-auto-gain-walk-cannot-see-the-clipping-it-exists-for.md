# The auto-gain walk could not see the clipping it exists for

Both pass-through sources on this bench reached the user with peak white
railed flat, and stayed that way: the walk was stepping, but at a rate of a few
register counts an hour against the forty or so it had to cover.

## The seeds are hot by design, and that is not the fault

`ColourSpace::ComponentGain` 0x33 and `RgbGain` 0x7B are deliberately hot
enough that a standard source clips, because the walk only ever backs off --
a seed that did not clip would strand a source hotter than it with no way
back. Raising them trades a slow correction for an impossible one, so the seed
is not where this is fixed.

## What the picture showed

The RISC PC desktop at 1280x720 in pass-through, PM5544, read off the USB
capture. `white` is the card's white surround, `s0..s5` its greyscale
staircase:

| gain | s0 | s1 | s2 | s3 | s4 | s5 | white |
|---|---|---|---|---|---|---|---|
| **123** (the seed) | 0.0 | 38.3 | 85.7 | 115.5 | 155.3 | 219.6 | **238.9** |
| 140 | 0.0 | 35.1 | 80.1 | 108.3 | 146.0 | 207.1 | **238.9** |
| 160 | 0.0 | 32.3 | 74.4 | 100.9 | 136.3 | 193.9 | **238.9** |
| 180 | 0.0 | 29.6 | 68.9 | 94.2 | 128.5 | 181.7 | 237.3 |
| 200 | 0.0 | 26.7 | 64.6 | 88.4 | 120.2 | 171.5 | 225.7 |
| 240 | 0.0 | 23.0 | 56.5 | 78.0 | 107.3 | 153.1 | 202.4 |

**White is pinned at 238.9 across three gains while every grey step below it
scales normally**, which is the top of the range clipped flat. It comes off the
rail between 180 and 200.

**The scaling path does not show this**, which is why a sweep taken there reads
as nothing wrong: the same source scaled moves white only 198 to 223 across
90..230 and never rails. A gain measurement is of a PATH, not of an input.

## What the walk was reading

`/gainpeaks` prints `AutoGain::peaks()`, which nothing could see before -- the
levels live on the test bus, so no register dump reaches them. On the state
above, at the seed:

```
1020/1020/1020  over 1/3/3    7 of 72
1020/1020/1020  over 1/1/4    6 of 72
1020/1020/1020  over 5/2/2    9 of 72
```

and at gain 240, correctly gained:

```
 952/944/1020   over 0/0/1    1 of 72
1016/940/972    over 1/0/0    1 of 72
 900/944/956    over 0/0/0    0 of 72
```

**All three channels are pegged at 1020 while the count straddles the
threshold of 8.** Only the samples that happen to land on peak white count
against the walk, and that is a far smaller share of 72 reads than the white
area of the picture suggests -- so a hard-clipping source counts 6 to 9 where
`OverToStep` wanted 8.

Stepping was therefore a coin flip, and `run()` requires the reading
*confirmed* on two consecutive passes, so the chance of moving on any pair was
about a quarter. The walk crawled 123 to 139 in the time it took to measure
this, and on `ypbpr` 51 to 67 over forty minutes.

## The threshold is what the measurement sets

`OverToStep` 8 -> 4: above the 0 to 1 a correctly gained source counts, which
is the floor of all-bits-set reads the constant has to clear, and below the 6
to 9 a clipping one counts.

Measured after, from the seed on each input, with nothing else changed:

| | seed | before | after | wanted |
|---|---|---|---|---|
| RISC PC 720p pass-through | 123 | 139 after tens of minutes | **207 within the boot** | 180..200 |
| Wii 480p `ypbpr` | 51 | 67 after forty minutes | **87 in under 40 s** | ~90 |

White on the RISC PC moves 238.9 -> 222.2, off the rail. On the Wii the disc
icon in the top-left cell -- a pale grey graphic on white, and the thing the
clipping destroyed -- goes from an indistinct oval to a disc with its hub ring,
centre hole and reflection all legible; its contrast against the surrounding
white measures 40.8 at the old resting point against 62.2 at 90.

**The convergence rate was the whole of it**: both inputs now reach a correct
gain faster than a viewer can see, from the same hot seeds, with the walk's
one-directional design untouched.
