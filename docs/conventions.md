# Conventions

**EVERY DIFFERENCE MUST BE DELIBERATE, AND THE GOAL IS THE BEST PICTURE WITH THE
LEAST MECHANISM.** This is the point of the whole refactor and it is easy to
mistake for tidying, so it is stated first.

Where two paths do the same job differently, the question is *why*, and **"nobody
knows" is the answer that means collapse them**. An unexplained divergence is not
a risk to preserve carefully -- it IS the complexity, and preserving it because
removing it might change something is how it has survived this long. That
instinct reads as caution and is the opposite: it guarantees the next reader
inherits the same unanswerable question.

The worked example is the bypass route. Two existed, the second had no reason to,
and it went -- one route, less code, and the picture measured indistinguishable.
`docs/investigations/one-bypass-route-carries-rgbhv.md`.

So: keep a difference that has a reason, and write the reason down. Delete one
that does not. A faithful refactor of an accident preserves the accident.


**CONFIGURE EXPLICITLY ON THREE EVENTS. "PRESET" IS LEGACY VOCABULARY AND THE
CONCEPT GOES WITH THE WORD.** Each subsystem is configured on exactly three:

| event | owner |
|---|---|
| app boot | `Tv5725::<Subsystem>::init()` -- constants only, no arguments |
| input source switch | `VideoSourceSelector` -- mux, colour space, stored ADC calibration, sync arrangement |
| mode change, source or output | the engine, `VideoPath` / `VideoSourceAcquisition` -- everything derived from a measurement, plus the ordered restart |

**The duplication is where the bugs live rather than untidiness**: two ways the
chip gets configured means some path reaches a picture through one and not the
other. A flat-field boot was exactly that -- `ResetSDRAM()` was a free function
only a preset load reached, so a boot whose detection claimed the source on its
first pass never ran it, 6 of 6 reproducible, and moving it onto the mode change
cured it. **So when a register looks missing, ask which of the three events
should have written it and whether that owner exists** -- do not bolt the write
onto whichever function happens to run, and do not reach for a register diff
first, which is what produces a partial fix in the wrong place.

**THE ENGINE HAS TWO ENTRY POINTS AND NO OTHER WAY IN.** An input source is
selected -- which measures the source and then CALLS the output setup, so it is
a measurement in front of the other method rather than a second implementation
of it -- and the output is set up, from state held. Anything that is neither is
a competing path: the preset load, a detection sweep that re-decides an output,
a resolution ladder in a menu handler. **`rto->videoStandardInput` does not
survive this**, being replaced by the pair that decouples the two concepts, the
input mux selection and the output mode resolution. One number currently carries
which input, which standard family, whether scaling RGBHV is on and whether the
part is bypassed, and every place that hurts is that conflation. Judge a change
by whether it reduces the number of ways the output gets configured. Bring-up
stays outside the engine, being per chip ARM rather than per event; bypass folds
in as an `OutputMode` rather than a branch that skips the engine; and what the
source MEASURES belongs to `Tv5725::SourceMeasurement`, so folding it back into
the input selection just recreates `videoStandardInput` under a new name.

**KNOW EVERY FACT BEFORE SETTING UP. MEASURING AND CONFIGURING MUST NOT
INTERLEAVE.** Learn everything about the arriving mode -- sync type, count,
scan, line rate, duty -- and only then set the subsystems up, once, because a
value chosen partway through is chosen through whatever the previous mode left
in force and nothing revisits it. **The coast is the worked example**: the
pre/post pair is chosen in `VideoPath::measureVerticalInterval()` before the
rate is known while the coast WINDOW is placed in
`VideoSourceAcquisition::holdSolvedSource()` after it, so a mode change that
moves the line rate judges the pair through the old mode's window. Where a
measurement genuinely needs the chip configured, use a known reference for the
measuring pass and set the real value in the one setup afterwards.

**THE SKETCH WRITES NO REGISTERS.** Every `GBS::X::write` in `gbs-control.ino`
migrates to the `Tv5725::` class owning the field, and the sketch keeps
orchestration. A field written from both has two owners and the sketch's write
usually runs last and unconditionally, so it silently overrides a derived value
and no check catches it, because checks compare names rather than bits. Prefer
passing facts in as parameters over adding a callback or an include back to the
sketch -- **the sketch only shrinks**. The OSD and web code is most of it by
line count and holds few writes, so that part is readability rather than
correctness.

**DERIVE THE VALUE. NO NEW LADDER RUNGS.** A fault is fixed by deriving the
right value, never by adding a recovery step that tries one and backs off.
Competing rungs are what the rewrite exists to delete: each can undo another's
work, and no reading says which one put the chip in the state being looked at.
When a derived value is wrong for some source the suspect is the derivation, or
a quantity it depends on that is not configured yet when it runs -- find that
and fix it in one place. Where a measurement is genuinely needed, take it at the
point in the sequence where everything it depends on is in force, and choose
once.

**BUT RECOVERY RETRIES AND NEVER GIVES UP.** The engine must never stall: a
source not yet acquired is one to keep trying to recover, so every recovery
repeats at some cadence and nothing latches into a state that asks for nothing
further. The expensive failures here are all "it stopped trying" rather than "it
tried the wrong thing" -- a run that treated an ambiguous pass as neither
evidence so it never reached its threshold, and a teardown gated on a flag
cleared only by success, so a source that never succeeded got exactly one
attempt. Each looks prudent alone and each turned a slow recovery into none,
measured at `state: absent` holding 150 s and 90 s with the source present
throughout and cleared only by a hand-run `/sc?~`. **Re-arm whatever counted up
to a recovery rather than marking it done**, act on the threshold every time it
is reached, and treat "tried once already" as a reason to wait and never a
reason to stop.

**CONTROLS HOLD STATE, NOT DELTAS, AND TESTS SUPPLY THE STATE THEY NEED.**
A delta applied to whatever the registers hold makes the step size depend on the
current value, so a fixed step is a different proportion at every zoom level and
reversibility rests on rounding cancelling; an integer step count as the state is
exactly reversible by construction. **State living outside the registers is not a
problem to solve** -- hold the user-facing quantity, derive every register from
it, and expose it over `/geometry` so tests and presets can read and set it. And
once registers are an output, a test that saves and restores registers restores
nothing, because the next press recomputes from state nobody reset -- which let
eleven zoom steps accumulate across a suite while each test believed it had made
one press. Set the state at the start of a test; do not add a save-and-restore
fixture. Applying such state from an `ESPAsyncWebServer` handler runs it in a
network-stack callback rather than `loop()`, so anything that measures or spins
reboots the unit -- flag it and let the loop apply it.

**`CODING_STYLE.md` is the C++ style, and it is not optional.** Classes rather
than namespaces over file-scope globals, one class per file named after it,
declare-in-header/define-in-.cpp, minimal OO strongly preferring composition over
inheritance and using no virtuals, `NULL` for a null pointer rather than `0` or
`nullptr`, dependency injection over reaching for globals, a default of no
comment at all,
and a behaviour-preserving refactor proven by diffing the `--dump` oracles.
Every rule in it is there because it cost a session. Read it before writing
firmware C++.

- **The default is NO comment, and a densely commented file is a defect rather
  than a matter of taste.** Commentary reads as machine-written, and a heavily
  commented file says its author thought the code was unreadable. Nothing checks
  it, so removing it is a pass of its own —
  `grep -c '^\s*//' <file>` against `wc -l` is the measure. Write one or two
  lines of *why* only where a constraint is genuinely hidden, put anything
  longer in `docs/` with a pointer to it, and remember that extracting a
  well-named function beats explaining an unnamed one. `CODING_STYLE.md`,
  "Comments are pointers, not essays".
- **A detailed comment is a COPY of a fact, and copies diverge silently.** This
  is the concrete harm, and it is not hypothetical. **The more detail a comment
  lists, the more likely it is to go stale, and the same misinformation then
  spreads through the code base.** A single document can be reviewed and updated
  as understanding changes; *n* copies cannot. Two examples found in one pass:

  | the fact | the copies |
  |---|---|
  | what causes the tail green at IF 1126 | `VideoSourceLine.h` and the host tests both asserted *"it is the source's blanking"* — which `docs/scaler-geometry-model.md` had already **refuted** by measurement, and carries as an open question |
  | the horizontal zoom ceiling | `test_geometry_pads.py` said `1024/500 = 2.048x` against what `test_axis.cpp` asserts, `Scale::Min` having moved |

  One fact, three copies, two wrong, and every test passed — because tests check
  code, and nothing checks prose. A single doc can be reviewed and corrected in
  one place; *n* copies in source cannot, and each one reads as authoritative to
  whoever finds it first. **So the length of a comment is a good predictor of
  how wrong it will be**: put the detail in `docs/`, leave a pointer, and let
  the doc be the thing that gets updated.
- **NEVER quote the user. State the requirement instead.** No name, no date, no
  speech marks — in source, tests, tooling, `docs/`, **and in this file and
  `CODING_STYLE.md` too**. These are the rules for the project, not one person's
  opinions recorded with a byline. Three costs:

  - **An attribution reads as scare quotes.** A rule with a name on it says *"I
    don't agree with this, but I was told to"* — the writer standing apart from
    the rule instead of stating it. Whoever reads it next inherits that doubt
    about a decision that was never in doubt.
  - **It freezes a live requirement** at the moment it was said, so it reads as
    history rather than as the spec it still is.
  - **It gets repeated verbatim rather than updated**, so it outlives the
    requirement it describes — which is how a scale floor of 500 reached four
    files and stayed there after it became 256.

  *"One unit of zoom is one pixel of the output screen"* is the comment. A
  decision worth keeping is recorded as a decision.
- **"Narrative" means anything that only makes sense to someone who was in the
  room.** That is the test — not whether it is long, and not whether it is true.
  Text that means nothing to someone who was not part of the investigation
  fails it: the reader does not want the mis-steps, only the current shape of the
  code and why it is like that. It also ages worse than anything else in the file, because it describes a state
  no reader can check any more. The recurring forms, every one of them found in
  this tree:

  | form | example |
  |---|---|
  | defending a signature | *"injected so a test can hand it a different one"* |
  | defending the default style | *"a class rather than statics in this header"* |
  | counterfactual history | *"used to be maintained from one"*, *"was reverted"* |
  | an unsourced duration | *"it did not look like one for two years"* |
  | a quote from a session | *"can you create a top level folder…"* |
  | the test build, from production source | *"doctest's CHECK binds each operand"* |
  | a path that rots | `test/test_hold_ramp.cpp`, `snapshots/CLEAN-*.json` |
  | a count that rots | *"all 107 decode sites"* |

  **What survives is what the reader cannot derive**: a datasheet contradiction,
  an ordering constraint, a trap with a live consequence, and a "do not
  reinstate this" guard. A failure mode stated in the present tense is not
  narrative — *"writing the divider after the latch leaves the PLL on the old
  value"* is a live warning; *"we wrote it after the latch once and it took an
  evening"* is a diary.
- **A MEASUREMENT BELONGS WITH THE TEST THAT PINS THE BEHAVIOUR, NOT IN THE
  PRODUCTION COMMENT.** Nothing ever forces a production comment to be re-read,
  so it diverges in silence — which is the whole complaint above, and the rule
  that merely asks for shorter comments does not prevent it. A test comment has
  a forcing function: the behaviour it describes stops holding and the test
  fails, so the comment is read exactly when it is wrong, and is then revised or
  deleted with the test. **The suite is what keeps the evidence honest.**

  The codebase already proves it. `scale_step` takes no `scale` argument and a
  test asserts the parameter does not exist, because its existence was the bug —
  a rationale that cannot rot, because deleting it means deleting the guard.

  | the context is | it belongs |
  |---|---|
  | the contract — what it does, its units, its absent case | the declaration |
  | why the behaviour is required, and the measurement that decided it | **the test that pins it** |
  | what was tried, what was refuted, the figures in full | a `docs/` page, cited by name |

  So a constant carries one line of what it is and a pointer; the run counts,
  the shares of samples and the per-mode tables go to the test, and anything
  spanning several behaviours goes to `docs/`.

  **WHERE NO TEST CAN PIN IT, THE EVIDENCE STAYS IN THE COMMENT** — a register
  width, a datasheet contradiction, a hardware-only trap, an ordering constraint
  the host suite cannot observe. Keep it to the line that IS the constraint, and
  say in the commit why there is no test, exactly as an untestable fix does.
  Moving such a fact to a test that does not exercise it is worse than leaving
  it: it reads as covered and is not.
- **A `docs/` page is current state; `docs/investigations/` carries the models
  that were refuted.** Even there, write for someone who was not present: what
  the code does now, why not the obvious alternative, and what going back would
  look like from the outside. A refuted model earns its page only when the wrong
  answer was convincing — `docs/investigations/moving-write-origin.md` is the
  shape, and its point is that a two-term fit matched eleven readings to 0.43 px
  and was still wrong.
- **A HANDOVER IS NEVER COMMITTED.** Not to `docs/investigations/`, not
  anywhere in the tree. A handover is addressed to the next session — where the
  unit is, what is uncommitted, what to do first — and every one of those facts
  is false within a day, so it arrives in review as a commit to delete. Write it
  outside the repo, in `~/Projects/gbsc-pro-handovers/`.

  **An investigation record is the opposite and is welcome.** It is addressed to
  anyone, and it says what was tried, what was measured, and what the outcome
  was — models refuted with the evidence that killed them, a measurement with
  the framing it was taken at, a trap with a live consequence. It carries no
  session state: no working-tree status, no "read this first", no "where the
  unit is", no instruction to the reader about what to do next.

  The test is the tense. A finding is stated in the present and stays true;
  a handover is stated in the imperative and stops being true when the session
  ends. When a session produces both, the findings go in `docs/investigations/`
  and the rest leaves the repo.
- **WORK GOES ON `dev`. `main` IS WHAT HAS BEEN REVIEWED**, and it moves only
  when a review advances it — never as a side effect of doing the work.
  **Check the branch before the first commit of a session**, because a session
  that starts on `main` will happily put a day's work there and destroy the one
  signal the branch carries. Nothing is lost when that happens and the repair is
  pointer movement, as long as `main` is still a pure fast-forward ahead of
  `origin/main`:

  ```sh
  git rev-list --count main..origin/main    # 0, or stop and think
  git merge-base --is-ancestor dev main     # dev must be behind main
  git branch -f dev main && git checkout dev && git branch -f main origin/main
  git merge-base --is-ancestor main dev     # main is now an ancestor of dev
  ```

  **Force-pushing is fine on this fork** -- it is a solo playground, so history
  is curated freely, with `--force-with-lease`.

  No `feature/` or `fix/` branches, and no self-review PRs. The discipline is in
  how commits are split, not in branch topology.
- **`dev` IS PUSHED SO IT CAN BE READ ON GITHUB, and the review is of the
  RESULTING CODE rather than of each commit.** Hundreds of commits are not read
  one at a time; what is reviewed is the engine as it now stands. So a push is
  routine and carries no promise that the history is final — **once the code is
  agreed, the commits are squashed aggressively to remove churn** and the series
  rationalised.
- Commit messages: lowercase area prefix (`tv5725:`, `tools/hwtest:`,
  `build:`), then what changed and *why*, with the evidence. Look at
  `git log` before writing one.
- **Group by FEATURE first, then split that group by kind.** A theme is one
  feature — every change that implemented it — not one file, one layer, or one
  working session. A reviewer reads a feature once; grouping by file makes the
  churn the unit and leaves them rebuilding the feature out of five commits
  because it touched five files. `docs/patch-workflow.md` has the mechanics,
  including which regroupings conflict and why.
- **A commit holds ONE of these four, never a mixture.** It is a readability
  rule: a reviewer reading the engine should not have to step over a prose edit,
  and a tooling change should not hide inside a firmware diff.

  **UPSTREAM CHERRY-PICKABILITY IS NOT A CONSTRAINT.** This fork has diverged
  too far for a commit to be lifted back, so nothing is shaped to keep it
  liftable -- upstream-derived files, the frame time lock among them, are
  refactored like any other, and a fix does not wait on being separable.

  | kind | paths | prefix |
  |---|---|---|
  | firmware | `GBSC-Pro-Source code/**`, `test/**` | `tv5725:`, `build:`, … |
  | Python tooling | `tools/**` | `tools:`, `tools/hwtest:` |
  | project conventions | `CLAUDE.md`, `CODING_STYLE.md` | `project:` |
  | design notes | `docs/**` | `docs:` |

  `test/` travels with the firmware — it is the host-compiled C++ that proves it.
  So one feature is at most four commits, landing as an **adjacent run**:
  firmware first, then its tooling and its docs. The run is still one push and
  one conceptual commit, split only so each kind reads on its own.
  Check before committing:

  ```sh
  git show --name-only --format= HEAD | \
    sed 's#^GBSC-Pro.*#firmware#;s#^test/.*#firmware#;s#^tools/.*#tools#;
         s#^\(CLAUDE\|CODING_STYLE\).md#project#;s#^docs/.*#docs#' | sort -u
  ```

  One line out means one kind, which is the rule. More than one means split it.
- **DO NOT STOP FOR REVIEW PER COMMIT. COMMIT THE WORK.** The review is of the
  resulting code, in a broad pass once the shape has settled, which is the same
  thing the push rule above says -- thousands of commits are not read one at a
  time, and a session that halts on each C++ commit spends the session on the
  halt. **The first pass is the probing question**, asked of the code as it
  stands, and it reaches the parts a diff does not.

  So the gate is the SUITE and not a signature: run the host suite, commit, and
  say in the message what was measured and what is untested. A change that
  cannot be judged from the host suite says so and names the bench reading that
  stood in for one.

  The one thing still worth splitting is the KIND, because that is readability
  rather than approval -- firmware, tooling, project conventions and docs each
  read on their own. The check for that is below.

  Where a C++-touching commit does need to be found later, gate on the FILE
  LIST and never the subject prefix: 8 of 10 such commits were labelled
  `tv5725:`, `input:`, `gbs-control:`, `docs:` or `tools:`, so the prefix alone
  would miss the 68-file preset-table deletion, and upstream provenance selects
  nothing either -- `src/tv5725:` is new in this fork, so "present at
  `upstream/main`" waves the entire geometry engine past.

  ```sh
  for h in $(git log --format=%h --reverse main..dev); do
    git diff-tree --no-commit-id --name-only -r "${h}" | grep -qE '\.(cpp|h|hpp|c|ino)$' \
      && echo "$h $(git log -1 --format=%s ${h})"
  done
  ```

  Use `"${h}"`, not `"$h:path"` -- zsh applies its `:t` modifier inside the
  quotes and silently mangles the argument.
- **WORK THE ONE ISSUE THAT WAS NAMED.** Several faults on this board look
  related and are not, so a session chasing two at once cannot tell which
  change moved which symptom -- cropping and corruption at a high sampling
  count were treated as one problem and are separate, which produced a fix for
  a problem nobody had. Restate the single issue before starting, and when a
  second fault appears put it in `docs/known-issues.md` and carry on with the
  first. **The exception is a trip already being spent**: where a flash or a
  bench visit has to happen anyway, offer to carry an already-filed adjacent
  fix with it. It is still a separate commit -- what is bundled is the trip,
  not the change.
- **EVIDENCE MUST BE CURRENT.** The panel is being watched while a session
  runs, so a capture from earlier presented as live is contradicted immediately
  and costs trust in every other claim -- and bench state changes constantly,
  so a frame minutes old is usually of a different state. Take a new one per
  claim, check its mtime, and delete the target first so a failed capture
  cannot silently leave the previous file in place. A sweep's per-point frames
  are evidence for that sweep, labelled as taken then.
- **Load the `tdd` skill before writing code.** Not a formality here: this
  codebase punishes the alternative. An evening went into a preferences bug that
  three sessions "fixed" without a failing test to say what fixed meant, and each
  fix guarded the wrong thing — the open instead of the read, then the read
  instead of the writer. A test that fails for the stated reason first is what
  stops that. Write the test at the cheapest layer that really exercises the
  behaviour, watch it fail, then make it pass.
- **Load the `troubleshooting` skill before diagnosing a fault**, and before
  reaching for a fix that "should" work. The expensive mistake here is a
  hypothesis formed before the instruments are read, and this file is a list of
  them: which vsync sample failed, inferred twice and wrong both times; a dead
  source diagnosed from four invented register addresses while the picture on
  the screen was perfect; four geometry models proposed in one evening and three
  refuted. The instruments are cheap and answer first — the serial console,
  `/geometry`, fields read by NAME, a `tv-snap` photograph, a register diff
  against a known-good. Read them, carry two or three candidates, and pick the
  one experiment that separates them.
- **A BYTE-IDENTICAL FIRMWARE BINARY IS THE PROOF, SO DO NOT FLASH IT.** Where
  a behaviour-preserving refactor produces one, the binary comparison is
  stronger evidence than the suite -- it covers every path, including the ones
  no test reaches -- and the bench time is expensive and disruptive, the
  `--source` suite leaving the picture wrong and needing a reset. Compare the
  `Sketch uses N bytes` line, or diff `build/output/gbs-control.ino.bin`
  against the previous build; same bytes means commit on the host tests alone.
  **PIN `GBS_BUILD_REV` TO COMPARE, OR THE COMPARISON CANNOT SUCCEED.** The
  build compiles in `$(GIT_REV)$(GIT_DIRTY)`, so identical source at two
  revisions -- or across one commit, or across a tree going clean -- shifts
  every byte after the string and changes its length besides: measured 13321
  differing bytes and 16 bytes of size between two builds of one tree.
  `make -C build GBS_BUILD_REV=x` on both sides is what makes them comparable.
  **It also means a flashed image legitimately differs from a fresh build of
  the same code**, so a hash mismatch is not evidence that a flash carried
  something else -- ask the unit's behaviour, or rebuild at the revision that
  was flashed.
- Firmware changes ship as a bounded commit plus a commit adding a runtime
  acceptance test. No source-parsing tests — test behaviour, not implementation.
  No tests for removals.
- **When a fix cannot be tested, say so in the commit and say why.** Several
  reliability fixes here have no acceptance test because their trigger is not
  reachable over HTTP — the preference wipe fires only from the OSD and IR
  handlers, so it has three manual confirmations and no automation. That is an
  acceptable answer; silently shipping untested and letting the next session
  assume coverage is not.
- **A HARDWARE FIXTURE WAITS; IT DOES NOT SKIP.** A `pytest.skip` on "the
  source is not locked" reads as green and is not. Three `test_front_porch.py`
  tests skipped in every full `--source` run taken, because a preceding
  teardown's `/sc?~` leaves the source out for about five seconds and
  `recover_lock()` returns as soon as it locks rather than when the pass ends --
  so they skipped straight through the change to `OutputMode::FrontPorchMinPx`
  they exist to guard, and failed the first time they ran. Use
  `wait_for(lambda: locked_steadily(host))` with a bounded timeout and keep the
  skip only for a source that genuinely never returns. The same holds for any
  precondition the firmware reaches on its own schedule: a poison wants
  `_settled_sampling()` first and a pad press wants a counting sync processor,
  because a press into an unmeasured source is accepted, logged and moves
  nothing, and a poison written mid-detection is overwritten before the
  read-back -- so the failure lands on the next test, or nowhere at all.
- **THE PICTURE IS NOT PRECIOUS AND THE DEFAULT RUNS EVERYTHING.** The flags in
  `conftest.py` that disturb the picture or write flash -- `--source`,
  `--preset-load`, `--preset-save`, `--reboot`, `--pllad-hostile` -- are
  opt-OUT, so `pytest --host=…` is the full suite and a leg is skipped only
  when someone says so. A suite that quietly ran less than it appeared to is
  the failure this shape exists to avoid.
  **`--source` leaves the picture wrong, and a cold boot is the fix** — mention
  it if someone is watching the screen, but do not withhold the run for it. Measured 2026-08-15: the
  pad tests left the framing cropped and panned well off the default, and an
  oversampling test left `PLLAD_MD` 4012 against the 2548 the source wants, so
  the picture is zoomed and softer. Neither is persisted, so an ESP reset
  re-detects and re-solves both — `nix develop -c esptool --port /dev/ttyUSB0
  --after hard_reset --no-stub flash_id`. **There is no framing constant to
  check it against**: `/geometry` reports where the capture window starts and
  how far it runs, and the default is the placement the solve computes for the
  source, not four zeroes. Reset once, note what it lands on, and compare with
  that. The route is behind `GBS_DEBUG`, so a 404 is a build without it.
- **A BUILD ENTRY POINT IS A MAKEFILE, NEVER A BASH SCRIPT, AND IT NEVER SHELLS
  OUT TO NIX.** Provisioning the environment inside the build pulls unpinned
  tools from whatever channel is configured, pays shell-startup cost per step
  and makes the script NixOS-only; entering the dev shell is the caller's job,
  so the rules work on any host that supplies the tools. The flake's devShell
  pins them and the Makefile calls them off PATH, guarded so a missing one says
  to run `nix develop`. Invocation is `nix develop -c make -C build`.
