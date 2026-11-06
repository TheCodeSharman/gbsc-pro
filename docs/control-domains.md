# The system has three control domains, and you can only see one

This is the single most expensive thing to not know. An evening was spent
diagnosing "the unit" while able to observe roughly a third of it.

| Domain | Reaches | Visible to you? |
|---|---|---|
| ESP8266 | TV5725 registers, Si5351, STV9426 (0x5D), audio | `/getregs` — **this is all you can see** |
| HC32F460 | `ASW_01`-`ASW_04` analog input routing (pins PB12-PB15), the ADV7280 | **write-only** — commandable over UART, never readable |
| MS9288A | HDMI encoding, EDID, output link | no — on nobody's I²C bus |

- **The HC32F460's firmware source is in this repo**, under
  `GBSC-Pro-Source code/usart_uart_dma - （IapApp）/usb_dev_cdc/source/`. The
  directory name and `main.c`'s header are a stock Xiaohua UART-DMA example;
  `videoprocess.c`, `uart_dma.c` and `flash.c` are the AV module. Version-tagged
  in git history (`V1.3`, `v1.2.3`, `v1.2.2`). These files are ISO-8859, so
  `grep -r … | grep -v Binary` hides them — **use `grep -a`.**
- **The ESP commands the HC32 over its UART**, ESP TX → HC32 `USART4` RX (PB7,
  pin 43), 115200 8N1. 7-byte frame: `41 44 <cmd> <arg> <val|nonce> FE <sum of
  bytes 0-5>`. `'S'` selects input: `0x4n` RGBs, `0x5n` RGsB, `0x6n` VGA,
  `0x70` YPbPr, `0x1n` S-Video, `0x2n` composite; `0xA0`/`0xA1` toggle
  `asw_02`. **NO REPLY REACHES THE ESP, AND THE REASON IS THE BOARD RATHER THAN
  THE FIRMWARE.** `ESP_RXD` is driven only by the CH340T's TXD through R60, and
  the HC32's `USART4` TX (PB6, pin 42) goes to J18 pin 2 and to SW2, never to
  the ESP — so no firmware change creates that path, and the commented-out
  `'I'` INFO handler follows from the missing conductor. Schematic sheet 12.
- **SW2 IS THE UPDATE BUTTON, AND THE APPLICATION ALSO READS IT.**
  `Key_Init()` configures PB6 as a pulled-up input and `Key_Read()` polls it,
  so to the running application it is a key grounding PB6 through R106 — which
  also means the application claims the pin `USART4` TX would otherwise drive.
  **That says nothing about the bootloader, which runs first and reads the same
  button**: hold it, apply power, keep holding ~3 s, and the part enumerates as
  `2e88:4603` on the USB-C socket. `tools/gbsc-pro-flasher/` is the route and
  it is verified on this hardware — **no SWD probe and no board modification**.
- **THE HC32 IS REACHABLE AND ITS FIRMWARE IS UPDATABLE, AND NO ROUTE NEEDS A
  BOARD MODIFICATION.** All three reach a HOST rather than the ESP, so they buy
  visibility and a way to flash the part — never a readback the ESP can act on.
  The part is an `HC32F460JEUA-QFN48TR`, so pin numbers are QFN48.

  | route | where | state |
  |---|---|---|
  | SWD | J19: 1 `DVDDIO_3.3V`, 2 `SWDIO` (PA13, pin 34), 3 `SWDCLK` (PA14, pin 37), 4 GND | the vendor's own MDK project ships `JLinkSettings.ini` and a `debug_init.ini` that loads the hex, so this is the route the board author used |
  | `USART4` | J18, a 6-pin header carrying PB7, PB6, PB5, PB4, PB3, GND | TX is already on pin 2, so a USB-serial adapter is a read channel. `BSP_PRINTF_DEVICE` points at USART3, so `printf` needs re-pointing to come out of it |
  | USB device | USB1, a USB-C receptacle, straight to PA12/`USBFS_DP` and PA11/`USBFS_DM` with CC1/CC2 pulled down 5.1K | **the flashing route, and it works** — `tools/gbsc-pro-flasher/`. The APPLICATION never enumerates (`LL_USB_ENABLE` is `DDL_OFF`, no USB source file in the MDK project, `main()` never touches it), which says nothing about the bootloader that runs before it |

  **The USB-C port is the HC32's, not the ESP's.** The ESP's serial is a
  separate micro-USB, J5 → CH340T, on sheet 1, so the two enumerate
  independently and the cable that powers the board is a free channel to the
  HC32. **The MDK project is not an IAP application despite the folder name**:
  the scatter file maps the image at `0x00000000` across the whole `0x40000`
  with no reserved boot region.
- **THE HC32 HAS THREE TWO-WIRE BUSES AND ONLY ONE CARRIES A CHIP**, which is a
  trap because the chips are NOT on the pins named `I2C_*`. Schematic sheet 12:

  | HC32 pins | net | on it |
  |---|---|---|
  | 17 / 16 (PA7/PA6) | `SDA` / `SCL` | the ADV7280 (0x42, ALSB high) and the ADV7391 |
  | 45 / 44 (PB8/PB11) | `I2C_SDA` / `I2C_SCL` | nothing -- out to J16, a 4-pin header, with 2.2K pull-ups. **PB11 is the `MD` boot-mode pin**, so J16 pin 1 is also the ROM-bootloader strap -- which UART the ROM then listens on is unconfirmed, the HC32F460 manual not being in this repo. PB9, pin 46, is unconnected |
  | 15 / 14 (PA5/PA4) | `SCREEN_SDA` / `SCREEN_SCL` | nothing -- out to J17, for an external screen |

- **The OLED menu is on the ESP**, not the HC32. Picking an input there works
  because that handler transmits the frame above.
- **`ADC_INPUT_SEL` is only half the input path.** It selects which TV5725 ADC
  input is used. Whether the HC32F460 has actually *connected* anything to it is
  `ASW_01`-`ASW_04`, which appear in no register dump. Two muxes in series.
  **VGA is the only input that raises `asw_01`** (the schematic's
  `ASW_01 = 0, HS_IN = SOGIN` — it selects the dedicated HSync pin over
  sync-on-green), and the only one raising `asw_01` and `asw_04` together.
- **Both MCUs persist "which input", separately, and neither can read the other
  back.** The ESP keeps the `input` setting in `/preferences.txt`; the HC32 keeps
  `asw_01..04` in its own flash and restores them via `Video_ReadNot2()`. The ESP
  reconciles them at boot: `applySavedInputSource()` (
  after `calibrateAdcOffset()` and `setResetParameters()` so neither overwrites
  it) points `ADC_INPUT_SEL` at the saved input and calls
  `sendSavedInputToAvModule()`, which transmits the frame.
  **The hole left is `SeleInputSource == 0`** — nothing meaningful saved, which
  is also what a short preferences read produces — where the `default:` case
  sends nothing and the two can still come up disagreeing. Picking the input
  repairs that, and saves the preference so the next boot sends it.
  **`/input?src=…` does it without a bench trip** — `rgbs rgsb vga ypbpr sv av`,
  queued for `loop()`, so a 200 means understood rather than selected. A unit
  that comes back from a flash showing no picture, with `STATUS_SYNC_PROC_VTOTAL`
  and `HTOTAL` at 0 and `DAC_RGBS_PWDNZ` 0, is usually this and not a fault worth
  diagnosing. **The bench RiscPC is on `vga`**, which is the only input raising
  `asw_01`; guessing `rgbs` because the source has composite sync wastes a
  detection sweep per guess.
  `applySavedInputSource()` records what it did as the boot log's `INPUT:` line,
  **which the default build does not keep**: `BOOTLOG_BYTES=0`, so rebuild with
  `BOOTLOG_BYTES=2048` before trying to tell "no frame sent" from "frame sent and
  ignored".
- **THE HC32 CAN STOP FOLLOWING INPUT SELECTIONS, AND NOTHING ON THE BOARD SEES
  IT.** `asw_01` left low on VGA puts `HS_IN` on `SOGIN`, so an RGB source with
  separate sync delivers vertical and no horizontal: a correct field rate beside
  a doubled line count and `NO EDGE`. `/sc?~`, `/input`, `/restart`, a sample
  clock restart and a source mode round trip all fail to reach it; **a mains AND
  USB power cycle recovers it at once**. The cheap discriminator is the OTHER
  input -- one `/input` request, and a source that acquires proves the board.
  `docs/known-issues.md`.
- **AV module v1.3 changes only the ADV7280/ADV7391 composite path** (525p vs
  625p encoder config). `uart_dma.c` and `flash.c` are byte-identical to v1.2.3.
  It cannot affect RGB/VGA routing — don't reach for it to fix a VGA fault.
- **A register dump is not the state of the machine.** `/getregs` reads the
  TV5725 and nothing else.
- **The MS9288A cannot be reset, queried or configured** by anything on the
  board. Only removing power clears it. Its I²C slave pins are wired to
  nothing — SDA (29) and SCL (28) carry no-connect flags on schematic sheet
  4/13 — so an NDA register map would still not reach it, and neither MCU's
  firmware addresses it: the HC32 drives the ADV7280 and the ADV7391, and the
  ESP drives the TV5725, Si5351, STV9426, PT2257 and the EEPROM. The
  encoder configures itself from its own MCU and ROM — `MCUSEL` (pin 4) is
  strapped low through R56, and the firmware is on-die mask ROM, not an
  external part — and reads the sink's EDID over its own DDC master.
