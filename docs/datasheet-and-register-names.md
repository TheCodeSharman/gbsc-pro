# The datasheet contradicts itself, and the header is the survivor

**Rediscovered three times now, so it is written down.** RD-5725-1.1 gives a wide
field's bit slices in three places, and they disagree:

1. the **bit diagram** above each register's table
2. the **Bit/Name table rows**
3. the `bit[hi:lo]` written inside the **Function description text**

`VDS_HB_ST` is the canonical case — diagram `[7:0]`/`[11:8]` (right), table rows
`[9:8]`/`[3:0]` (wrong) — and reading the table makes it 10 bits at `s3_05`
instead of 12 at `s3_04`. It holds 1342 on this bench, which does not fit in 10
bits, so the header is right and the table is simply an error.

- **Cross-check all three; do not trust one.** `c922c80` (2026-08-04) cut the open
  disagreements from 12 to 2 that way. **The diagram is not automatically the
  authority either** — `SP_H_CST_SP` has the wrong slice in its diagram and the
  right one in its table.
- **One name per field — there are no intended aliases.** Settled 2026-08-13:
  prefer the datasheet's name unless the firmware's has a tangible benefit, and
  the benefit that counts is *granularity* — `SDRAM_RESET_SIGNAL` names one bit
  the datasheet only has inside `MEM_INI_REG[7:0]`, so it stays. A better
  wording counts too (`INT_CONTROL_RST_SOGBAD` over the datasheet's bare
  `INT_RST_0`), and a name commented "fake name" does not. **An alias is not
  untidiness, it is a second owner**: the bring-up block wrote `INT_RST_0` while
  the sketch wrote `INT_CONTROL_RST_SOGBAD`, four bits had two owners, and every
  check passed because checks compare names. **Ask in bits, not in names** —
  nothing guards this automatically since the ownership tooling was deleted.
- **The whole-byte convenience names are a live campaign, not settled.**
  `PLL648_CONTROL_01::write(0x75)` sets five documented fields under a name the
  datasheet lacks. A literal byte write is the target; a save/restore pair
  through one is legitimate and stays. **Where a byte has bits no datasheet name
  covers, decomposing it is not equivalent** — the byte write zeroes those bits
  and field writes leave them. The inventory, the counts and the order are
  `docs/whole-byte-convenience-names.md`; do not restate a count here, it rots.
- **Where the header and the datasheet disagree, the header wins.** Its values
  are that audit's output plus bench proof, and eleven such fields remain.
  **Nothing checks this any more** — the audit was deleted on 2026-08-17 once it
  had finished finding things, so a new disagreement now arrives silently.
  `tools/gbsc-pro-hwtest/datasheet_fields.json` is the extraction it was checked
  against, and git has the tooling.
- **Assert known widths before believing any extraction.** `VDS_HSCALE` 10,
  `PLLAD_MD` 12, `IF_HB_ST1` 11, `MEM_MODE_REG` 16. Two attempts at re-deriving
  the field set on 2026-08-13 both produced complete-looking tables that failed
  exactly there — one keyed the parse by name so later slices of a wide field
  overwrote earlier ones, one read `(hi, lo)` as `(lo, hi)`. Both looked fine.
- **The danger is one-directional.** A field declared *narrower* than it is
  truncates every value written through it and says nothing.
- **A READ-BACK IS NOT PROOF THE HARDWARE ACTED ON THE WRITE.** Reserved bits
  beside a field are often writable storage with nothing behind them, so
  widening a field on a successful round trip proves only that the bit exists.
  `HD_HSYNC_RST` is the case: `s1_38` bit 3 stores a 1 and reads it back, and
  the channel's counter ignores it — proven by the pair either side of 2048,
  where a line of 2040 counts displays and one of 2056 gives no signal.
  **Ask the behaviour, not the register.**
  `docs/investigations/the-bypass-divider-is-capped-by-the-channel-counter.md`
- **The PDF's line wrapping is what breaks any re-derivation, in two
  directions.** A long field name wraps across two lines, and a parser that
  keeps only the second gets a fragment — `ALUE`, `EG0`, `R_B` — which reaches
  the header looking like a real register. Every genuine field name contains an
  underscore, so a name without one is a fragment. The worse direction is the
  quiet one: the wrap can put the whole name on the line above and leave the bit
  row holding only `[7:0]`, and the field then arrives as nothing at all. Seven
  went that way, every wide multi-byte address field
  (`WFF_SAFE_GUARD_A`/`_B`, `CAP_SAFE_GAURD_A`, `RFF_WFF_STA_ADDR_A`/`_B`,
  `VDS_NS_SQUARE_RAD`). **A completeness check that compares a header against an
  extraction cannot see this**, because both have the same hole — count against
  the PDF's own table count instead.
- **The datasheet has typos in field NAMES, not just in bit slices.**
  `CAP_SAFE_GAURD_A` ("GAURD") and `OSD_YCBCR_RGB_FORMATE` ("FORMATE"). The
  header carries the corrected spelling at the same address — so searching
  RD-5725-1.1 for the header's name will fail on exactly those two.
- **A "gap" in the register map may be a documented register the tooling lost.**
  s4_47..49 was carried for a session as an unnamed hole with a proposed bench
  experiment. It is `WFF_SAFE_GUARD_B`, documented in full, with its own table.
  Search RD-5725-1.1 itself before designing an experiment.
- **A bit the preset writes is not automatically a field.** Of the four
  addresses whose unnamed bits some preset table sets, three — `s3_14[3]`,
  `s3_71[3]`, `s4_5b[6:0]` — are marked **RESERVED** in the datasheet's own
  tables. The preset writes 1s into reserved bits. Only `s5_5d` is genuinely
  absent from RD-5725-1.1, appearing zero times in it.
