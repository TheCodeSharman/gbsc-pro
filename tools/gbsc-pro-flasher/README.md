# GBSC Pro flasher (Linux)

A native Linux flasher for the **RetroScaler GBSC Pro** upscaler — the composite/
S-video variant of gbs-control that sits on the RISC PC's video→HDMI path.

RetroScaler only ship Windows tools (`GBSC_PRO_Programmer.exe`,
`NodeMCU-PyFlasher.exe`) in their firmware zip. This directory reverse-engineers the
AV-module programmer's serial protocol so the GBSC Pro can be updated from this box
without Windows. See [`PROTOCOL.md`](PROTOCOL.md) for the full wire spec and how it
was derived (ILSpy decompile of the vendor exe).

`gbsc_pro_flash.py` is a thin driver: the YMODEM transfer itself is done by the
[`ymodem`](https://github.com/alexwoo1900/ymodem) library (its "CP/M YAM" style
happens to produce the bootloader's non-standard **filename-only block 0**), and this
script adds only what the library can't: the `HCMGBoot.` → `U` → `1` →
`Enter download` bootloader handshake. `ymodem` is not on PyPI in a usable form, so it is
pinned inline (`buildPythonPackage` + `fetchFromGitHub`) and puts it in the dev shell.

## The GBSC Pro has two firmwares

| Part | Chip | File in release zip | Flash with |
|------|------|---------------------|------------|
| **AV module** | HDSC **HC32** (+ LM1881 sync) | `GBSC_PRO_AV_MODULE_vX.Y.bin` | `gbsc_pro_flash.py` (this tool) |
| **ESP module** | ESP8266 (web UI) | `GBSCPro_YYYY-M-D.ino.bin` | `esptool` (see below) |

For **v1.3** the *only* change is in the AV module (the "1X crop" / 525p-625p
autoswitch fix); the ESP bin is unchanged since v1.2.3. The matching official bins for
v1.3 are staged in [`firmware/`](firmware/).

## Requirements

`pyserial` + `ymodem` (which pulls in `ordered-set`):

```bash
pip install -r requirements.txt
```

Your user needs access to the serial device (`dialout` group, or run under the
sudo-askpass wrapper).

## Flashing the AV module (this tool)

Getting into bootloader mode is the finicky part (cf. RetroScaler/gbsc-pro
issue #6). Verified working procedure on real hardware:

1. **Port:** the AV module is the **USB-C** socket (the micro-USB is the ESP, which
   shows up as a CH340/CH341 → `ttyUSB*` — *not* what you want here). Use a known
   **data** USB-C cable, not charge-only.
2. **Enter bootloader:** hold the **update button**, plug the GBSC Pro's power in,
   keep holding ~3 s, release. It should flash **red/green** and enumerate as
   **`2e88:4603` (XHSC CDC) → `/dev/ttyACM0`**. Confirm with `./gbsc_pro_flash.py --list`.
   Note: the bootloader does *not* emit a banner passively — `--probe` will read
   nothing even when it's ready; the `2e88:4603` enumeration is the real proof.
3. **Flash** (auto-detects by VID:PID, or pin `--port /dev/ttyACM0`):
   ```bash
   ./gbsc_pro_flash.py firmware/GBSC_PRO_AV_MODULE_v1.3.bin
   ```
   Until your user is in the `dialout` group, the port is root-only — run under sudo
   with the dev-shell interpreter:
   ```bash
   sh -c 'sudo -A "$(command -v python3)" \
     gbsc_pro_flash.py --port /dev/ttyACM0 firmware/GBSC_PRO_AV_MODULE_v1.3.bin'
   ```
4. After "Update success", **power-cycle the GBSC Pro normally** to boot the new
   firmware (the red/green flashing stops and the picture returns).

Verify the tool without any hardware attached:

```bash
./gbsc_pro_flash.py --selftest      # CRC, filename-only block 0, SOH/128 framing
python3 test_loopback.py            # full handshake + transfer vs an emulated bootloader
./gbsc_pro_flash.py --probe         # read-only: dump the bootloader banner (when connected)
```

**Safety:** the transfer is plain YMODEM to a resident bootloader — a failed/
interrupted flash leaves the bootloader intact, so just power-cycle and retry. Only
flash the official AV-module bin for your hardware; do not send the ESP `.ino.bin`
here.

## Flashing the ESP module (esptool, not this tool)

The ESP half is a stock Arduino/ESP8266 image; flash it at offset `0x0`:

```bash
esptool.py --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x0 firmware/GBSCPro_2025-7-29.ino.bin
```

(This is exactly what the bundled NodeMCU-PyFlasher does; it relies on DTR/RTS
auto-reset to enter flash mode.) Back up your slots/Wi-Fi config from the web UI first
if the flash might reset them. For v1.3 you can skip this — the ESP bin didn't change.

## Provenance

- Upstream firmware: <https://github.com/RetroScaler/gbsc-pro> (v1.3, 2025-11-15)
- Vendor tool reversed: `GBSC_PRO_Programmer.exe` ("GBSC PRO Programmer V0.2",
  .NET/WinForms on the YModemWin library)
