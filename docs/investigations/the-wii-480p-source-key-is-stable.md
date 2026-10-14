# The Wii's 480p source key is stable, and the spread was debris

`/framing.txt` held six entries for one source -- a Wii at 480p on `ypbpr` --
which reads as a `SourceKey` that moves between acquisitions and a stored
framing that is therefore recalled only by luck. **Measured, the key does not
move.** The six entries were accumulated while the 1080p non-acquisition was
being debugged, and every one of them was written by a build or a state that no
longer occurs.

## What was measured

Ten acquisitions, each a real input change away and back -- `/input?src=vga`,
wait for its key, `/input?src=ypbpr`, wait for its key -- driven off the
console's own `input selected:` and `source key:` events rather than off
`/geometry`, which reports the previous state until the queued selection reaches
`loop()`.

| | reading |
|---|---|
| lines | 524, ten times |
| field rate | 59.94 Hz, ten times |
| vsync polarity | Undetermined, ten times |
| sync width | 734 nine times, 726 once |
| raw duty | `89 pulse / 1212 divider` nine times, `88 / 1212` once |

The whole spread is **one ADC sample**, 8 ten-thousandths against the 20 that
`SyncWidthIdentity` allows, so the two strings are one key:
`SourceKey::operator==` compares the sync width inside that tolerance and
`FramingTable::remember()` finds by the same comparison, updating the entry in
place rather than adding one.

Each acquisition completed 2.3 to 2.6 s after the selection landed.

## What the six entries were

```
521@59.94/1065?? = 1361 8639  613 9330 17778
524@59.94/685??  = 1418 8392  686 9143 17778
524@59.94/733??  = 1604 7967 1010 8667 17778   <- the live key
524@60.00/1157-- = 1799 8000  667 9143 13333
521@59.94/1100?? = 1361 8639  613 9330 13333
524@60.00/1032-- = 1702 8000  648 9143 13333
```

Three signatures, none of which the current build produces:

- **A count of 521** against the source's 524, and **a duty of 10.3 to 11.6%**
  against 7.3%. That is the pair `SourceMeasurement::takeDuty()` now refuses:
  it takes a reading only when the divider is latched and the ratio is a pulse,
  and keeps the previous one otherwise. The guard arrived in `61d2eb466`.
- **A rate of 60.00** against 59.94, which is the rate read before it is
  confirmed.
- **A vsync polarity of Negative** where sync on green can only report
  Undetermined. `SourceMeasurement::polarityOf()` returns Undetermined when
  `SyncMeasurement::isCsync()`, so a key carrying `-` was built before the sync
  type was established.

`524@59.94/685??` is the odd one: 6.85% is a plausible pulse and passes every
guard, but it is six ADC samples from the 89 this source measures at any
divider, the duty being a ratio. It is unreachable from the live key and was
dropped with the rest.

## What this does not say

**The store still accumulates an entry per distinct key and nothing prunes an
unreachable one.** A key written by a build or a state that no longer occurs
stays until the file is edited by hand, and the only per-source forget is
`/sc?B`. Six debris entries out of thirteen is what that costs after one
debugging campaign, and the reading that the identity was unstable followed
directly from it.

So a framing store read as evidence about the engine is evidence about every
build that has written it, not about the one running.
