# Testing

What runs where, which layer a new test belongs in, and the disciplines that
exist because skipping them cost a session.

Companion to `docs/chip-initialisation.md`, which is the design this tests.

## The layers

Cheapest first. **Use the cheapest layer that genuinely exercises the
behaviour** — never drop to a slower one to dodge wiring up the fast one.

| layer | command | needs | speed |
|---|---|---|---|
| **host unit** | `make -C test` | g++ + doctest | ~1 s, all of it |
| **host tooling** | `pytest tools/gbsc-pro-hwtest/` | nothing | ~11 s |
| **hardware acceptance** | `pytest tools/gbsc-pro-hwtest/ --host=<ip>` | a running unit | ~1 min |
| **write trace** | `capture_traces.py` | a unit, USB, a trace build | ~1 min a branch |
| **bench** | by hand, one register at a time | a unit and eyes | slow, and the only judge of a picture |

```sh
make -C test                     # 15 host binaries
pytest tools/gbsc-pro-hwtest/ -q # hardware tests skip without --host
pytest tools/gbsc-pro-hwtest/ --host=192.168.88.108 --source -v
```

A bare `pytest` with no `--host` stays useful and safe: everything needing
hardware skips.

### Opt-in flags

Tests that would disturb a working picture or write flash are behind flags in
`conftest.py`, so `pytest --host=…` is safe to run on a unit someone is using:

| flag | opts into |
|---|---|
| `--source` | tests needing a locked signal |
| `--preset-save` | tests that write flash |
| `--no-sync` | tests that drop sync deliberately |
| `--freeze` | the `/freeze` acceptance test |
| `--pllad-hostile` | tests that move the ADC divider |

## The write trace

The equivalence oracle for a branch no bench source can reach. A
`GBS_TRACE_WRITES` build prints every register write to hardware Serial;
`/trace/standard` forces a standard and runs a load; `capture_traces.py` collects
the runs and `trace_oracle.py` turns them into the ordered subsequence common to
all of them, recording what differed as variable rather than asserting it.

**IT HAS TO BE USB.** The trace never reaches SerialM, because `broadcastTXT()`
allocates per frame and the build runs on ~21 KB of free heap.

### Which entry is traced decides what the oracle contains

| `--via` | runs | reaches |
|---|---|---|
| `post` | `doPostPresetLoadSteps()` | the scaled standards |
| `apply` | `applyPresets()` | both bypass switches, on 15 and on 5/6/7/13 |
| `bypass` | `setOutModeHdBypass()` | that switch's SD and progressive arms |

`applyPresets()` hands the HD switch 5, 6, 7 and 13 and nothing else, so its
1/2 and 3/4 arms are reachable only by calling it directly — the pass-through
preference is what does that at runtime.

**AN ORACLE THAT DOES NOT REACH THE CODE DOES NOT FAIL — IT PASSES.** A trace
taken through the wrong entry silently contains none of the writes being asked
about, and the diff comes back empty. Check that the fixture carries a
fingerprint of the code under test before believing an empty diff.

### Comparing two oracles

**Check the run lengths agree first.** A noisy capture drops writes from its
longest-common-subsequence, and those then read as "only after" in the diff. A
baseline with 139 stable writes against an after with 161 produced 20 spurious
differences that were nothing of the kind.

`compare()` reports `equivalent=False` with EMPTY `onlyBefore`/`onlyAfter` when
only the ORDER changed. Read the two sequences side by side, not the summary.

**Bracket the code under test.** Where the change is a move, the decidable
comparison is not the whole trace but the slice between the write that precedes
the moved code and the one that follows it. That slice is the same length and
the same values in both, or the move is not faithful; everything outside it is
the settle loops and the live measurements, which vary by capture.

**Capture in short sessions.** Eight standards in one run drifts: a branch that
is a steady 1046 writes in a four-standard run came back 1217/1205/1049 in an
eight-standard one, which is a fixture with no oracle worth having.

**When two captures of the SAME build disagree, that is the answer.** Before
concluding a change moved something, re-capture the after and diff it against
itself.

### Tracing with no source attached

The forcing route calls the load directly, so it runs whether or not a signal is
present, and a before/after pair taken without one is still like-for-like. Two
things change, and both matter:

- **The traces get longer** — about 700 writes on a scaled standard against 220
  with a source — because the no-sync retry paths run inside the load.
- **`ADC_SOGCTRL` (s5_02) becomes session-variable.** `loop()` ratchets the
  sync-on-green slicer down every 500 ms while the source is disconnected, so
  its value depends on how long the unit has been up. It is stable with a source
  and is NOT in `SESSION_VARIABLE`, because ignoring that address blanket would
  hide `ADC_INPUT_SEL` in the same byte.

**A sourceless pair cannot be compared against the committed fixtures**, which
were captured with the bench source locked. Compare it against its own before.

## Host unit tests

`test/*.cpp`, doctest, one binary per subject, each a target in `test/Makefile`.
They live in `test/` and not `build/` because `build/` is the arduino-cli driver
and a test is not a build artefact.

Two kinds:

**Pure arithmetic** — `MemoryMap`, `SdramTimings`, `OutputMode`, `SourceMeasurement`,
`DisplayClock`, `PresetLoad`. No chip, no Arduino. This is where most logic
should live, and the pure/register split in `src/tv5725/` exists largely to put
it there.

**Register bring-up, against a fake Wire** — `test_memory_bus.cpp`,
`test_frame_buffer.cpp`, `test_input_formatter.cpp`, `test_hd_bypass.cpp`,
`test_mode_detect.cpp`, `test_deinterlacer.cpp`, `test_segment_select.cpp`.
See below.

### The fake Wire seam

**`tw.h` is pure C++** — header-only templates, static methods, no virtuals —
and its only Arduino dependency is `Wire`, reached through exactly two
functions, `tw::detail::rawRead` and `rawWrite`. Every register access in the
firmware funnels through those.

So `test/fake/Wire.h` is the entire seam: an in-memory model of the six register
banks behind the segment pointer at `0xF0`. Put it on the include path and
firmware `.cpp` files that touch `GBS::` compile unchanged, **with no `#ifdef`
in the firmware**.

```make
cd "$(SKETCH)" && $(CXX) $(CXXFLAGS) -I "$(HERE)" -I "$(HERE)/fake" \
  -o "$(OUT)/test_memory_bus" \
  "$(HERE)/test_memory_bus.cpp" src/tv5725/MemoryBus.cpp src/tv5725/SdramTimings.cpp
```

`-I test/fake` makes `#include <Wire.h>` inside `tw.h` resolve to the fake;
`-I test` lets the test say `"fake/Wire.h"`. The test file defines the instance
(`FakeTwoWire Wire;`) because the header only declares it.

**Fake at `Wire`, not at `GBS::`.** A register access is *two* transactions —
aim the pointer at `0xF0`, then access — and this project lost an evening to
those coming apart: after a pytest run segment 1 held segment 3's bytes, because
a `0xF0` write went missing on a bus shared with the Si5351 and the OLED. A fake
intercepting `GBS::write()` is handed a field and a value and can never express
that. This one sees bytes and banks, so a test can assert *where* a write
landed. Both mutation tests that moved a write to another segment were caught by
that, and by nothing else.

The fake offers `bank[seg][reg]`, `touched[seg][reg]`, `poison(value)` and
`field(seg, reg, offset, width)`.

Files excluded from host builds are `test/Makefile`'s `HOST_GEOMETRY_SRC`. The
list shrinks as subsystems graduate.

**The glob means a new class joins every target, so one that will not compile on
the host breaks all of them.** `HOST_GEOMETRY_SRC` lists what to leave out, not
what to put in, which is deliberate -- a class that graduates needs no Makefile
edit. The cost is that adding a `src/tv5725/*.cpp` calling `millis()`, `Serial`
or anything else `test/fake/Arduino.h` withholds takes out `axis`, `scale`,
`memory` and the rest at once, and the error names the new file while the
failing target is one that has nothing to do with it. `make -C test` after
adding a class, every time.

A class that genuinely needs the time takes it as an argument -- `SamplingLog`
is the worked example. A class that needs a subsystem the list already excludes
has to be excluded with it, which is why `SamplingLog` sits beside `Adc`.

## Disciplines

These are not style preferences. Each is here because its absence cost real
time.

### Poison before asserting ownership

**The TV5725 keeps its registers when the ESP reboots.** Reading the right value
proves nothing on its own — it may simply be what was already there. Two
conclusions were drawn from exactly that mistake in one day, in opposite
directions: leftover hand-writes read as "the code ran", and a warm boot that
never loaded a preset read as "the code is broken".

So: write a value that is **neither the table's nor the firmware's**, force the
code to run, then read back. `test_the_frame_buffer_subsystem_owns_the_memory_map`
and `test_the_memory_bus_subsystem_owns_its_timing` both do this.

Better still, where the layer allows it, ask whether a write *happened* rather
than inferring it from a value: `Wire.touched[seg][reg]`. A field whose owned
value happened to equal the poison would read correct having never been touched.

**But `touched` is per BYTE, and that is a hole where fields share one.**
`test_frame_buffer.cpp` was written with the usual `0xA5` and a mutation walked
straight through it: dropping the `WFF_FF_HALF_REQ` write changed nothing,
because `WFF_SAFE_GUARD` writes the same byte so `touched` stayed true, and
`0xA5` has bit 1 clear so the byte still read back as the 0 the test wanted.
Two assertions, neither able to fail.

So **choose the poison against the fields, not for looking unlikely**: every
owned field's value must differ from what the poison leaves in its bits. For
that file the constraint came out as bit 1 set, bits 3–5 clear, and the low six
bits equal to none of 24, 61, 36 or 60 — which `0xC2` satisfies. Write the
constraint down in the test; the next person adding a field to the class has to
re-check it.

### Choose a trigger that can discriminate

`test_the_memory_bus_subsystem_owns_its_timing` triggers with `/sc?y`
(`pal_1280x720`), whose table carries a *different* value from the owned one in
every field checked. Triggering with `/sc?)` would load `pal_1920x1080`, whose
table already carries three of the four owned values — and the test would pass
whether or not the firmware ran at all.

State it in the test when a field cannot discriminate. `MEM_ACT_CYCLE` is
asserted and every table carries the same value, so it proves nothing; the test
says so rather than letting a reader count six fields of evidence where there
are four.

### `/sc?#` is not a preset load

`applyPresets(13)` returns early — `result == 5 || 6 || 7 || 13` — *before*
`doPostPresetLoadSteps()`, so neither the bring-up nor the memory map runs. The
real preset-load trigger is **`/sc?)`** (`curl 'http://<ip>/sc?%29'`), or `/sc?y`
and friends for a specific table.

A warm reset does not re-apply the bring-up either. Both only run when a preset
actually loads.

### Mutation-check a new test

A passing test proves nothing until you have seen it fail. Break the production
code deliberately and confirm the failure names the right thing. `test_memory_bus`
was checked against three mutations — a dropped register write, a hardcoded
clock replacing the derivation, and a write aimed at the wrong bank — and each
was caught, the last by two separate assertions.

### Test behaviour, not implementation

- **No source-parsing tests.** Assert what the chip or the caller ends up with.
- **No tests for removals.**
- Tests set the state they need rather than saving and restoring it.

### Say so when a fix cannot be tested

Several reliability fixes here have no acceptance test because their trigger is
not reachable over HTTP — the preference wipe fires only from the OSD and IR
handlers. That is an acceptable answer, stated in the commit. Shipping untested
and letting the next session assume coverage is not.

## Layer-specific gotchas

- **Filesystem access blocks the firmware loop.** `/fs/dir` calls `delay(1)` in
  a loop; hammering it can make the sync watcher see instability.
- **`/getreg` timing out while `/freeze` answers is a signal, not a dead unit.**
  Register access is deferred to `loop()`, so a read blocks while `loop()` is
  busy — usually inside detection's 6000 ms `getVideoMode()` sweeps. That
  distinguishes "wedged" from "hunting", which are opposite diagnoses.
- **Discard ~6 s after any mode change** before judging a sample. Raw sampling
  across a sweep produced 15 false positives in one run.
- **Know what else is talking to the unit**, so a result can be read in context:
  `ss -tanp | grep <ip>` for what is connected, and
  `ps -eo pid,etime,cmd | grep -E 'soak_watch|regpanel|sweeplog'` for what
  started it. A panel left open is not a fault; a polling loop changes what the
  unit is doing between reads, which is the part worth knowing.
- **`test_the_sampling_divider_is_one_quantity_in_three_registers` fails in the
  suite and passes alone.** State leaks between the `--source` tests. It presents
  as a divider fault on the unit and is not one — re-run it by itself before
  believing it. Unfixed.
- **The picture is the only judge of a picture.** No register distinguishes "the
  output clock is not running", "the encoder has stopped" and "the TV timed
  out". When a bench result is six clean readings in a row, run a **positive
  control** — deliberately break something in the same path and confirm the
  symptom appears — or the clean readings may only mean nothing you touched was
  in the path.
