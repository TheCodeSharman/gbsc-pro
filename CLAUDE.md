# GBSC-Pro / RetroScaler

Fork of gbs-control for the GBSC-Pro board: a TV5725 video scaler driven by an
ESP8266, analog RGB/YPbPr/composite in, HDMI out. Firmware lives in
`GBSC-Pro-Source code/gbs-control/`; `docs/` carries the rest.

**Read the page that covers what you are about to touch. Each one is a list of
things that have already cost a session.**

## Start here

| read | before |
|---|---|
| [known-issues.md](docs/known-issues.md) | diagnosing anything — the register of open defects and unsettled questions. A new one goes here, never in a handover |
| [bench-rules.md](docs/bench-rules.md) | judging any change: both sources are powered and scriptable, acquisition is seconds, fix the scaler and not the source |
| [bench-commands.md](docs/bench-commands.md) | running anything — build, flash, host suite, pytest, and driving the RISC PC over ModeServ |
| [bench-instruments.md](docs/bench-instruments.md) | reaching for a register dump. The console says what the engine is DOING; `/samplinglog` and `/testbus` are the other two |
| [conventions.md](docs/conventions.md) | writing code or a commit — the three configure events, the two engine entry points, comment and branch discipline |
| [CODING_STYLE.md](CODING_STYLE.md) | writing firmware C++. Not optional |

## The reference pages

| page | what |
|---|---|
| [repo-layout.md](docs/repo-layout.md) | what is in this tree and in the three sibling repos |
| [control-domains.md](docs/control-domains.md) | ESP, HC32F460 and MS9288A — you can only see one of the three |
| [bench-traps.md](docs/bench-traps.md) | the hour-costing list: USB backfeed, heap gate, stale build cache, nix from the wrong directory |
| [register-facts.md](docs/register-facts.md) | which status bits are not verdicts, the PLL latch, the sampling clock, the eleven-bit line counters |
| [working-with-the-unit.md](docs/working-with-the-unit.md) | snapshots, reading fields by NAME, the television, the shape of an eye-in-the-loop measurement |
| [diagnostic-toolkit.md](docs/diagnostic-toolkit.md) | what to DO with the instruments once they are open |
| [no-hdmi-output.md](docs/no-hdmi-output.md) | every register perfect and the TV dark — and why "it is the encoder" is the standing wrong answer |
| [datasheet-and-register-names.md](docs/datasheet-and-register-names.md) | RD-5725-1.1 contradicts itself in three places; the header is the survivor |
| [preset-load-reality.md](docs/preset-load-reality.md) | what a preset load writes now the tables are deleted |
| [osd-vs-oled.md](docs/osd-vs-oled.md) | two menu trees, three renderers, told apart by their INPUT and not by the screen |

Design and subsystem pages are indexed in [repo-layout.md](docs/repo-layout.md)
— geometry, aspect ratio, sampling, sync type, bypass, preferences, OTA, the
web UI chain, and the datasheets.

## The four rules that are broken most often

- **Read fields by NAME.** `read_named(host, "PLLAD_MD")`. A wrong name raises;
  a hand-written segment/register/offset returns a plausible lie.
- **Enter the dev shell from the repo root**, or `NIX_LDFLAGS` word-splits on
  the space in the source path and the link fails as a missing directory.
- **Run the host suite ONCE** — `test/Makefile` is already parallel. Redirect to
  a file and grep the file; a second pipeline runs all 54 suites again.
- **Work goes on `dev`.** `main` is what has been reviewed and moves only when a
  review advances it. Check the branch before the first commit of a session.

## Commands

```sh
nix develop                       # tools: arduino-cli, make, esptool, python3+pytest
make -C build                     # compile -> build/output/gbs-control.ino.bin
make -C build flash-ota HOST=…    # upload over WiFi (USB: make -C build flash)

nix develop -c make -C test >/tmp/test.log 2>&1; echo "make: $?"
grep -E "ERROR|FATAL" /tmp/test.log

ruff check tools                  # BEFORE any hardware run
pytest tools/gbsc-pro-hwtest/ --host=192.168.88.108 -v
```

Every hardware flag defaults ON, so `pytest --host=…` is the whole suite.
`--source` leaves the picture wrong; a cold boot fixes it.
