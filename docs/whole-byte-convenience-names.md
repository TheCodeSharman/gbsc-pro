# The whole-byte convenience names

The driver declares 25 names that cover a whole byte whose individual bits the
datasheet already names. `GBS::PLL648_CONTROL_01::write(0x75)` sets five
documented fields at once under a name RD-5725-1.1 does not contain, and `0x75`
cannot be looked up anywhere.

**Use the registers as the datasheet defines them, and carry no aliases.** Where
there is a choice, prefer the datasheet's name unless the firmware's has a
tangible benefit.

Removing them is a campaign, deliberately not started in one go. This is the
inventory, the two things that make it non-mechanical, and the order to do it in.

## What is already settled

**Exact aliases are gone.** Four fields were declared under two names each and
the header now declares one apiece — see `15ebd4e`, which is also where the
reason it mattered is written down: the bring-up block wrote one name while the
sketch wrote the other, so four bits had two owners and every by-name check
passed. `test_the_shipped_header_declares_no_field_twice_under_two_names`
asserts the empty list.

**The opposite direction is a keeper, not a target.** Where gbs-control names a
bit that the datasheet only has inside a wider lumped field, the firmware name
is *more* informative and stays:

| firmware name | inside datasheet field | |
|---|---|---|
| `STATUS_MISC_PLLAD_LOCK` | `MISC_STATUS_[8:0]` | s0_09[7] |
| `SDRAM_RESET_SIGNAL` | `MEM_INI_REG[7:0]` | s4_00[4] |

That is the whole of the exception. `ADC_TEST_0C_BIT1` was the third and had
zero call sites; it is deleted.

## The inventory

25 names, 109 call sites, all in `gbs-control.ino` except three
(`PLL648_CONTROL_01` in `framesync.h` ×2 and `Geometry.cpp` ×1).

| name | addr | uses | every bit datasheet-named? |
|---|---|---|---|
| `PLL648_CONTROL_01` | s0_41 | 24 | yes |
| `TEST_BUS_SP_SEL` | s5_63 | 17 | no — bit 7 |
| `INTERRUPT_CONTROL_00` | s0_58 | 12 | yes |
| `RESET_CONTROL_0x47` | s0_47 | 9 | no — bits 5,6,7 |
| `RESET_CONTROL_0x46` | s0_46 | 8 | no — bit 7 |
| `MADPT_Y_DELAY_UV_DELAY` | s2_17 | 6 | yes |
| `ADC_TEST_04` | s5_04 | 4 | no — bits 5,6,7 |
| `INTERRUPT_CONTROL_01` | s0_59 | 3 | yes |
| `ADC_TEST_0C` | s5_0c | 3 | no — bits 5,6,7 |
| `ADC_TA_05_CTRL` | s5_05 | 3 | no — bits 5,6,7 |
| `PLL648_CONTROL_03` | s0_43 | 2 | no — bits 6,7 |
| `PAD_CONTROL_01_0x49` | s0_49 | 2 | no — bit 7 |
| `INPUT_FORMATTER_02` | s1_02 | 2 | yes |
| `GPIO_CONTROL_00` / `_01` | s0_52 / s0_53 | 2 each | yes |
| `DEINT_00` | s2_00 | 2 | yes |
| `ADC_5_00` | s5_00 | 2 | no — bits 5,6,7 |
| `SP_CS_0x3E` | s5_3e | 1 | no — bits 6,7 |
| `SP_5_57` | s5_57 | 1 | no — bits 4,5 |
| `SP_5_56` | s5_56 | 1 | yes |
| `PLLAD_CONTROL_00_5x11` | s5_11 | 1 | yes |
| `PLLAD_5_16` | s5_16 | 1 | yes |
| `PAD_CONTROL_00_0x48` | s0_48 | 1 | yes |
| `ADC_AUTO_OFST_RANGE_REG` | s5_0f | 1 | yes |
| `ADC_5_03` | s5_03 | 1 | no — bits 6,7 |

**13 fully covered, 12 not.**

## The two things that make it non-mechanical

### 1. A byte write is not the same as writing its named fields

This is the trap, and it is silent. `GBS::ADC_TEST_0C::write(0x12)` sets
**s5_0c[7:5] to 0**. Nothing in the datasheet names those three bits, so the
decomposition — `ADC_CKBS::write(0)` plus `ADC_TEST::write(9)` — leaves them at
whatever they already held.

For the 12 partially-covered bytes the two forms are therefore *not equivalent*,
and the difference only shows if some path ever sets one of those bits. Doing
those needs a bench check per byte, not a refactor.

The 13 fully-covered bytes have no such gap, which is why they go first.

### 2. Save-and-restore genuinely wants the byte

By idiom, over the 109 sites:

| | count |
|---|---|
| magic literal write — `write(0x75)` | 77 |
| compare against a literal — `read() != 0x75` | 12 |
| save into a local — `backup = …::read()` | 11 |
| restore or computed write — `write(backup)` | 9 |

The 20 save/restore sites are the ones to leave alone:

```cpp
uint8_t debug_backup_SP = GBS::TEST_BUS_SP_SEL::read();
GBS::TEST_BUS_SP_SEL::write(0x0f);
...
GBS::TEST_BUS_SP_SEL::write(debug_backup_SP);
```

The value is never interpreted — it is an opaque token — and splitting it into
three datasheet fields means three reads, three writes and three chances to drop
one. No datasheet field can express "this byte, whatever it is". The whole-byte
name is the right tool here and this is the tangible benefit the rule allows for.

**The literal writes are the target.** The code already admits it:

```cpp
GBS::ADC_TA_05_CTRL::write(0x02); // ADC test enable BIT0    ADC test bus control bit   BIT4:1
GBS::ADC_TEST_04::write(0x02);    // 1:0 REF test resistance selection 4:2REF test current selection
```

Those comments are hand-copied paraphrases of `ADC_TA_EN`, `ADC_TA_CTRL`,
`ADC_TR_RSEL` and `ADC_TR_ISEL` — the datasheet names the line could have used,
which would have made the comment unnecessary. Someone had to write out what the
bits mean *because the code would not say*.

## Order to do it in

1. **The 13 fully-covered bytes, literal writes only.** Exactly equivalent, so a
   snapshot diff over a flash should move nothing but the usual runtime drift.
   One commit per byte or per small group — `PLL648_CONTROL_01` alone is 24
   sites and deserves its own.
2. **`PLL648_CONTROL_01` needs thought beyond mechanics.** `0x75` is a *sentinel*
   the firmware tests for (`framesync.h:652`, `gbs-control.ino:1094`), not just a
   value it writes, and `Geometry.cpp` writes a computed `raster.divider` into
   it. Decomposing the writes without deciding what the sentinel becomes will
   break the tests-for-0x75. See CLAUDE.md on why `PLL648_CONTROL_01 == 0x75` is
   a claim the firmware made rather than a measurement.
3. **The 12 partially-covered bytes**, each with a bench check that the
   undocumented bits were already 0 — or a decision to keep the byte name for
   exactly that reason and say so in a comment.
4. **Leave the 20 save/restore sites.** Record the reason at the declaration so
   the next pass does not re-litigate it.

## Verifying each step

The proof is register-level, not binary-level — the generated code changes, the
chip state must not:

```sh
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> --out snapshots/before.json
make -C build flash-ota HOST=<ip>
python3 tools/gbsc-pro-hwtest/snapdiff.py --diff snapshots/before.json snapshots/after.json
```

Expect only the known runtime-adapted drifters — `PA_SP_S`, `SP_H_CST_SP` — and
treat anything else as the decomposition being wrong.
