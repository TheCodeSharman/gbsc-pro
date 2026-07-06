# GBSC Pro AV-module flashing protocol

Reverse-engineered from `GBSC_PRO_Programmer.exe` ("GBSC PRO Programmer V0.2"),
the Windows tool RetroScaler ships in the GBSC Pro firmware zip
(`github.com/RetroScaler/gbsc-pro`). The exe is a .NET/WinForms app built on the
open-source **YModemWin** library; it was decompiled with ILSpy and the transport
logic transcribed here so a native Linux flasher can be maintained instead of
depending on the Windows binary.

The **AV module** is the HDSC **HC32** MCU that carries the LM1881 composite/S-video
sync path (`midwares/hc32` in the repo source). Its resident bootloader speaks a
lightly-customised **YMODEM-128** over a USB-CDC virtual serial port. This is the
firmware that actually changed in v1.3 (the "1X crop" / 525p-625p autoswitch fix);
the ESP8266 half is flashed separately with esptool.

## 1. Device identification

In bootloader mode the AV module enumerates as a USB CDC-ACM device:

| Field | Value |
|-------|-------|
| USB VID | `0x2E88` |
| USB PID | `0x4603` |

The Windows tool finds it via WMI (`SELECT * FROM WIN32_SerialPort`, matching the
PNPDeviceID VID/PID substrings). On Linux it appears as `/dev/ttyACM*`; match the
same VID:PID via `pyserial`'s `list_ports`.

## 2. Serial parameters

| Setting | Value |
|---------|-------|
| Baud | 115200 |
| Data bits | 8 |
| Parity | none |
| Stop bits | 1 |
| DTR | asserted (true) |
| RTS | asserted (true) |
| Read timeout | ~1000 ms |

## 3. Entering upgrade mode (the "custom header")

On open, the bootloader is already printing a banner. The host reads whatever is
buffered (`ReadExisting`) and branches:

- **If the banner contains `"HCMGBoot."`** — the bootloader menu is up:
  1. write ASCII `"U"`, wait ~100 ms, drain input
  2. write ASCII `"1"`, wait ~100 ms, drain input
  3. the response must contain `"Enter download"` → now in YMODEM receive mode
- **Else if the banner contains `"Enter  1"`** (note: two spaces) — already at the
  download prompt; proceed straight to YMODEM.
- **Otherwise** — abort: *"Can not enter the Upgrade mode. Please check the Connection"*.

## 4. YMODEM transfer

Standard YMODEM batch send, **128-byte blocks only** (SOH); the tool never uses
1024-byte STX blocks. CRC-16 mode throughout.

### Packet layout (133 bytes)

```
+------+--------+-----------+---------------------+--------+--------+
| SOH  | seq    | 255 - seq | 128 data bytes      | CRC hi | CRC lo |
| 0x01 | 1 byte | 1 byte    |                     |        |        |
+------+--------+-----------+---------------------+--------+--------+
```

- `seq` starts at 0 (the block-0 header packet), wraps modulo 256.
- Short final data block is padded with `0x1A` (SUB / Ctrl-Z) to 128 bytes.

### CRC-16

CRC-16/CCITT (a.k.a. CRC-16/XMODEM): polynomial `0x1021`, **initial value
`0x0000`**, no input/output reflection, no final XOR. Transmitted **big-endian**
(high byte first). Check value for `"123456789"` = `0x31C3`.

### Block 0 (header)

Data field = the **filename only**, encoded gb2312 (ASCII for normal names), NUL
terminated, zero-padded to 128 bytes. Spaces in the filename are replaced with `_`.
Note the tool *computes* a size/mtime string but **discards it** — unlike canonical
YMODEM, block 0 carries no `<size>` field. The HC32 bootloader does not need it.

### Full send sequence

1. Wait for `'C'` (0x43) from the receiver (poll `ReadByte`, sleep 30 ms between tries).
2. `DiscardInBuffer`.
3. Send **block 0** (seq 0, filename). Expect `ACK` (0x06).
4. Read one byte, expect `'C'` (0x43) — receiver requests first data block.
5. For each 128-byte file chunk:
   - seq++ (wraps at 256), pad short chunk with 0x1A, CRC, send packet.
   - Response: `ACK` (0x06) → next; `NAK` (0x15) → resend same block; `CAN` (0x18)
     → abort; anything else → error.
6. Send `EOT` (0x04). Response must be `ACK` (0x06) **or** `NAK` (0x15) — the tool
   accepts either and proceeds. Then sleep ~2 s.
7. **Closing block** (last file only): wait for `'C'` (0x43), then send a block 0
   whose 128-byte data field is all zeros (the YMODEM end-of-batch null header).
   Expect `ACK` (0x06). Done.

### Control byte reference

| Byte | Meaning |
|------|---------|
| 0x01 | SOH (128-byte packet) |
| 0x04 | EOT (end of transmission) |
| 0x06 | ACK |
| 0x15 | NAK |
| 0x18 | CAN (cancel; sent 8× to abort) |
| 0x43 | 'C' — receiver ready / CRC mode |

## 5. Reading firmware back / current version (verified against device source)

Cross-checked against the published device source in `RetroScaler/gbsc-pro`
(`GBSC-Pro-Source code/usart_uart_dma -（IapApp）`, an HC32**F460**):

- **The flash is effectively write-only via this path.** The vendor exe *contains* a
  `YModemReceiver` (receive/save) class, but it is **dead code** — never instantiated
  and `StartReceiving()` is never called; only the download (`U`→`1`) path is wired up.
- **The bootloader is closed-source.** The `HCMGBoot.` banner and the `U`/`1`/
  `Enter download` menu strings appear **nowhere** in the published source — only the
  IAP *application* (the video-processing app that runs after boot) is open. It has
  since been disassembled from an SWD dump, which settles the question: **there is no
  Upload command** (§7).
- **No firmware version is exposed anywhere in software.** There is no version constant
  in the IAP app, no version handshake between the ESP and the HC32, and the gbs-control
  web UI reports none. The only possible software-visible version is whatever the closed
  bootloader prints in its banner — see `--probe` (read-only; sends nothing).

Practical consequence: over the serial port we cannot back up the installed image or
read its version number. The mitigations are to keep the previous official AV bin
(`firmware/GBSC_PRO_AV_MODULE_v1.2.3.bin`) as a rollback target — a bootloader-mediated
flash always leaves the bootloader intact and re-flashable — and to use SWD (§7) when a
readback is genuinely needed.

## 7. The bootloader, disassembled (and the SWD readback path)

Recovered by dumping the MCU's flash over SWD and disassembling it, after the serial
protocol proved to have no readback. This is the authoritative answer to §5's open
question, and the reason `gbsc_pro_flash.py` offers no `--backup`.

### Command set

Dispatch table at `0x454c`. **Three commands exist; none of them is upload.**

| Key | Target | Behaviour |
|-----|--------|-----------|
| `U` | `bl 0x1928` | print device info — the `016MHP` string + a 16-byte chip-UID blob |
| `1` | `bl 0x3e58` | print `"Enter download mode"`, then YMODEM receive + EFM flash program (near the `fileName:` / `fileSize:` / `Download completed!` strings) |
| `2` | `bl 0x215c` | **jump to the application** (prints `"Jump to app failed"` on failure) |

⚠️ `2` is *not* upload, which is the trap the ST/HC32 `iap_ymodem_boot` lineage sets:
its classic menu is `1 Download / 2 Upload / 3 Execute`, so `2` looks like a readback
and is actually "run the app". Sending it boots the installed firmware, which takes
over USB (serial writes then time out) and lights the LED solid red. **Nothing is
erased** — power-cycle back into the bootloader.

### SWD access and flash map

The AV PCB exposes a 4-pin SWD header **J19** (1 = DVDDIO_3.3 V, 2 = SWDIO, 3 = SWDCLK,
4 = GND; KiCad schematic `GBSC-AV-IR-*.pdf` p.12, MCU U21). An ST-Link V2 on
SWDIO/SWDCLK/GND is enough — leave the 3.3 V pin alone, the board self-powers from
USB-C. Driven with `pyocd commander -t cortex_m`; the flash is **not** readout-protected.
CPUID `0x410FC241` = Cortex-M4, consistent with the HC32F460.

`savemem 0x0 0x80000` dumps all 512 KB:

| Range | Contents |
|-------|----------|
| `0x00000`–`0x06fff` | bootloader (~28 KB) |
| `0x10000` | **application base** — where a flashed `.bin` lands |
| `0x70000`, `0x7c000` | config blobs |
| elsewhere | `0xff` (erased) |

### What this verified

The v1.2.3 application at `0x10000` came back **byte-for-byte identical** to
`GBSC_PRO_AV_MODULE_v1.2.3.bin`, all 38 272 bytes. The bootloader's "Update success" is
therefore real programming, not merely a transfer ACK — this flasher writes the image
correctly. SWD is also the recovery path if the bootloader itself is ever lost.

## 6. Notes / gotchas

- The exe embeds an AES key string `"lyan1989@xyz"` with `Encrypt/Decrypt` helpers,
  but these are **not** applied to the firmware stream — the YMODEM payload is the
  raw `.bin`, sent verbatim. (The key appears to be unused dead code / for some other
  config path.)
- Multi-file batches: intermediate files skip the closing null-header; only the last
  file sends it. For a single `.bin` (the normal case) there is exactly one file and
  it is the last.
- The bootloader window may be time-limited after power-on/reset. If the banner isn't
  seen, power-cycle/reset the AV module and reopen the port promptly.
