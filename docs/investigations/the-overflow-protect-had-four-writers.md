# The overflow protect had four writers and no measurable effect

`SP_H_PROTECT` -- RD-5725-1.1's "H count overflow protect", `s5_3E[4]` -- was
written four times on the way through a preset load, by three different owners,
and the one with a host test behind it lost.

| order | writer | value |
|---|---|---|
| 1 | `SyncProcessor::applyForSyncType()` | 1 on csync, 0 on separate sync |
| 2 | `SyncProcessor::prepare()`, inside `if (!rgbhvRoute)` | 1 |
| 3 | `doPostPresetLoadSteps()`, eight statements in | 0 |
| 4 | `doPostPresetLoadSteps()`, a hundred statements later | 0 |

Writer 4 was 0 over 0: nothing between 3 and 4 touches the field. Writer 3 was
the last word, so `applyForSyncType()`'s csync choice -- which
`test_sync_processor.cpp` asserts as "csync coasts around the vertical interval
and **protects the line**" -- never reached the chip on this path. A fact with a
test on one side and the opposite value shipping on the other.

A fifth writer sat in `loop()`, re-deciding from the same predicate:

```cpp
if (Tv5725::SyncProcessor::coastPlaced()) {
    if (sourceHasSerratedSync()) {
        Tv5725::SyncProcessor::setSubCoast(true);
        Tv5725::SyncProcessor::setHsyncOverflowProtect(false);
    }
}
```

`applyForSyncType()` already calls `setSubCoast(serrated)` from the same
`sourceHasSerratedSync()`, so the first of the pair wrote the value that was
already there and the second contradicted the sync type a second time.

## It has no measurable effect

Measured with automation frozen, the bit alternated by hand and nothing else
touched, on both composite-sync modes the bench can produce:

| | 320x256@50 | 640x480@60 |
|---|---|---|
| margins at 0 | 313/254/0/0 | 21/12/0/0 |
| margins at 1 | 313/254/0/0 | 21/12/0/0 |
| spread at 0 / at 1 | 108.6 / 108.6 | 108.0 / 107.9 |
| `STATUS_SYNC_PROC_HTOTAL` | 2200, sd 0.00 either way | 1444, sd 0.00 either way |
| `STATUS_SYNC_PROC_VTOTAL` | 308..309 either way | 522..523 either way |

Four alternations at 640x480@60 and three at 320x256@50, identical to the pixel.
That is what `SyncProcessor`'s header already said -- nothing on the board
measures whether it is helping -- and it is why four writers could disagree for
as long as they liked without anything noticing.

## The settled value is a toggle parity, and that is by design

`SyncRecovery::HsyncOverflowProtect` calls
`SyncProcessor::toggleHsyncOverflowProtect()` while a csync source is being
acquired, so what a settled unit reads is the parity of however many times that
rung fired on the way in. Measured across five composite-sync legs on one build:
0, 0, 0, **1**, 0, with the picture at 1 inside the same spread range as at 0
(106.0 against 105.9..108.7).

So a reading taken after acquisition says nothing about what the setup wrote,
and two readings of it are not a comparison. The field a setup leaves is
`applyForSyncType()`'s; everything after is the ladder's.

## What is left

One writer per phase. `applyForSyncType()` states the value a setup leaves,
`setHsyncOverflowProtect()` is detection's and the ladder's `FullReset` rung's,
and `toggleHsyncOverflowProtect()` is the search. `setSubCoast()` is private to
`SyncProcessor`, since the source's serration is asked in exactly one place.

The net register state is unchanged on every bench path -- eleven
sync-processor fields read identically across `vga` separate sync, `vga`
composite sync, `ypbpr`, a boot on each input, and an input round trip -- which
is the point: removing three writers of an inert field changes nothing, and that
is why it was safe to leave broken for so long.
