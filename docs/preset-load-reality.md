# What a preset load actually writes

**The scaling preset tables are gone, and `writeProgramArrayNew()` with them,
and so is the last blob — `rgbhv.h` was unreferenced and is deleted.** Bypass is
applied by `bypassModeSwitch_RGBHV()` writing registers. So a preset load is now
`Tv5725::BringUp::init()` — the static registers, one call per subsystem — plus
whatever the geometry engine solves, and nothing else.

Two consequences that have each cost time. **A small register diff across a
preset load is expected, not evidence that no preset loaded**: `/sc?~` moves
about thirty registers on a settled source, and reading that as "the preset path
did not run" is a wrong inference drawn from the old 432-register figure.
And **a preset can no longer clobber a field**, because there is no bulk byte
range to overrun one — the mechanism behind `docs/preset-load-clobber.md` is
closed by construction, though its individual findings still describe registers
nothing owns.

`docs/chip-initialisation.md` is the design and why the tables went.
`docs/investigations/preset-abandonment-audit.md` has what was measured from the
twelve tables while they existed, which is what `BringUp` was built from.
