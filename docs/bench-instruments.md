# READ THE CONSOLE FIRST. IT SAYS WHAT THE ENGINE IS DOING.

**The status WebSocket is the cheapest instrument on the board and it answers
before any register can.** A register dump is a snapshot of a machine that
re-solves itself every few seconds; several faults here are a SEQUENCE rather
than a state, and the console is the only place a sequence is visible. Reach for
it first, ahead of `/getregs`, `/geometry` and a photograph.

```sh
nix develop -c python3 - <<'PY'
import websocket, time
ws = websocket.create_connection("ws://192.168.88.108:81", subprotocols=["arduino"], timeout=5)
ws.settimeout(1); t0 = time.time()
while time.time() - t0 < 90:
    try: print(f"{time.time()-t0:7.2f}  {ws.recv().strip()}", flush=True)
    except Exception: pass
PY
```

**TIMESTAMP EVERY LINE, AND CAPTURE AT LEAST 60 SECONDS.** The cadence is the
finding. Printed bare, a solve every five seconds that nothing asked for reads
as a healthy engine reporting a correct measurement, over and over.

| line | what it says |
|---|---|
| `sampling: 311 lines x 50.08 Hz -> line rate 15625` | **the engine's whole measurement of the source in one line** — the count, the field rate, and the line rate derived from them. `line rate 0` is a reading `rateFollowsCount()` rejected. A first sample of 60..160 Hz followed by a good one is `getSourceFieldRate()` settling, not a fault |
| `source moved: interrupt \| count \| rate (N lines, solved M)` | why a solve was armed. `interrupt` with `N == M` is the count saying nothing moved, which is the case the latch exists for -- the same count at a different field rate. **An arm SPENDS the latch**, so one taken during a change the engine armed for another reason cannot fire again afterwards |
| `own V sync: yes\|no after Nms` | the sync type probe ran. **It writes `SP_EXT_SYNC_SEL`, and the chip latches that as a SOG switch**, which the probe acknowledges itself -- so an `interrupt` arm after one is a disturbance the probe did not cause |
| `no INPUT vsync` / `no OUTPUT vsync` | which FrameSync sample timed out. Do not infer which — it says |
| `input selected: ypbpr, reference divider 2506, reset +18ms, …` | the selection edge, said once the sequence is complete. **It must arrive BEFORE the first `DETECT`** — that ordering is what says the arriving source will be counted through a reference clock rather than the previous source's, and it is the only part of that fault which repeats reliably enough to test. It lands ~250 ms after the request, `/input` being queued for `loop()` |
| `h:%4u … ht:%4d vt:%4d … u:%3x s:%2x S:%2d` | `printInfo()`. `h:` is `HPERIOD_IF`, `ht:`/`vt:` are `STATUS_SYNC_PROC_HTOTAL`/`VTOTAL`, **`u:` is `VideoSourceAcquisition::unmeasuredPasses()` IN HEX**, `s:` is `acquiredPasses()`, `S:` is the SOG level |

**`u:` climbing with `s: 0` is a diagnosis on its own.** One of the two is always
zero: `u:` counts the passes since the engine could last measure the source and
`s:` the unbroken run since it could. `u:` pinned in the nineties is the ladder
cycling its recovery on a source it never acquires, roughly every five seconds,
for ever. `vt:` beside it says whether the sync processor is counting at all,
which separates a dead sync path from a source the engine is refusing.

**`printf()` IN THE SKETCH DOES NOT REACH THE CONSOLE. `debugPrintf()` DOES.**
The console mirror is `SerialM`, and `debugPrintf` is
`SerialM.printf_P(PSTR(fmt), ...)`; a bare `printf` goes to stdout and is
invisible over the websocket. Several existing messages are bare `printf` --
the colour-offset readback among them -- so **a missing line is not evidence the
branch did not run**, and a new diagnostic written with `printf` produces a
silent route that answers 200.

**A quiet console is not a quiet firmware.** Silence with a live HTTP stack
means the loop is not running, or the heap gate is shut — read `/bootlog`'s
`free heap:` line before believing it.

**AND IT IS DEAF FOR THE FIRST ~15 SECONDS OF A BOOT**, which is where
detection, the sync-type probe, the first solve and the rate match all happen.
`/bootlog` holds them, and **everything printed through `SerialM` is already in
it** — `SerialMirror::write()` appends there itself, so no line needs anything
added to survive a boot and a second path alongside it double-writes. The whole
of what is needed is `BOOTLOG_BYTES=2048` on the flash line. It stops recording
once a websocket client takes delivery, so open no console until after the read.
`docs/gbs-control-debug-interface.md`. And the console DROPS BURSTS under
FrameSync spam, so a missing line is not evidence the step did not run: judge by
outcome, and by what the next line implies.

### WHERE A SIGNAL REACHES IS `/testbus`, AND ONLY THE DEVICE CAN TIME IT

`TEST_BUS_SEL` (`s0_4d[4:0]`) picks which block drives `DEBUG_IN_PIN`, and the
transition count over one window separates a field-rate signal from a line-rate
one and both from a dead bus. The pin is an ESP GPIO, so no HTTP read can see
it; `/testbus` queues a sweep for `loop()` and prints CSV to the console.

```sh
curl 'http://<ip>/testbus?ms=25'            # every selector
curl 'http://<ip>/testbus?ms=25&sp=4'       # with a sync-processor stage out
curl 'http://<ip>/testbus?ms=25&if=0'       # with an input-formatter signal out
```

At 50 Hz expect single-digit transitions in 25 ms, at 15.6 kHz several hundred.
`SP_TEST_MODULE` selects one sync-processor stage (4 `vs_act_det`, 6 retiming, 7
out proc) and `IF_TEST_SEL` one input-formatter signal, so **a sweep taken on
each sync type says which stage stops carrying vertical sync.**

### AN HTTP READ IS NOT A SAMPLE. `/samplinglog` IS.

`/getreg` is deferred to `loop()` and answers at tens of hertz at best, so it
cannot tell a value that dithers from one read torn across two states, and it
cannot see a transient at all. `Tv5725::SamplingLog` samples from inside
`loop()` and prints CSV to the console.

```sh
make -C build flash-ota HOST=… GBS_SAMPLING_LOG=1     # off in the default build
curl 'http://<ip>/samplinglog?ms=25&for=30000'        # follow the source
curl 'http://<ip>/samplinglog?low=1600&high=2900&step=100&dwell=400'   # walk the divider
curl 'http://<ip>/samplinglog?rates=120'              # time the field rate, N times
```

**`rates=` IS WHAT ASKS WHETHER A MEASUREMENT IS STEADY WITHOUT THE ENGINE IN
THE WAY.** It times the source's field rate off `DEBUG_IN_PIN` exactly as
`SourceMeasurement` does and prints `rate,ms,sp_vtotal,field_rate_mhz`, and it
runs from `loop()` rather than from the acquisition tick — so it still answers
with automation FROZEN. That is the only way to ask whether a register a solve
writes disturbs what the solve then measures: frozen, nothing rewrites the
register, and a spread belongs to the register rather than to a loop closed
through the engine. 120 readings returning one value at sd 0.000 is what a
steady source looks like.

**`flash-ota` REBUILDS, so the flag has to be on THAT line.** A `GBS_SAMPLING_LOG=1`
passed to a bare `make -C build` does not reach the unit — the `flash-ota` target
rebuilds without it and uploads the default image. **An unregistered route
answers 500, not 404**, which reads as a crash in the handler rather than as a
build missing the flag, and the difference is one word on the upload line.

One line per sample carries the divider, `STATUS_MISC_PLLAD_LOCK`,
`STATUS_SYNC_PROC_VTOTAL`, `STATUS_SYNC_PROC_HTOTAL`, `HPERIOD_IF`,
`VPERIOD_IF`, `HSACT`, the IF status bits and the latched interrupt byte — read
adjacently in one pass, which is what makes two of them comparable to each other.
`SamplingLog::event()` logs a decision as the branch takes it, which no dump
afterwards can show.

**QUEUE IT FROM THE PROCESS THAT HOLDS THE WEBSOCKET.** The log prints to the
console, so a capture has to be listening before it starts -- and queuing from a
shell while a separate capture process starts up races both ways: the request
answers `{"error":"already running"}` against a previous run, or the log finishes
before the socket is attached. Either gives an empty capture, which reads as a
dead route rather than as a missed window. Open the socket, poll the route until
it answers `queued`, then read until `smp,done`.

**A `sol,` line says where the engine SOLVED**, emitted when it moves rather
than per sample: the raster, both scales, both display windows, both output
sync pulses, `IF_HSYNC_RST` and `IF_HBIN_SP`. That is the set two runs are
compared on, and taking it over HTTP is what changes the outcome — a full dump
per return is hundreds of `loop()`-deferred requests through the loop being
measured, and the affordable subset used instead left out `VDS_HS_ST`, which is
a pan.

`STATUS_SYNC_PROC_VTOTAL` on the Wii is the measurement that made the case:
HTTP point reads gave 149 / 160 / 230 / 299 among 310s where the log gave
**310 in 1050 of 1050**, so HTTP over-reported a steady register as
intermittent. **A count of agreeing HTTP samples is not evidence of stability**
— a dozen reads at five-second spacing say nothing about the 5.99 seconds
between each pair. The console also drops bursts, so ask for an interval the
link can carry: `ms=25` over 30 s lands ~1050 lines and reports its own
effective rate.

**AND A READING TAKEN TWO SOURCE EXCURSIONS LATER IS NOT A SECOND INSTRUMENT.**
An input change or a sync-type round trip re-rails the register, so a healthy
HTTP sample before one and a railed on-device sample after it compare two
states, not two instruments. That is how the refuted claim above was arrived at,
twice. Sample both in ONE window, or say which state each belongs to.
