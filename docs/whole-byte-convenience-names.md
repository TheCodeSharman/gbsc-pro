# The whole-byte convenience names

A name covering a whole byte whose individual bits the datasheet already names
hides what the write does. `GBS::PLL648_CONTROL_01::write(0x75)` sets five
documented fields at once under a name RD-5725-1.1 does not contain, and `0x75`
cannot be looked up anywhere.

**Use the registers as the datasheet defines them, and carry no aliases.** Where
there is a choice, prefer the datasheet's name unless the firmware's has a
tangible benefit.

**Deleting the name is the mechanism, not a policy against using it**: every
remaining call site then fails to compile, which is a check no grep gives. This
is what is left, why each one is still here, and the one thing that makes any of
it non-mechanical.

Two bugs have come out of decomposing these, so the exercise is not cosmetic. A
byte write to s0_49 took the sync output pad down behind the engine's cache and
left a dark panel with every geometry register correct, and stepping s2_17 to
move the luma delay by one pipe borrowed out of the chroma delay at zero, which
is where a line-doubled source sits.

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

**Two are private to their subsystem now**, which settles the ownership half
without settling the byte half. `INTERRUPT_CONTROL_00` and `_01` are declared
inside `Tv5725::Interrupts` and named only by its own three sequences -- unmask
every source, clear every latched condition, clear all but SOG bad -- so the bits
`INT_RST_*` and `INT_ENABLE*` already name have no second writer. The byte write
itself stays: each sequence is one bus write per byte where the decomposition is
eight, and the write trace is the equivalence oracle for the load they run in.

## The read side was not in this inventory, and is nearly finished

The inventory below is write-side. Six more names cover a status byte whose bits
are all named, and a READ decomposes with no hazard at all -- nothing is written,
so there are no reserved bits to zero. Settled:

- **Seven are deleted.** `TEST_BUS_2E`, `RESET_CONTROL_0x46`, `RESET_CONTROL_0x47`,
  `INPUT_FORMATTER_02`, `DEC_5_1F` and `VDS_3_24` had no callers at all;
  `STATUS_16`'s one caller, the boot trace, takes `GBS::read(0x00, 0x16)`.
- **`Tie` is how a decision keeps its atomicity**, and it already exists -- do not
  add a second facility for it. `Tie<A, B>::read(a, b)` takes several named fields
  in one transaction, which is the only thing a whole-byte read was buying.
  `getStatus16SpHsStable()` and the television Info screen use it.

**What is left of the read side is nearly all inside `getVideoMode()`**, lines
3109-3211 of the sketch: every decision site for `STATUS_03`, `STATUS_04` and
`STATUS_05`, and most of `STATUS_00`'s. **Step 12 of
`video-source-acquisition.md` deletes that function**, so converting the tree
there is work on code already scheduled to go. Leave it.

Outside it every remaining whole-byte read is legitimate, which is the good
reason the rule allows for:

| site | why the byte |
|---|---|
| `printInfo()`'s `S:%02x.%02x.%02x` | a raw dump of three bytes, reserved bits included |
| auto-gain's capture-and-compare | wants ANY bit to have changed, not a named one |
| nine discarded `STATUS_00::read()` | bus exercise after `startWire()`; the register's content is never used |

**The nine bus pokes are a naming job, not a bit job.** Three consecutive
`GBS::STATUS_00::read();` with the result dropped says nothing about intent; what
it wants is a name for "prove the bus answers", and any readable register serves.

## The inventory

Write-side names. The count here has rotted before and will again -- an
enumeration over the headers gave 28 after the seven deletions above, against the
25 this section used to claim -- so **count rather than quoting**:

```sh
python3 - <<'EOF'
import re, glob, collections
d=re.compile(r'typedef\s+UReg<\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*>\s*(\w+)')
b=collections.defaultdict(list)
for f in glob.glob('GBSC-Pro-Source code/gbs-control/src/*/*.h'):
    for l in open(f, errors='surrogateescape'):
        m=d.search(l)
        if m: b[(m.group(1),m.group(2))].append((int(m.group(3)),int(m.group(4)),m.group(5)))
print(sum(1 for k,v in b.items()
          if any(o==0 and w==8 for o,w,_ in v) and any(not(o==0 and w==8) for o,w,_ in v)))
EOF
```

Almost all are used from `gbs-control.ino` alone -- the exceptions are
`PLL648_CONTROL_01` in `FrameSync.cpp` and in `Geometry.cpp`, and the two interrupt
bytes, which no longer have a call site outside `Interrupts.cpp`.

The per-name `uses` column below is a snapshot and drifts with every commit
touching the sketch. Recount rather than trusting it:

```sh
grep -c 'GBS::PLL648_CONTROL_01::' "GBSC-Pro-Source code/gbs-control/gbs-control.ino"
```

What is left, and what each one is waiting on:

| name | addr | why it is still here |
|---|---|---|
| `PLL648_CONTROL_01` | s0_41 | the display-clock sentinel. `0x75` means "the Si5351 drives the display" to the code that compares against it, and it is saved and restored as a byte in several places. Settling what the sentinel *is* comes before decomposing it |
| `PLL648_CONTROL_03` | s0_43 | written once, beside `PLL648_CONTROL_01`, and goes with it |
| `PLLAD_CONTROL_00_5x11` | s5_11 | the ADC PLL group, which `PLLAD_LAT` loads on a rising edge. Field writes are read-modify-write, so decomposing changes what is in the register between the edge and the write |
| `PLLAD_5_16` | s5_16 | the same group |
| `INTERRUPT_CONTROL_00` / `_01` | s0_58 / s0_59 | kept on purpose. Private to `Tv5725::Interrupts`, so the bits have no second writer, and each sequence is one bus write where the decomposition is eight |
| `MEM_INI_REG` | s4_00 | **not a convenience name.** RD-5725-1.1 names the whole byte this. It is listed only because `SDRAM_RESET_SIGNAL` names one bit inside it, which is the granularity exception above |
| `STATUS_00`, `STATUS_05`, `STATUS_0F`, `SP_CS_0x3E` | — | read-side, and nearly all of it is inside `getVideoMode()`, which step 12 of `video-source-acquisition.md` deletes |

The ADC's reference trim is the shape the rest should take: three bytes written
in three places became `Adc::applyReferenceTrim()`, named fields, one owner.

## The two things that make it non-mechanical

### 1. A byte write is not the same as writing its named fields

`GBS::ADC_TEST_0C::write(0x12)` sets **s5_0c[7:5] to 0**, and the decomposition
— `ADC_CKBS::write(0)` plus `ADC_TEST::write(9)` — leaves those three bits at
whatever they already held. So the two forms are not equivalent.

**They are not equivalent in the DECOMPOSITION'S FAVOUR, and no bench check per
byte is owed.** Every bit left uncovered on a partially-covered write-side byte
is marked **RESERVED** in RD-5725-1.1's own table — checked against the
datasheet for all nine of them, s0_43, s0_49, s5_00, s5_03, s5_04, s5_05,
s5_0c, s5_3e and s5_57. Writing a reserved bit is what the byte form does; the
fields leave it alone, which is what a reserved bit is for.

So a partially-covered byte is *more* correct decomposed, not merely different,
and the split between fully and partially covered does not order the work.
`test_chip.cpp` asserts a seeded s0_49[7] survives `Chip::padsToResetState()`,
which is the guard against the byte form coming back.

### 2. Save-and-restore genuinely wants the byte

By idiom, at the time the inventory was taken:

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
   the firmware tests for (`src/tv5725/FrameSync.cpp`, `gbs-control.ino`), not just a
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
