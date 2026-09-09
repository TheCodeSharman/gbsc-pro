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
