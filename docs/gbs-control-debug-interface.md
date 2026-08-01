# gbs-control debug and control surface

What is reachable on a running GBSC Pro.

**The debug surface is a build option.** Compile with `GBS_DEBUG=1` (see
[build/README.md](../build/README.md)). Without it the debug endpoints are not
compiled in and the trace output is absent.

The unit answers on `gbscontrol.local` (or its DHCP address) — `<scaler>` below.

## HTTP

Single-character console commands, issued as query strings:

| Endpoint | Meaning |
|----------|---------|
| `GET /sc?<char>` | system command |
| `GET /uc?<char>` | user command |

Useful ones:

| Command | Effect |
|---------|--------|
| `/sc?k` | `bypassModeSwitch_RGBHV` — VGA passthrough |
| `/sc?K` | HD bypass, and **saves** the `OutputBypass` preference |
| `/sc?i` | toggle the periodic info print (goes to **hardware serial**, not the WebSocket) |
| `/sc?,` | `printVideoTimings()` — live display timings to the web console (needs `GBS_DEBUG=1`) |
| `/uc?1` | **reset to defaults and reboot — wipes the config** |
| `/uc?f` `/uc?g` `/uc?p` `/uc?s` | scaling presets: 1280×960, 1280×720, 1280×1024, 1920×1080 |

### Registers

| Endpoint | Meaning |
|----------|---------|
| `GET /getreg?s=<seg>&r=<reg>` | read one TV5725 register byte |
| `GET /setreg?s=<seg>&r=<reg>&v=<val>` | write one, and report what it was |

Segment `0`–`5`, register and value `00`–`ff`. Every field is hex, with or
without an `0x` prefix — never decimal, so `r=11` means the same register the
serial console's `g5 11` means. Bad or missing fields get a `400`.

```
$ curl 'http://gbscontrol.local/getreg?s=5&r=0x11'
{"segment":5,"register":"0x11","value":"0x3c"}
$ curl 'http://gbscontrol.local/setreg?s=5&r=0x11&v=0x40'
{"segment":5,"register":"0x11","was":"0x3c","value":"0x40"}
```

`setreg` reads the register back after writing, so `value` is what the chip
actually holds: unchanged means the bits are read-only, or the segment is wrong.
`was` is the value to write back to undo the poke.

These do what the serial console's `g` and `s` have always done — but those read
their arguments a character at a time straight from the UART, so they cannot be
driven over the network at all.

Both write straight to the chip and neither is persisted: a reboot, or any
preset load, undoes whatever you poked.

### Saving what you poked

`/uc?4` writes the live registers to SPIFFS as the custom preset for the current
video mode and slot, and switches the unit's preset preference to
`OutputCustomized` — so from then on it boots into that file.

That is the wedge: whatever the registers hold at that moment is what the unit
comes up in, every boot, and if it comes up without a picture the web UI is the
thing you would have used to fix it. `/uc?1` (wipe) or a reflash is the way out.

The save path refuses the cases it can recognise rather than persisting them: a
video mode with no preset file, a register readback that is all `0x00` or all
`0xff` (the chip did not answer), and a file that does not write in full. It
writes to `<preset>~` and swaps that into place only once complete, so a failure
leaves the previous preset untouched — and the preference is only switched to
`OutputCustomized` if a preset actually landed. With `GBS_DEBUG=1` the
reason for a refusal goes to the web console.

None of that judges whether the timings are *good*, only whether they are a
preset at all. Saving a bad-but-well-formed preset still wedges the unit.

### Footguns

Two of the `/sc` commands bite:

- `/sc?K` persists the bypass preference, so a wrong guess survives reboot and
  the unit comes back in the same unusable state. Prefer `/sc?k`.
- `/uc?1` is the big hammer. It clears a wedged config, and it also destroys a
  working setup.

## Live status WebSocket

`ws://<scaler>:81/`, subprotocol `"arduino"`. Two kinds of frame:

- `#<preset><slot><opt0><opt1><opt2>` — status. Preset `8` is a bypass preset.
- anything else — terminal/log text, i.e. whatever `SerialM` broadcasts.

`websocket-client` (in the dev shell) is enough to read it. The server accepts
five clients (`WEBSOCKETS_SERVER_CLIENT_MAX`); each one costs heap.

## Output that is still absent

Some `SerialM.print(...)` calls are written `; // SerialMprint(...)`, with the
identifier mangled so un-commenting one does not compile; `grep -c SerialMprint`
counts them. Commands that rely on those print nothing even with the switch on,
and the web UI's "developer mode" has dead buttons as a result.

## Timings

`printVideoTimings()` — `/sc?,`, and after every `moveHS`/`moveVS` nudge —
emits, for a scaling preset:

```
HT / scale   : 1716 512      VDS_HSYNC_RST, VDS_HSCALE
HS ST/SP     : 100 200       VDS_HS_ST, VDS_HS_SP
HB ST/SP(d)  : ...           display blanking
HB ST/SP     : ...           memory blanking
------
VT / scale   : ...           VDS_VSYNC_RST, VDS_VSCALE
VS ST/SP     : ...
VB ST/SP(d)  : ...
VB ST/SP     : ...
IF VB ST/SP  : ...           deinterlacer V offset
CsVT         : ...           measured input VTotal
CsVS_ST/SP   : ...
```

In HD bypass (preset ID ≥ 0x20) the `HD_*`/`SP_CS_*` registers are dumped
instead, ending with the same `CsVT` / `CsVS_ST/SP` pair.

## Serial
`/dev/ttyUSB0` at 115200 via the ESP's CH340 (`1a86:7523`). **Opening the port
resets the ESP.** Independent of the WebSocket:

- the sync watcher prints `RGBHV limit no sync` and friends;
- `printInfo()` (toggled by `i`) is genuinely active — h/v period, HS/VS active
  and polarity, HTotal/VTotal, video mode, `noSyncCounter`.
