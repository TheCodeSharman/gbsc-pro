# The register map

`tv5725_registers.json` is what every tool here uses to turn a field name into
`(segment, register, offset, width)`. The TV5725 does not describe itself and the
firmware exposes no map — `/getreg` and `/setreg` take raw numbers — so the suite
has to carry one.

**It is checked in and hand-owned.** Nothing generates it any more. That is
deliberate: this suite tests the unit as a black box, and a map read back out of
the firmware's own headers cannot disagree with the firmware, which is exactly
what a test needs to be able to do. An address here is the suite's claim about
the hardware, and if the firmware disagrees the device tests fail — a finding,
not a synchronisation problem.

`datasheet_fields.json` is where the claim comes from: the extraction of
RD-5725-1.1, 956 documented fields. It is the provenance for `tv5725_registers.json`
and the thing to check an address against before changing one.

## Changing an address

Check RD-5725-1.1 itself, not the firmware. The datasheet contradicts itself —
a wide field's slice appears in the bit diagram, the Bit/Name rows and the
description text, and for eleven fields those disagree. CLAUDE.md, "The
datasheet contradicts itself", has the resolution and the known-wrong cases.

Then confirm on the bench. `inrange.py` and `test_register_bounds.py` will fail
on a width that does not hold the value the unit reports.

**A field declared narrower than it is truncates every value written through it
and says nothing.** That is the failure mode to fear here, and no test catches it
in general — only a value large enough to clip.

## History

Both files were generated until 2026-08-17, by `tools/tv5725-header/` (a parse
of the datasheet) joined to the firmware's register headers. That machinery found
real defects — invented field names lifted from wrapped PDF lines, seven wide
fields silently dropped — and once it had, it was maintaining a copy of something
already settled. It is in git if the PDF ever needs parsing again.
