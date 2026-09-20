# Building the reference firmware, with instruments

`be58a24f4` is the last firmware commit before the 2026-08-02
`bypass-800x600-fills` snapshot, so it is the build that reference was captured
on. It is what an old-against-new comparison is judged against.

**It cannot be driven from a session as it stands.** It has no `/getregs`, no
`/input`, no `/samplinglog`, and its console heap gate is 20000 -- a figure this
fork never reaches, so the websocket accepts a client and delivers nothing.
Upstream `1.3` is worse: no register routes at all, and `dumpRegisters()` exists
with every print inside it commented out.

`be58a24f4-bench-instruments.patch` adds the smallest set that makes it
answerable without a bench trip, and nothing else:

- `/input?src=rgbs|rgsb|vga|ypbpr|sv|av`, queued and acted on in `loop()`,
  calling that build's own `Input*_mode()` handlers so the HC32 frame and the
  ADC mux move together. **Its detection still sweeps for a live signal**, so a
  second source with sync on another ADC input wins regardless of the choice --
  unplug what you are not testing.
- `/samplinglog?ms=&for=`, emitting the same CSV columns as
  `Tv5725::SamplingLog` so one parser reads both.
- the console gate lowered to 8000, without which neither prints anything, and
  its `else` branch removed -- the no-argument `webSocket.disconnect()` there
  drops every client at once.

`/getreg` and `/setreg` are already present at this commit; `/getregs` is not,
so a full snapshot is 1536 single reads, about 90 seconds.

```sh
git archive be58a24f4 'GBSC-Pro-Source code/gbs-control' | tar -x -C /tmp/ref
mv '/tmp/ref/GBSC-Pro-Source code/gbs-control' /tmp/ref/gbs-control
patch -d /tmp/ref/gbs-control -p1 < tools/reference-build/be58a24f4-bench-instruments.patch
make -C build clean
make -C build SKETCH=/tmp/ref/gbs-control GBS_BUILD_REV=ref-instrumented flash-ota HOST=<ip>
```

The sketch directory must be named `gbs-control`, because arduino-cli requires
it to match the `.ino`.

**Going back costs the filesystem, both ways.** This build is SPIFFS where the
current one is LittleFS, so each switch reformats the region and loses
`/framing.txt`, `/slots.bin`, `/slots.txt` and the saved preferences. Pull them
over `/fs/download` first and expect to reselect the input afterwards, because
`SeleInputSource` comes back 0 and its `default:` case transmits nothing -- the
OSD then shows one input while the HC32 routes another.

WiFi survives: credentials live in the SDK's own flash sector rather than the
filesystem, which is what keeps OTA available in both directions.

## The prebuilt images

Both sides of the comparison are kept built, in `~/Projects/gbsc-pro-backups`
beside the flash dumps, because rebuilding to switch is two minutes and a
cleared build cache each way:

    esp_ref-be58a24f4-instrumented_2026-09-09.bin   the 08-02 firmware, patched as above
    esp_current-<rev>-samplinglog_2026-09-09.bin    ours, GBS_SAMPLING_LOG=1 so the
                                                    sampler answers on both

`flash-bin.sh` arms the unit and uploads one of them without building anything:

```sh
tools/reference-build/flash-bin.sh 192.168.88.108 \
  ~/Projects/gbsc-pro-backups/esp_ref-be58a24f4-instrumented_2026-09-09.bin
```

They are build artefacts and live outside this repo deliberately: LFS is refused
on a public fork, and Nix copies tracked files into the store on every dirty
evaluation.

**Budget a filesystem reformat each way** -- see above -- so pull `/framing.txt`,
`/slots.bin` and `/slots.txt` over `/fs/download` before switching, and expect to
reselect the input afterwards.

## After the switch, expect to run `/sc?~`

The chip keeps its registers across an ESP reset, so detection restarts against
whatever sync path was left behind. Switching firmware while the saved input is
gone lands on the YPbPr path, and a separate-sync source then reads
`SP_SOG_MODE` 1 with `STATUS_SYNC_PROC_VTOTAL` 97 -- the unlocked value -- no
matter how many times the input is reselected. `/input?src=vga` moves
`ADC_INPUT_SEL` and nothing else; it is `/sc?~` that forgets the sync type.

Measured today, in order: `/input?src=vga` gave `ADC_INPUT_SEL` 1 with
`SP_SOG_MODE` still 1 and `VTOTAL` 97; `/sc?~` then gave `SP_SOG_MODE` 0 and
`VTOTAL` 627 with a full-screen picture, about 35 seconds later.

## The two DAC routes are not mutually exclusive on this build

Measured on it: `DAC_RGBS_ADC2DAC` 1 **and** `DAC_RGBS_BYPS2DAC` 1 at the same
time, after an HD bypass switch followed by the 535-line override sending the
same source to the RGBHV route. Neither switch clears the other's bit.
`Tv5725::Chip` makes the three routes alternatives and each one clears the
others, so a snapshot from this build can hold a combination the current
firmware cannot produce. Do not read one as evidence about the other.
