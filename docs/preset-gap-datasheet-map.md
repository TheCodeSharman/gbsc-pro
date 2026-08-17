# The preset gap, mapped to the datasheet

Everything the twelve preset tables still write that nothing else owns, resolved
against RD-5725-1.1, **which is the authority**.

**Every field the preset still owns is a documented field, and every unnamed
bit is now accounted for.** Mapping them settled four outright and a bench
session retired two more, taking the gap from 18 fields + 4 holes to
**14 fields + 0 holes** — and thirteen of the fourteen are in segment 4. What
is left is the memory subsystem, not a scattering of loose ends.

## The 14 still unowned

SDRAM bus timing, the three FIFOs, and the buffer/guard addresses. Nothing in
the geometry path remains, and only `IF_LD_ST` sits outside segment 4.

| field | address | datasheet says |
|---|---|---|
| `IF_LD_ST` | s1_0c[4:1] | Line double write reset generation start position |
| `MEM_FK_RD_DLY` | s4_04[2:0] | SDRAM rising-edge clock delay for latching read data |
| `MEM_DATA_DLY_REG` | s4_18[2:0] | Data delay, 0.00–3.00 ns in eight steps |
| `MEM_CLK_DLY_REG` | s4_1b[6:4] | Clock delay, same eight steps |
| `PB_MAST_FLAG_REG` | s4_2c[5:0] | Playback FIFO master line flag — sets FIFO *high* request timing |
| `PB_GENERAL_FLAG_REG` | s4_2d[5:0] | Playback FIFO general line flag |
| `PB_CAP_BUF_STA_ADDR_A` | s4_31[20:0] | Capture/playback buffer A start address |
| `PB_CAP_BUF_STA_ADDR_B` | s4_34[20:0] | Buffer B start address — live, see below |
| `WFF_FF_HALF_REQ` | s4_42[1] | Request at FIFO half (1) or at write pointer 1 (0) |
| `WFF_SAFE_GUARD_A` | s4_44[20:0] | Write FIFO buffer A safe guard address |
| `WFF_SAFE_GUARD_B` | s4_47[20:0] | Write FIFO buffer B safe guard address |
| `WFF_LINE_FLIP` | s4_4a[4] | Write FIFO line ID invert |
| `RFF_MASTER_FLAG` | s4_4e[5:0] | Read FIFO master line flag — FIFO *high* request timing |
| `RFF_GENERAL_FLAG` | s4_4f[5:0] | Read FIFO general line flag — FIFO *low* request timing |

### What the datasheet settles for free

- **`PB_CAP_BUF_STA_ADDR_B` matters, and an earlier draft of this file wrongly
  implied it might not.** The datasheet qualifies it with "when in double buffer
  mode", which reads like an escape route — but `CAP_DOUBLE_BUFFER` (s4_21[1])
  is **1 in all twelve tables**, so double buffering is always on and buffer B
  is always live. Checking the enable bit is what settles this class of
  question, in both directions: the same check retires `CAP_SAFE_GUARD_A`,
  because `CAP_SAFE_GUARD_EN` is 0 in all twelve.
- **The `*_FLAG` pairs are watermarks, not addresses.** Master sets the FIFO
  *high* request timing and general the *low*, for playback (s4_2c/2d) and read
  (s4_4e/4f). They are a function of how much latency the memory path has, which
  is why they move with the output mode — and why `PB_FETCH_NUM` tore 80 of 493
  framings when it was wrong.
- **`WFF_SAFE_GUARD_B` mirrors `_A` exactly** in all twelve tables and on the
  bench, both 0x052000.

### Values, by table

`PB_CAP_BUF_STA_ADDR_A`/`_B` are the only two that split cleanly on standard —
0x100000 for all six PAL tables, 0xc0000 for all six NTSC, A always equal to B.
Nothing else in the list has a pattern that simple.

## The unnamed bits — all four retired

| address | datasheet says | how it was retired |
|---|---|---|
| `s3_14[3]` | **RESERVED**, between `VDS_DIS_VB_ST[10:8]` and `VDS_DIS_VB_SP[3:0]` | reads 0 on the live chip; nothing to carry |
| `s3_71[3]` | **RESERVED**, same slot in the `VDS_EXT_VB_*` pair | reads 1, but `VDS_EXT_*` drives HBOUT/VBOUT and those pads are off |
| `s4_5b[6:0]` | **RESERVED**; only bit 7, `MEM_FF_TOP_FF_SEL`, is a field | cleared `0x4c`→`0x00` on the bench, picture unchanged |
| `s5_5d` | **not in RD-5725-1.1 at all** — zero occurrences of `S5_5D` | cleared `0x02`→`0x00` on the bench, picture unchanged |

Three of the four were the preset writing 1s into bits the datasheet marks
reserved. They are not configuration; they are what a preset table happened to
contain.

**This refutes a tempting reading.** `s3_14[3]` and `s3_71[3]` sit exactly where
a 12th bit of `VDS_DIS_VB_ST` and `VDS_EXT_VB_ST` would go, and CLAUDE.md's
`VDS_HB_ST` case — where the datasheet's table rows are wrong and the field is
12 bits, not 10 — makes that look like a precedent. It is not. Both tables name
the bit RESERVED in the Name column *and* leave it out of the bit diagram, so
all three sources agree, which is the test `c922c80` established.

## What was retired, and on what evidence

Six things left the gap, each for a reason recorded below.

| retired | evidence |
|---|---|
| `CAP_SAFE_GUARD_A` | `CAP_SAFE_GUARD_EN` (s4_21[5]) is **0 in all twelve tables** — the guard is off in every mode |
| `s3_14[3]` | RESERVED, and reads 0 on the live chip |
| `s3_71[3]` | RESERVED, and `VDS_EXT_*` drives pads that are disabled |
| `CAP_FF_STATUS` | status register; cleared `0x1f`→`0x00`, picture unchanged |
| `s4_5b[6:0]` | RESERVED; cleared `0x4c`→`0x00`, picture unchanged |
| `s5_5d` | undocumented; cleared `0x02`→`0x00`, picture unchanged |

**Two of the six needed no write at all**, and those are the stronger results: a
register the chip is not reading cannot matter, whatever an experiment shows on
one mode. Reach for the enable bit before reaching for the bench.

The three that did need a write were done under `/freeze`, one register at a
time, from a snapshot, with the value read back and sync and the capture window
confirmed unmoved. The closing snapshot diff moved exactly those three bytes out
of 608. **The picture itself was confirmed by eye** — that part cannot be
automated, and the failure mode being guarded against is precisely the one that
does not show in a register dump: the segment-2 precedent was random colour
pixels with all 608 config registers still reading correct.

*Scope*: RGB in, progressive, scaling, 1080p50, one framing. Not interlaced, not
scale-down, not RGBHV bypass.

## How this was arrived at, and the trap in it

By address, not by name — a gbs-control name can otherwise hide what the
datasheet actually calls the bits.

Doing that exposed a defect in the extractor: `BITROW` required a Name cell to
start with `[A-Z_]`, and the PDF's wrap can leave the bit row holding nothing
but `[7:0]`, with the name on the line above. Those rows parsed as description
text, so the field vanished — no fragment, no wrong width, **absent**. Seven
fields, all of them wide multi-byte address fields. Two of them
(`WFF_SAFE_GUARD_B`, `VDS_NS_SQUARE_RAD`) were missing from the header too.

The reason that survived so long is worth keeping: **every test compared the
header against the extraction, and both had the same hole.** Agreement between
two views built from one broken parse is not evidence. That is now
`test_the_shipped_header_declares_every_field_the_datasheet_does`.

So: before treating an address as undocumented, grep
`tools/tv5725-header/regdef.txt` for its table. s4_47 spent a session as an
unnamed hole with a bench experiment proposed for it, and the datasheet had a
full page on it the whole time.
