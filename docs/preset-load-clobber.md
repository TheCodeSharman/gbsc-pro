# Preset loads leave things behind

A preset load writes bulk register tables and then patches individual bits back.
When a load happens on a path that skips the patch-up, the bulk value stands —
and the symptom appears somewhere unrelated to anything you were working on.

## The clearest case: blue goes missing

Symptom: the picture is **yellow-tinted**. Yellow is red plus green, so blue is
gone. Every register a health check looks at is fine — sync locked, `HPERIOD_IF`
correct, geometry sane.

```sh
curl 'http://<ip>/getregs?s=0'      # inspect s0 0x44 and 0x45
```

```
s0 0x44 = 0x25   DAC_RGBS_R0ENZ (bit 2) = 1     red   enabled
                 DAC_RGBS_G0ENZ (bit 5) = 1     green enabled
s0 0x45 = 0x10   DAC_RGBS_B0ENZ (bit 0) = 0     blue  DISABLED   <-- here
```

The odd-one-out bit matches the odd-one-out colour. Fix, confirmed on the bench:

```sh
curl 'http://<ip>/setreg?s=0&r=0x45&v=0x11'     # restores blue immediately
```

## Why it happens

`DAC_RGBS_B0ENZ` is **never written to 0 anywhere in the firmware.** The only
explicit writes are `write(1)`, at `gbs-control.ino:5360` and `:5686`. But a UART
write trace of one live session shows 254 writes to `s0 0x45`:

| value | `B0ENZ` | count |
|---|---|---|
| `0x11`, `0x01`, `0x09` | 1 — blue on | 230 |
| `0x10`, `0x16`, `0x12` | 0 — blue off | 24 |

So the disabling writes come from **bulk table loads**, not from any line of code
that means to turn blue off. Normally a load is followed by the explicit
re-enable and nobody notices. Whichever write lands last wins, so a load that
does not reach `:5360`/`:5686` leaves the DAC off.

## The general shape

The same pattern is the leading explanation for `HPERIOD_IF` failures, which
occur only after a **deep sync loss** — never after a clean mode change (0 of 32
transitions) — and a deep sync loss is exactly what makes the firmware re-detect
and reload a preset. See
[tv5725-chip.md](tv5725-chip.md#what-triggers-it-a-deep-sync-loss-not-a-mode-change).

Two consequences worth carrying:

- **Freezing hides it.** `/freeze` stops preset loads, so a frozen unit cannot
  reproduce any of this. Every frozen sweep and every steady-state register
  replay has come back clean, and that is a property of the experiment, not
  evidence about the fault.
- **Register-level health checks do not catch it.** Sync lock, `HPERIOD_IF` and
  geometry can all read correct while a DAC channel is off or a measurement is
  stable-but-wrong. Look at the picture, and compare readings against the
  *expected* value for the mode rather than against rails.

## Restoring a snapshot has the same failure mode

`dump_registers.py --restore` writes registers, but the firmware's own state
lives in ESP RAM — `rto->videoStandardInput` and friends — and no register write
touches it. Restoring a bypass snapshot onto a running unit produced a state the
firmware could not re-detect its way out of: it sat in `PresetBypassRGBHV` through
every sub-535-line mode, where branch `:6642` would normally have escaped it,
because the RAM state matched no branch.

**Reboot after restoring**, or accept that the two halves of the state machine
disagree. And restore **segment 2 as well as 1, 3, 4, 5** — segment 2 holds the
deinterlacer and MADPT block, and an IF/VDS restored around a stale segment 2 is
worse than what you started with.
