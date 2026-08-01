# gbsc-pro-hwtest — regression tests against a running unit

These talk to a real GBSC Pro over the network. They exist because this fork's
firmware changes are all in code paths that only a live unit exercises: the
register endpoints, the timings print, and the preset save path.

```sh
pytest --host=gbscontrol.local        # or the unit's IP
pytest --host=192.168.1.20 -v
GBSC_HOST=gbscontrol.local pytest     # same thing via the environment
```

Without `--host` (or `$GBSC_HOST`) every test skips, so a bare `pytest` at the
repo root is safe with no hardware attached.

**Close the web UI first.** Each WebSocket client costs heap and these tests
hold a connection for the whole session. The server accepts five
(`WEBSOCKETS_SERVER_CLIENT_MAX`), so this is about heap, not a hard limit.

The timings tests need a `GBS_DEBUG=1` build (see
[build/README.md](../../build/README.md)). They *fail* rather than skip when the
console is silent — a silent console is the regression they exist to catch.

## What each test is for

| Test | Guards against |
|---|---|
| `getreg_reads_a_register` | the endpoint disappearing or changing shape |
| `getreg_reads_registers_as_hex_not_decimal` | `r=11` being parsed as decimal 11, i.e. poking `0x0b` instead of `0x11` |
| `malformed_register_requests_are_rejected` | a bad request reaching the chip instead of returning 400 |
| `setreg_reports_the_previous_value` | losing the `was` value that tells you how to undo a poke |
| `setreg_changes_a_register` | writes not landing, or the read-back being faked |
| `sc_comma_prints_timings` | `printVideoTimings()` going silent again |
| `timings_agree_with_getreg` | either feature drifting from the other |
| `no_leftover_temp_presets` | a failed save leaving `<preset>~` behind |
| `preset_save_writes_a_complete_preset` | the save path writing a short or dead preset |

## The destructive one

`preset_save_writes_a_complete_preset` is skipped unless you pass
`--preset-save`. It issues `/uc?4`, which overwrites the stored preset for the
current video mode **and** switches the unit's preset preference to
`OutputCustomized` — so from then on it boots into that file. That is the wedge
path. Take a flash backup first:

```sh
esptool --chip esp8266 --port /dev/ttyUSB0 --baud 460800 \
    read_flash 0x0 0x400000 backup.bin
```

## Notes on flakiness

`/spiffs/dir` calls `delay(1)` inside an async request handler and intermittently
drops the first request after a burst of WebSocket traffic. That is upstream
behaviour in a handler none of these changes touch, so `spiffs_dir()` retries
rather than failing the suite over it.

`timings_agree_with_getreg` brackets its console read with register reads taken
before and after. The firmware rewrites these registers continuously while it is
hunting for sync, so a register that moved across the window proves nothing and
is reported instead of asserted on. If every register is moving, the test skips
and says so — that itself means the unit has no stable sync.

## Which branch the preset test takes

`preset_save_completes_or_refuses_cleanly` asserts a contract, not an outcome:
`/uc?4` must either write a complete 432-value preset, or refuse and leave the
preferences untouched. Which branch runs depends on the unit's current video
mode — mode 15 has no preset file, so it refuses; mode 0 writes
`/preset_unknown.<slot>`. Run with `-s` and it prints which one it exercised.

A successful save switches the unit's preset preference to `OutputCustomized`
(byte 0 of `/preferencesv2.txt` becomes `2`). `/uc?p` puts it back to
`Output1024P` (`4`) without touching the filesystem, which is a cleaner undo
than re-uploading the preferences file.

## sync_monitor.py

A live view of the sync processor, for correlating with a scope on the VGA
input:

```sh
python3 tools/gbsc-pro-hwtest/sync_monitor.py --host 192.168.88.108
python3 tools/gbsc-pro-hwtest/sync_monitor.py --host … --totals
```

It polls `STATUS_16` and prints every change, flagging any instant where H and V
are active together — which is what the firmware's `stable` test requires and
what this unit has never sustained. Ctrl-C prints a summary with the percentage
of samples each sync was active for. Scope answers "is it on the wire"; this
answers "does the scaler see it".
