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
| **write trace** | a `GBS_TRACE_WRITES` build, read over HTTP | a running unit | ~1 min a load |
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

A `GBS_TRACE_WRITES` build records every register write into memory and serves
it at `/writetrace` as CSV -- `segment,index,sinceArmMs,reg,length,payload`.
Writes to 0xF0 aim the segment window rather than becoming entries, so each
entry carries the segment it was taken under and a slice means something on its
own.

```sh
curl 'http://<ip>/writetrace?arm=1'    # record from here, discarding what is held
curl 'http://<ip>/writetrace?stat=1'   # held, seen, capacity, overflowed
curl 'http://<ip>/writetrace'          # the entries, oldest first
curl 'http://<ip>/writetrace?first=40&last=96'          # issue that slice again
curl 'http://<ip>/writetrace?first=40&last=96&gaps=1'   # with the recorded spacing
```

**IT RECORDS INTO MEMORY RATHER THAN OUT OF A PORT**, and that is the point
rather than a convenience: `Serial.print()` costs ~90 us a byte inside every I2C
write, which reorders the very sequence being measured. It costs `Capacity` x 10
bytes of `.bss` -- 5120 at 512 entries -- so check `/bootlog`'s `free heap:`
against `CONSOLE_BROADCAST_MIN_HEAP` after flashing one.

**THE RING DOES NOT WRAP AND THE DUMP DOES NOT SAY SO.** Past `Capacity` it
keeps the oldest and silently stops. `?stat=1` is the only thing that answers
it: `seen` above `held` means the tail is missing. Arm immediately before the
event.

**512 entries does not hold one transition.** Measured on `vga` at 640x480@60,
the return leg of a pass-through round trip -- one `/uc?x` -- offers **950 to
1500** writes, so the default ring keeps the first third and drops the rest. A
capture that has to span a whole transition needs `Capacity` raised, and that
comes out of a free heap the console's broadcast gate already sits close to: the
trace build measures 14000 bytes free at boot against a gate of 8000, dipping to
**9064** under back-to-back HTTP requests.

**A REPLAY FROM THE HOST IS NOT THE SAME EVENT.** `/setreg` is deferred to
`loop()` and lands at tens of hertz whatever bytes it carries, which is why
replay runs on the device. Its limits are real: entries longer than four payload
bytes are skipped, replay writes whole bytes so it clobbers neighbouring bits
that have moved since recording, and timestamps are `millis()`, so `gaps=1`
reproduces spacing no finer than a millisecond and reproduces no pulse width at
all.

**IT IS A READING, NOT AN ORACLE.** A capture-and-compare facility sat here --
a route that forced `rto->videoStandardInput` so a per-standard branch could be
reached on a bench with no source for it, a collector, a comparator and 64
fixtures. It went with the byte: there are no per-standard branches for it to
reach. What it learnt that is still true of any trace comparison:

- **A trace that does not reach the code does not fail, it passes.** The diff
  comes back empty either way, so check the capture carries a fingerprint of the
  code under test before believing one.
- **Check the run lengths agree first.** A noisy capture drops writes from its
  longest common subsequence and those read as "only after". A 139-write
  baseline against a 161-write after produced 20 differences that were nothing
  of the kind.
- **Bracket the code under test.** Where the change is a move, what is decidable
  is the slice between the write before the moved code and the one after it.
  Everything outside is the settle loops and the live measurements.
- **Two captures of the same build disagree**, so re-capture the after and diff
  it against itself before concluding a change moved anything: measured at 713,
  738 and 713 writes across three captures of one branch, two of them the same
  binary. `PA_SP_S` is the phase sweep's live output and `PAD_SYNC_OUT_ENZ`
  follows whatever ran before, so both move between sessions.
- **Every load starts from what the last one left**, and the writes are
  read-modify-write, so a load after a boot and the same load after another
  disagree on real bytes.

## Host unit tests

`test/*.cpp`, doctest, one binary per subject, each a target in `test/Makefile`.
They live in `test/` and not `build/` because `build/` is the arduino-cli driver
and a test is not a build artefact.

Two kinds:

**Pure arithmetic** — `MemoryWindow`, `SdramTimings`, `OutputMode`, `SourceMeasurement`,
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

### Poison is an INPUT to anything the code measures

The section above is about poison catching a write that never happened. The
mirror case is poison being *read* as a measurement, and it fails the other way:
silently, with a plausible answer.

`SourceMeasurement::measureLineRate()` reads `HPERIOD_IF`. Poisoned, that
register holds the poison byte, which converts to a line rate through
`27e6 / ((h + 1) * 4)` -- and for both poisons in use here the result lands
inside the band a real source occupies. `0xE2` is 226, which is 29735 Hz, a
plausible 95 Hz over 311 lines. So a case injecting a field rate through the
`getSourceFieldRate()` seam had that injection **ignored**, because the engine
preferred a measurement it appeared to have.

Three suites failed this way one at a time, each hidden behind the one before,
and every failure read as a geometry error rather than as a seeding gap.

So **a register the code measures from has to be seeded, not poisoned**, and the
seeding belongs beside the poison rather than in each case. `poisonChip()` in
`SolvedEngine.h` is that: it poisons and then blanks `HPERIOD_IF`, which means
nothing measured, so the injected rate is used. A case that wants a measured
rate seeds the register with the value the mode implies.

The general rule, and it will bite again as more of the engine measures rather
than assumes: **poison what the code WRITES, seed what the code READS.** Adding
a measurement to a subsystem makes every test that seeds a source responsible
for the new register, and nothing warns you -- the value is plausible, the test
compiles, and the assertion that fails is somewhere downstream.

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

**GO THROUGH THE PUBLIC INTERFACE, AND ONLY THAT.** Set the class up the way the
code base sets it up, then check what comes out -- for this firmware that is
usually the registers, because the registers are the behaviour. A test that
reaches past the interface pins the route rather than the result, and the route
is what a refactor is entitled to change.

**Widening an interface to let a test in is the defect, not the fix.** If a step
has to be made public to assert on it, that is the signal to assert on the
result instead. An arithmetic helper made public "for the tests" was how nine of
`OutputWindow`'s private steps ended up on its face, and the comment saying so
was the smell admitting itself.

**An intermediate either reaches the registers or it does not.** If it does, the
result assertions already cover it and the step assertion is redundant; if it
does not, it does not matter. Either way the step assertion earns nothing --
and it costs, because it reads as coverage. A case asserted
`originOffset(AxisVertical, 2.0f)` was within 1.0 of 2 when the value is 1.8:
the vertical write-start constant could be moved from 0.8 to 0.9 and the
assertion still passed. A test that cannot fail for the thing it names is worse
than no test, because it stops anyone writing the one that can.

**What replaced it is the shape to copy.** The suite states the bench reading
itself -- the write origin is 55 + 25m horizontally and 0.2 + 0.8m vertically --
predicts the registers from it, and compares. A constant that moves in the
firmware then disagrees with the measurement it came from, where asking the
class for the value compares the model with itself. The readings stay as a case
of their own, checking the stated model against the numbers it was fitted to,
so the chain from a bench measurement to a register has both links tested and
neither is a tautology.

**A sweep is what catches a small constant.** Vertically 0.8 against 0.9 is a
tenth of a line and only some magnifications round across it, so a single
magnification proves nothing.

**Prefer socialised tests to isolated ones.** `SolvedEngine.h` builds a real
`VideoPath`, `InputFormatter`, `SourceMeasurement` and `FramingTable` over the
fake bus and drives the acquisition path, so a case says "this source, this
output mode, these presses" and asserts `Wire.field(...)`. Real collaborators
catch what a stubbed one cannot: the seam between two classes is where the
engine's faults have actually been.

**A behaviour-preserving move is not proven by a `--dump` oracle alone.** The
oracle re-runs the cases it already had; it cannot see a branch none of them
enters. Moving the placement arithmetic off `Axis` turned `writeFloorBinds()`'s
`<=` into `<`, and 55 suites stayed green with the dump byte-identical, because
nothing sits on the boundary the two disagree about. **Mutate the moved code and
confirm the mutations are still caught** -- that is what found it, on the first
run.

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
