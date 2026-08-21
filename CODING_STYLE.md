# Coding style

Conventions introduced 2026-08-09, while untangling the geometry engine. They
are here because every one of them was arrived at by being bitten, and the bite
is recorded next to the rule so nobody has to relearn it.

This is a **C++** project. Most of it does not read like one yet. New and
rewritten code follows this; old code is converted when you are in it anyway,
not in sweeps.

## Use a class

State and the operations on it go in a class, with the state **private**.

```cpp
// no
namespace Geometry { struct Axis { float startConst; ... }; }
static Tv5725::PanAndZoom geometryFraming;       // mutable static IN A HEADER
inline RasterFit fitToRaster(uint16_t capture, uint16_t raster, const Axis &axis);

// yes
class Axis {
public:
    RasterFit fitToRaster(uint16_t capture, uint16_t rasterTotal) const;
private:
    float startConst_, startPerMag_;
};
```

**Why.** A namespace of free functions makes you pass the data alongside every
call, which is more code and harder to follow — `fitToRaster(c, r, AxisHorizontal)`
says less than `AxisHorizontal.fitToRaster(c, r)`.

And the globals are a real defect, not a taste question. `geometryFraming`,
`geometrySolvePending` and `geometryFramingRequested` were mutable `static`s in
a header, so **every translation unit gets its own silent copy**; the only
reason that was not already a bug is that the sketch is a single translation
unit. Worse, the invariant that made the engine correct — *the framing is the
truth and every register is an output of it* — was enforced by convention alone,
and three separate OSD implementations wrote those registers behind it. The
`/geometry` handler took raw `int16_t*` into the struct's fields.

Behaviour that belongs to a value belongs **on** that value. An axis knows its
own write-start model and solves itself.

Reach for a free function only for genuinely stateless arithmetic that belongs
to no type.

## Minimal object orientation, and no more

No inheritance, no virtual functions, no templates, unless something concrete
needs them. Plain value classes with inline non-virtual methods cost **nothing**
at runtime or in flash.

Deep hierarchies and template metaprogramming make low-level concerns
unreviewable, which on an ESP8266 with 80 KB of RAM is the thing you least want
to lose sight of. `tw.h`'s register machinery is the project's entire template
budget. `Tv5725.h` spends it through one alias, `UReg`, and adds none of its own —
the chip is a plain class, because there is only ever one of it on the board.

**The one inheritance in the driver is `GBS`, and it is deliberate.** It is a
mixin — `class GBS : public Tv5725::Tv5725, public Tv5725::VideoProcessor, …{}`,
one base per subsystem — aggregating names and nothing else: no state, no
constructors, no virtuals, and a byte-identical binary either side of it, so it
costs nothing.

It exists because registers migrate out of `Tv5725::Tv5725` into the subsystem
that owns them a block at a time, and `GBS::VDS_HSCALE` has to keep resolving
while its callers still say that. Each migration adds one base; the class is
deleted when the last caller names its owner directly. **Removing it early breaks
every unmigrated call site at once**, so it goes when the base list is empty, not
when someone notices the inheritance.

Ambiguity is not a risk worth guarding: the field names are disjoint, and the
compiler rejects the build the moment two subsystems claim the same one — which
is a better check than the flat catalogue ever had.

## `src/tv5725/` is the driver, and the name is the boundary

Renamed from `src/tv5725/` on 2026-08-09, because the old name was refusing
work that belonged there. `PLLAD_MD` sets the ADC sampling rate, `PB_FETCH_NUM`
sets the memory read burst — neither is "geometry", both are computed from the
same model, and both were left inherited from preset tables for exactly that
reason. Every fault of that evening was in the inherited set.

**It is a device driver, not a geometry engine**, and naming it one is what
stops other registers feeling out of scope. The DRM comparison settles it:
display drivers compute PLL dividers, blanking and memory FIFO watermarks, and
`PB_FETCH_NUM` is a watermark. Three layers result:

| layer | what | where |
|---|---|---|
| bus | how to get a byte to the chip: banking and the queue | `src/net/RegisterQueue`, `tw.h` |
| **driver** | **what the chip needs: sampling, capture, memory, playback, output timing** | **`src/tv5725/`** |
| board | anything spanning two devices, or not the TV5725 at all | the sketch, for now |

The bus layer is `regmap`, not the driver — a driver is the thing that knows
what the device needs.

**The boundary is what the TV5725 can decide alone.** `ADC_INPUT_SEL` is a
TV5725 register and belongs here; *selecting an input* does not, because
`ASW_01`-`ASW_04` live on the HC32F460, are reached over UART and are
write-only. Two muxes in series, two microcontrollers. No TV5725 driver should
know about a UART to another chip — see CLAUDE.md, "The system has three
control domains".

That is a boundary with a reason behind it, which is what `geometry` lacked: it
excluded `PLLAD_MD` on the strength of a word.

### The chip is `Tv5725::Tv5725`, a class inside the namespace of the same name

`Tv5725.h` declares the chip as a class; the subsystems are its siblings in
`namespace Tv5725`, and each declares the registers it owns. `GBS` is the flat
view of all of them, kept so the legacy call sites still compile, and it goes
when they do.

**Inside `namespace Tv5725`, the injected class name shadows the namespace.** A
sibling written `Tv5725::SourceMeasurement` resolves to the *class* and fails
to compile; write `SourceMeasurement`, or `::Tv5725::SourceMeasurement` where
the qualification is wanted.
Outside the namespace both `Tv5725::Tv5725` and `Tv5725::VideoProcessor` resolve
as expected, including after a `using namespace Tv5725;`.

Registers migrate out of `Tv5725::Tv5725` into the subsystem that owns them, so
what remains in it is whatever has no owner yet. `docs/chip-initialisation.md`.

## One class per file, named after the class — `ClassName.h`

`src/tv5725/Scale.h` holds `Tv5725::Scale` and nothing else, and
`src/tv5725/Scale.cpp` holds its definitions. If you are looking for an
implementation, it is in the file named after the class.

**The file name IS the class name, character for character.** `Axis.h`,
`AxisSolution.h`, `ControlSteps.h`, `HoldRamp.h`, `IrReceiver.h`. That is
Arduino's convention — `WiFiClient.h`, `HardwareSerial.h`, `OLEDDisplay.h`, and
the Arduino Library Specification — and it is already what `OSDManager.cpp` and
`OLEDMenuManager.cpp` do here. The geometry was briefly `axis_solution.h` in
Google style, which made the rule above *approximately* true instead of literally
true, and left the tree using two conventions at once -- half the files
capitalised and half underscored.

**A file that is not a class stays lowercase.** `gbs_types.h` is a typedef, not
a class, and the lowercase name is what says so at a glance. `Tv5725.h` is
capitalised because it holds `Tv5725::Tv5725`, and the rule above applies to it
like any other class.

**There is no umbrella header, and adding one back is a regression.** Every file
includes the classes it names. `src/tv5725/driver.h` used to include thirteen
headers on its callers' behalf, and what it really did was hide dependencies:
`Geometry.cpp` used `Memory` without including it, and the sketch reached
`ControlSteps` through two levels of someone else's include. Both compiled until
the umbrella went. A header that saves a caller from naming what it uses also
stops the caller from seeing what can break it.

## `src/` means reviewed; the sketch root means legacy

A file moves into `src/` as it is cleaned up; a file still in the sketch root
has not been reviewed.

That is the whole convention, and it is worth more than tidiness: it makes the
mess **countable**. A file's directory tells you whether anyone has been through
it since the fork. Move a file into `src/` as part of cleaning it up, never as a
sweep — an untouched file moved to `src/` is a lie about its state.

**`gbs-control.ino` stays at the root, and that is not a choice.** Arduino
requires the sketch folder to contain a `.ino` named after the folder; that file
is what arduino-cli is handed. So the end state is exactly one file at the root —
the `.ino` as composition root — and everything else under `src/`.

**The other Arduino constraint, also not a choice:** arduino-cli compiles `.cpp`
files from the sketch root and `src/` **only** — `src/` recursively, so
`src/tv5725/*.cpp` builds. A `.cpp` in any *other* subfolder is not built.
The vendored `3rdparty/` tree is the standing demonstration: the build produces
no object for `3rdparty/PersWiFiManager/PersWiFiManager.cpp` or
`3rdparty/WebSockets/src/*.cpp`, because the copies that actually compile are
the ones at the sketch root and in `src/`. Verify with
`find build/output/sketch -name '*.o'` after moving anything.

## Declare in the header, define in the .cpp

The definition goes in a `.cpp`. Headers declare. This is the rule, not a
preference to be weighed each time — a header full of bodies gives every
translation unit its own copy of everything, makes rebuilds expensive, and hides
the ODR hazards that mutable and non-`extern` file-scope entities bring with
them.

**It costs flash, and the cost is the price of the rule, not an argument against
it.** Measured across the extraction on 2026-08-09, geometry header-only →
`.h`/`.cpp`: **829,112 → 830,056 bytes, +944**, with globals down 48. An earlier
note here predicted the opposite — that moving the bodies out would give back the
~600 bytes the class refactor cost. That was a guess and it was wrong: these are
one-line accessors called from one or two places each, so inlining them was
already cheaper than a call. On a part with 215 KB of flash spare, correctness
and reviewability win. **Do not re-argue this from the size number.**

Two things that only appear once the bodies move out, both of which bit here:

- Instances belong in the `.cpp` with an `extern` declaration in the header.
  `static const Axis AxisHorizontal(...)` in a header was one silent copy per
  translation unit; it is now `extern const Axis AxisHorizontal;` and one
  definition in `Axis.cpp`.
- An in-class initialiser is **not** a definition, so `static const uint16_t Max
  = 1023;` needs `const uint16_t Scale::Max;` in a `.cpp` — **but only if
  something ODR-uses it**, meaning binds it to a reference or takes its address.
  Passing it by value does not. Do not add these speculatively: a whole
  `control_steps.cpp` was written containing three such lines and nothing else,
  and deleting it changed neither the host tests nor the binary by one byte --
  a whole file with no actual code in it. Add one when the linker asks for it,
  and its error names exactly which.

  **A test framework will ask where the firmware never does.** doctest's `CHECK`
  decomposes its expression by binding each operand to a `const &`, so
  `CHECK(previous == HoldRamp::MaxMultiplier)` ODR-uses a constant that every
  line of firmware only ever reads by value. Both `Scale.cpp` and `HoldRamp.cpp`
  carry definitions for exactly the constants the tests compare, and for no
  others.

**A class with no out-of-line definitions needs no `.cpp`.** `ControlSteps` is
three constants; `ControlSteps.h` alone is the whole class. "Define in the
.cpp" is about *bodies*, not about pairing every header with a file.

**The host test build globs `src/tv5725/*.cpp`** in `test/Makefile`. Glob it,
never write the list out: a new class needs nothing added, but a new class that
must NOT host-compile has to join the `Geometry|Controls` exclusion.

## Inject dependencies, by reference, from a composition root

Collaborators are **handed** what they need rather than reaching for a global by
name. It decouples the pieces and, more usefully here, makes them testable — a
test can give `OSDManager` its own `Tv5725::Controls` and press every bar
without a board.

**Reference, not pointer.** A reference cannot be null and cannot be rebound, so
the dependency is mandatory and fixed *in the type*, and no caller has a
missing-dependency branch to get wrong. Take it in the constructor and store it
as a reference member:

```cpp
explicit OSDManager(Tv5725::Controls &geometry) : geometry_(geometry) {}
```

A pointer is right only when the dependency genuinely can be absent or replaced
at runtime. "It might not be wired yet" is not that case — it is a symptom of
wiring in the wrong place.

**The `.ino` is the composition root.** Instances are declared there, in one
translation unit, and injected downward. Headers declare *types*; they do not
declare or define instances.

```cpp
Tv5725::Geometry geometry;
Tv5725::Controls geometryControls(geometry, reportGeometryAdjust);
OSDManager osdManager(geometryControls);
```

Declaration **order is the wiring**, and one translation unit is what makes that
safe: objects are initialised in the order they appear, so each is ready before
whatever references it. Globals in *different* translation units are initialised
in an **undefined** order, so a global constructing from another global across
files is the static initialisation order fiasco — it works until one day it does
not. Keeping the root in one file removes the problem rather than working around
it with function-local statics.

Never `static SomeType instance;` in a header: every translation unit silently
gets its own copy.

## One namespace per module, and everything in it

`Tv5725::Scale`, `Tv5725::CaptureWindow`, `Tv5725::RasterFit`,
`Tv5725::RegisterSolution`, `Tv5725::Axis` — names that generic would collide in a sketch that also pulls
in Arduino, an OLED driver and WebSockets. That is what the namespace is for and
it earns its keep.

**What does not earn its keep is using it for half the module.** The engine, its
controls and its capture once sat *outside* the namespace and paid a `Geometry`
prefix instead, so one module had two mechanisms for one job. They are
`Tv5725::Geometry`, `Tv5725::Controls` and `Tv5725::Capture`, and the stutter is
gone.

A namespace is all-or-nothing. If a type belongs to the module, it goes in.

**The exception is a free function the `.ino` defines**, like
`getSourceFieldRate()`: its definition is at global scope in the sketch, so a
declaration inside the namespace resolves to a different function that does not
exist, and the failure is at link time.

## One name for one thing

The `GBS` typedef was written out in several files at once. It now lives in
`gbs_types.h` and nowhere else. That duplication is *why* the geometry had to be
header-only: no `.cpp` could name `GBS` without repeating it.

The same rule at the behaviour level: there is **one entry point** for a user
geometry adjustment — `Tv5725::Controls::panH` / `panV` / `zoomH` / `zoomV`. The
web pads, the OSD bar and the IR menus all land there. When there were three
ways in, two of them bypassed the engine and the acceptance suite was green
against a broken picture for a whole evening.

## Comments are pointers, not essays

**The default is no comment at all.** A densely commented file is a defect rather
than a matter of taste: it reads as machine-written, and it says its author
thought the code was unreadable. The *why* — a hidden constraint, a measurement,
a bug that shaped the code — belongs next to the code, briefly, where a name
cannot carry it. The reasoning belongs in the tests and in `docs/`. Prefer
extracting a well-named function over explaining an unnamed one.

**Keep the process out of the code.** What we tried, what was refuted, which
session measured it, how the bug was found: none of that goes in a source file.
A code comment is one or two lines of *why*, and a pointer to the `docs/` page
carrying the detail. Detailed explanations go in the doc files.

This applies to commit messages too — a feature commit says what the feature does
and why, and nothing else. **Keep the word count as low as possible.** Generated
prose runs long by default; the discipline is cutting it.

## Docs describe this codebase, in the present tense

Write for someone arriving with no history. They want to know what the code does
now — not what it used to do, not what some other tree does, not what changed and
when. Nobody arriving here cares what other forks do; they are interested in what
THIS code base does.

So: no "the fork silences…", no "restored from", no "upstream ships…", no "so
far". State the behaviour. If a reader needs the history, `git log` has it.

```
no   The fork blanked its debug output with a search-replace that turned
     every SerialM.print(...) call into `; // SerialMprint(...)`.
yes  Most debug output is inert. SerialM.print(...) calls are written
     `; // SerialMprint(...)`, with the identifier mangled so un-commenting
     one does not compile.
```

**A count is a fact that rots.** "310 calls" was wrong within a few commits and
wrong when written. Give the command that answers it — `grep -c SerialMprint` —
or pin the number to something fixed, like a released version.

The exception is a page whose *subject* is an investigation: `docs/investigations/`
exists to carry refuted hypotheses, and that history is its content rather than
narration around it.

## Host unit tests are doctest

`test/test_axis.cpp` and `test/test_hold_ramp.cpp`, run by `make -C test`
(`make -C build test` still works and delegates there). The flake supplies the header; nothing is vendored and
nothing is linked.

Chosen 2026-08-09 over Criterion, which the rpcemu project next door uses —
these are C++ and doctest is the C++-native one — and over GoogleTest, which is
a library to link and a build system to feed for a suite whose whole point is
compiling in a second.

**What it replaced, and why that mattered:** a hand-rolled harness in which
`main()` called each test function by name. Nine in one file, seven in the
other, every one of them a line somebody had to remember to add — so a test you
wrote and forgot to wire up silently never ran, and reported success. doctest
registers them itself. The migration preserved the assertion counts exactly,
145 and 65, which is how it was checked.

**`--dump` is intercepted before doctest sees `argv`.** The three suites that
double as oracle generators below exit non-zero on an option doctest does not
recognise, so `test_axis.cpp`, `test_active_image.cpp` and
`test_register_solution.cpp` use `DOCTEST_CONFIG_IMPLEMENT` with their own
`main` while every other suite uses `..._WITH_MAIN`.

Two things to know when writing an assertion:

- **`&&` and `||` need an extra pair of parentheses.** doctest cannot decompose
  them and says so at compile time: `CHECK(((a < b) && (c < d)))`.
- **There is no absolute-tolerance assertion.** `doctest::Approx`'s epsilon is
  *relative*, and every tolerance in the geometry is a count of pixels. Hence the
  shared `CHECK_NEAR(got, want, tol)` macro in `test/CheckNear.h`, over
  `CHECK_MESSAGE`.

## Prove a refactor changed nothing

A behaviour-preserving change must be **shown** to be behaviour-preserving, not
argued to be. `test_axis`, `test_active_image` and `test_register_solution`
each take `--dump` and print their share of an oracle grid; diff all three
across the change and they must be byte-identical.

`test/test_geometry.cpp` is the other half of the proof, and asks a different
question. It drives `Tv5725::Geometry` at the top and asserts every field that
reaches the chip **by name, against an expected value** — so it says whether the
values are right, not merely whether they moved, and a stray write to a register
no expectation names fails as well.

If a refactor cannot be checked like that, say so in the commit.

## See also

- `CLAUDE.md` — the project's operating knowledge and traps
- `docs/firmware-geometry-engine.md` — how the engine uses the model
- `.claude/` memory — standing preferences behind several of these rules
