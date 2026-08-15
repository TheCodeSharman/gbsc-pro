#!/usr/bin/env python3
"""What a static bring-up block has to write, resolved to field names.

    python3 tools/gbsc-pro-hwtest/bringup_map.py
    python3 tools/gbsc-pro-hwtest/bringup_map.py --subsystem VDS_

The plan this serves is docs/investigations/preset-abandonment-audit.md: twelve preset tables
collapse to one static bring-up block, and the block should be *named* register
writes rather than a byte array, so the code says what it is configuring.
Michael, 2026-08-13: "ideally we'd have classes and methods for each tv5725
subsystem as part of the engine. This would let us write self documenting code."

This computes the set. Three filters, and the middle one is the whole point.

1.  **What every scaling table agrees on.** preset_common.split() -- an address
    only counts when all twelve wrote it and wrote it identically.

2.  **Minus what the firmware already writes at runtime.** This is the filter
    that is easy to miss and expensive to miss. Of the 452 named fields inside
    the agreed set, **136 are written at runtime** by the sketch or the engine,
    and a bring-up that also wrote them would either fight them or clobber
    state that is not the chip's at all:

        GBS_OPTION_SCANLINES_ENABLED     s1_2c[0]
        GBS_OPTION_SCALING_RGBHV         s1_2c[1]
        GBS_OPTION_PALFORCED60_ENABLED   s1_2c[2]
        GBS_RUNTIME_FTL_ADJUSTED         s1_2c[4]

    Those four are **gbs-control's own option storage** parked in a spare
    register -- not TV5725 configuration in any sense. Writing them from a
    bring-up resets the user's scanlines, RGBHV-scaling and PAL-60 preferences
    every mode change. IF_LINE_ST is the same shape of mistake: Engine::write()
    owns it, so the bring-up must not.

3.  **Minus everything undocumented.** Michael: "if they are truly undefined we
    have no business touching them at all." Addresses past the end of the
    documented register set, and undocumented holes inside it, are dropped
    rather than reproduced. Note that "every table writes 0x00" is NOT proof the
    reset default is 0 -- if one of them defaults non-zero, not writing it
    changes behaviour, and the bench is where that shows up.

Names come from tv5725.h, not from the datasheet extraction: the header is what
the firmware writes through, and CLAUDE.md's rule is that where the two disagree
the header wins.

**"What is left unnamed is undocumented, not unextracted" USED TO BE WRITTEN
HERE, AND IT WAS FALSE.** The extractor was dropping fields whose Name cell held
only a slice, and the header was missing two of them -- so an unnamed byte could
perfectly well be a documented register the tooling had lost. Fixed 2026-08-13,
and now enforced by test_the_shipped_header_declares_every_field_the_datasheet_does
rather than asserted in a comment. The claim was unfalsifiable while it stood,
because every check compared the header against the extraction and both had the
same hole.
"""

import argparse
import collections
import glob
import json
import os
import re
import sys

import preset_common as pc

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.join(HERE, "..", "..", "GBSC-Pro-Source code", "gbs-control")
HEADER = os.path.join(SKETCH, "tv5725.h")

DECL = re.compile(
    r"UReg<\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*>"
    r"\s*([A-Za-z_][A-Za-z0-9_]*)"
)
WRITE = re.compile(r"GBS::([A-Za-z_][A-Za-z0-9_]*)::write")

# The identifier before a top-level parameter list, used to name the function a
# brace at depth 0 is about to open.
SIGNATURE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\([^()]*\)\s*(?:const\s*)?$")


# Functions whose register writes are NOT ownership, because they do not run on
# the scaling path. Each one drives the chip somewhere a scaling preset has to
# bring it back from, so the values they leave behind are the problem the block
# solves rather than evidence the block is unnecessary.
#
#   setResetParameters / _re   low power, and the reset state on the way in
#   setOutModeHdBypass         HD bypass; reaches doPostPresetLoadSteps() at
#                              gbs-control.ino:5183 and then writes its own
#                              values AFTER it, so the block cannot fight it
#   bypassModeSwitch_RGBHV     RGBHV bypass; never reaches it at all
#   resetPLL                   called only from setResetParameters(), which
#                              re-asserts PLL_VCORST two lines later
#
# **KEEP THIS SHORT AND ARGUE FOR EACH ENTRY.** Every name here converts "some
# code writes this" into "nothing owns this", which moves a field INTO a static
# block. A function listed by mistake freezes a value some mode never asked for
# -- the exact failure the agreement filter exists to prevent.
NON_OWNING = {
    "setResetParameters",
    "setResetParameters_re",
    "setOutModeHdBypass",
    "bypassModeSwitch_RGBHV",
    "resetPLL",
}

SUBSYSTEM = [
    ("VDS_", "video display scaler"),
    ("MEM_", "SDRAM bus"),
    ("SP_", "sync processor"),
    ("ADC_", "ADC"),
    ("PLLAD", "ADC PLL"),
    ("PLL", "display PLL"),
    ("IF_", "input formatter"),
    ("CAP_", "capture"),
    ("PB_", "playback"),
    ("WFF_", "write FIFO"),
    ("RFF_", "read FIFO"),
    ("DAC_", "DACs"),
    ("PAD_", "pads"),
    ("SFTRST_", "soft resets"),
    ("OUT_", "output"),
    ("INT_", "interrupts"),
    ("GPIO_", "GPIO"),
]


# Which src/tv5725/bringup/ class owns each field, longest prefix first so
# PLLAD_ reaches the ADC before PLL_ reaches the chip clock. The grouping is by
# SUBSYSTEM rather than by segment: FrameBuffer's four FIFOs are one thing to
# reason about and MemoryBus's bus timing is another, though both live in s4.
CLASSES = [
    ("SyncProcessor", ["SP_"]),
    ("Adc", ["ADC_", "PLLAD", "PA_"]),
    ("FrameBuffer", ["CAP_", "PB_", "WFF_", "RFF_"]),
    ("MemoryBus", ["MEM_"]),
    ("VideoProcessor", ["VDS_"]),
    ("InputFormatter", ["IF_"]),
    ("Interrupts", ["INT_"]),
    ("Gpio", ["GPIO_"]),
    # Chip is last and takes the remainder: the s0 resets, pads, DAC power and
    # output clock, which have no single prefix between them.
    ("Chip", []),
]


# **THE GENERATOR IS GONE, AND THIS RECORDS THAT IT FINISHED.** There was a
# GENERATED_CLASSES list here, and a --emit that printed a class body, because
# reverse engineering the blob is a job -- but a job that ENDS. Generated code
# was the anti-pattern, not the solution: the point of deleting the twelve
# preset tables is to replace an opaque blob with hand-written code that says
# why each value is what it is, and a generator emitting an equally opaque block
# from the same blob has not achieved that. Michael, 2026-08-14: "we are trying
# to base everything on datasheet specs and sensible values rather than opaque
# blobs with no documented justification other than someone saved a block of
# registers because 'they worked for me'".
#
# MemoryBus graduated 2026-08-14 and the last eight on 08-15, each proven a pure
# move by diffing `test/output/test_bringup --dump` across it. bringup_fields()
# now returns EMPTY, and that is the invariant to hold: a field the twelve
# tables agree on with no owner is a field nothing will write once the tables
# go. test_the_derivation_is_empty is the guard.
#
# CLASSES stays, as the prefix-to-subsystem taxonomy the --gap report groups by.


def class_of(name):
    for cls, prefixes in CLASSES:
        if any(name.startswith(p) for p in prefixes):
            return cls
    return "Chip"


def header_fields():
    """(seg, reg, lo, width, name) for every UReg declared in tv5725.h."""
    src = open(HEADER, encoding="utf-8", errors="replace").read()
    return [(int(s, 16), int(r, 16), int(lo), int(w), n)
            for s, r, lo, w, n in DECL.findall(src)]


def strip_noise(text):
    """Comments and string/character literals blanked, newlines kept.

    Brace counting has to run over this rather than the raw source. Unstripped,
    gbs-control.ino desynchronised within a few hundred lines -- a `{` inside a
    comment or a printf format is enough -- and every write after that point
    attributed to the wrong function, or to none.
    """
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
        else:
            out.append(c)
            i += 1
            continue
        out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
        i = j
    return "".join(out)


def writes_by_function(text):
    """[(function, field)] for every GBS::FIELD::write in one translation unit.

    Brace depth, not a C++ parser, and it only has to decide one thing: which
    TOP-LEVEL function a write sits in. The name is the identifier before the
    parameter list that precedes the depth 0 -> 1 brace.

    **A WRITE IT CANNOT ATTRIBUTE COMES BACK AS "", WHICH IS THE SAFE ANSWER.**
    Anything nested inside a class or namespace body -- every src/tv5725/ method
    -- opens its brace at depth 1, so the parse never sees it and the write is
    reported at file scope. "" is in no caller's non-owning list, so the field
    stays excluded from the block exactly as it was before this function
    existed. Attribution failing therefore costs coverage, never correctness,
    and that is why a shallow parse is good enough to act on.
    """
    clean = strip_noise(text)
    depth, current, pending = 0, "", ""
    out = []
    for line in clean.splitlines():
        for name in WRITE.findall(line):
            out.append((current, name))
        for ch in line:
            if ch == "{":
                if depth == 0:
                    match = SIGNATURE.search(pending.strip())
                    current = match.group(1) if match else ""
                    pending = ""
                depth += 1
            elif ch == "}":
                depth = max(0, depth - 1)
                if depth == 0:
                    current, pending = "", ""
        if depth == 0:
            pending = "" if ";" in line else pending + " " + line
    return out


def runtime_written():
    """Every field a writer that runs on the SCALING PATH writes.

    **"THE FIRMWARE WRITES IT" IS NOT THE SAME AS "SOMETHING OWNS IT", AND
    CONFLATING THEM COST THE WHOLE OF STEP 4.** This filter used to be a bare
    regex over every source file, so a field written anywhere by anything was
    excluded from the bring-up block for having an owner. Twenty-three of those
    writers are in NON_OWNING -- functions that put the chip into low power or
    into one of the two bypass modes. Each of them LEAVES a state a scaling
    preset has to undo, and the preset table was what undid it. Excluded from
    the block for having an owner, and unwritten on the path that needs them,
    those fields had no owner at all the moment the tables went.

    Measured on step4-wip, 2026-08-15: 34 bytes wrong against a known-good dump,
    and every one of them a field in this category. CLAUDE.md's yellow-tint
    fault -- DAC_RGBS_B0ENZ, "the firmware never writes that bit to 0 anywhere"
    -- is the same shape, and predates step 4 by weeks.
    """
    patterns = ["*.ino", "*.cpp", "*.h", os.path.join("src", "**", "*.cpp"),
                os.path.join("src", "**", "*.h")]
    names = set()
    for pattern in patterns:
        for path in glob.glob(os.path.join(SKETCH, pattern), recursive=True):
            if os.path.basename(path) == "tv5725.h":
                continue
            # The bring-up block itself is not "written at runtime" -- it IS the
            # set being derived. Without this the filter eats its own output and
            # bringup_fields() returns almost nothing, which is exactly what
            # happened the moment the block was added.
            if os.path.sep + os.path.join("tv5725", "bringup") + os.path.sep in path:
                continue
            text = open(path, encoding="utf-8", errors="replace").read()
            names.update(name for function, name in writes_by_function(text)
                         if function not in NON_OWNING)
    return names


def datasheet_names():
    """Every field name RD-5725-1.1 declares, from the header tooling's merge.

    Used only to break ties: where two header names cover the same bits, the
    block writes the one a reader can find in the datasheet. Missing file is not
    fatal -- the tie-break degrades to "keep the wider field".
    """
    merged = os.path.join(HERE, "..", "tv5725-header", "merged.json")
    try:
        with open(merged) as f:
            return set(json.load(f))
    except (OSError, ValueError):
        return set()


def subsystem_of(name):
    for prefix, label in SUBSYSTEM:
        if name.startswith(prefix):
            return prefix, label
    return "misc", "everything else"


def table_field(table, seg, reg, lo, width):
    """One field's value in one preset table, or None if the table omits it."""
    raw = 0
    for i in range((lo + width + 7) // 8):
        byte = table.get((seg, reg + i))
        if byte is None:
            return None
        raw |= byte << (8 * i)
    return (raw >> lo) & ((1 << width) - 1)


def bringup_fields():
    """[(subsystem, name, seg, reg, lo, width, value)] the block must write.

    **AGREEMENT IS PER FIELD, NOT PER BYTE**, because that is what the block
    writes. UReg is read-modify-write, so a bit that is constant across all
    twelve tables can be carried whatever its NEIGHBOURS in the byte do.

    Asking the byte instead cost 51 fields, and it cost them in the worst
    possible shape: the byte was excluded exactly because some OTHER bit in it
    varies, and that other bit is usually the one the firmware already writes at
    runtime. So the constant bit had no owner on either side -- s1_01[7]
    IF_SEL24BIT reads 1 in all twelve tables and is set by nothing but the
    preset table itself. Whole registers looked unownable when only one bit of
    them was.
    """
    tables = {name: pc.read_table(name) for name in pc.ARCHIVED_PRESETS}
    runtime = runtime_written()

    out = []
    for seg, reg, lo, width, name in header_fields():
        # Owned at runtime, or not a chip register at all.
        if name in runtime or name.startswith("GBS_"):
            continue
        values = {table_field(t, seg, reg, lo, width) for t in tables.values()}
        # A field missing from any table is not agreed: "they agree" has to mean
        # all twelve wrote it and wrote it the same, or the block freezes a value
        # some mode never asked for.
        if len(values) != 1 or None in values:
            continue
        prefix, _ = subsystem_of(name)
        out.append((prefix, name, seg, reg, lo, width, values.pop()))

    # **WHERE TWO NAMES COVER THE SAME BITS, THE DATASHEET'S NAME WINS.**
    # Michael, 2026-08-13: "can we use only the datasheet registers in the case
    # of overlap?" -- which is the right rule for a block whose whole purpose is
    # to be readable: every line can then be looked up in RD-5725-1.1.
    #
    # THE EXACT-ALIAS ARM OF THIS IS NOW DEAD CODE, and deliberately kept.
    # gbs-control used to carry its own name for four fields alongside the
    # datasheet's -- INT_CONTROL_RST_SOGBAD/INT_RST_0, SDRAM_RESET_CONTROL/
    # MEM_INI_REG -- and the header declared both. That was not merely untidy:
    # the block wrote one name while the sketch wrote the other, so four bits
    # had two owners and every by-name check passed. The header now declares one
    # name per field, so aliases() is empty and this branch never fires.
    #
    # It stays because the CONTAINMENT arm below is still live and still needed
    # -- e.g. ADC_TEST_0C over ADC_TEST -- and because a future header edit that
    # reintroduces an alias should be resolved here rather than crash. The
    # containment case is the one that bites: writing both a byte and a bit
    # inside it makes the result depend on which lands last.
    documented = datasheet_names()
    spans = [(f[2], f[3], f[4], f[5], f[1]) for f in out]
    drop = set()
    for seg, reg, lo, width, name in spans:
        for seg2, reg2, lo2, width2, name2 in spans:
            if name == name2 or seg != seg2:
                continue
            here = (reg * 8 + lo, reg * 8 + lo + width)
            there = (reg2 * 8 + lo2, reg2 * 8 + lo2 + width2)
            if not (there[0] <= here[0] and here[1] <= there[1]):
                continue  # `name` is not inside `name2`
            if there == here:
                # Exact alias: keep whichever the datasheet declares.
                if name not in documented and name2 in documented:
                    drop.add(name)
            elif name in documented and name2 not in documented:
                # **THE WIDE NAME IS A WHOLE-BYTE CONVENIENCE NAME, AND IT
                # LOSES.** PAD_CONTROL_00_0x48, PLL648_CONTROL_03 and
                # PLLAD_CONTROL_00_5x11 are three of the 25 non-datasheet byte
                # names CLAUDE.md is removing; each spans bits the datasheet
                # names one at a time. Keeping the byte would write every bit
                # under it -- including, for s0_43, bits 7:6 that neither the
                # header nor RD-5725-1.1 names -- which is exactly what
                # BringUp.h says the block must not do: "anything undocumented
                # ... simply not written". So the block writes the named bits
                # and leaves the unnamed ones to the holes report.
                drop.add(name2)
            elif name2 in documented or name not in documented:
                # Contained in a wider field, and the wider one is usable.
                drop.add(name)

    # **AND THE SAME RULE ASKED OF THE HEADER, WHICH IS WHERE IT HAS TO BE
    # ASKED.** The pass above compares `out` against `out`, so a whole-byte
    # convenience name only loses while the datasheet fields inside it are also
    # in the block. The moment those acquire a real owner they leave, and the
    # byte comes back.
    #
    # That is not hypothetical: Tv5725::Chip::init() took every documented bit
    # of s0_48 and s0_43, and the derivation immediately started asking for
    # PAD_CONTROL_00_0x48 and PLL648_CONTROL_03 -- two of the 25 names
    # CLAUDE.md's campaign is removing, resurrected by the very commit removing
    # them, each about to write over eight bits somebody else now owns.
    #
    # Whether the narrower field is in the block or owned at runtime makes no
    # difference to the question. Somebody names those bits properly, so the
    # byte must not be written; whatever is left under it is undocumented, and
    # undocumented bits are the holes report's, not the block's.
    for seg, reg, lo, width, name in spans:
        if name in documented:
            continue
        here = (reg * 8 + lo, reg * 8 + lo + width)
        for seg2, reg2, lo2, width2, name2 in header_fields():
            if seg2 != seg or name2 == name or name2 not in documented:
                continue
            there = (reg2 * 8 + lo2, reg2 * 8 + lo2 + width2)
            if here[0] <= there[0] and there[1] <= here[1] and there != here:
                drop.add(name)
                break

    out = [f for f in out if f[1] not in drop]
    return sorted(out, key=lambda f: (f[0], f[2], f[3], f[4]))


# What the preset writes and the BRING-UP DELIBERATELY WILL NOT, with the
# evidence for each. Bench session 2026-08-13, unit at 192.168.88.108, RGB in,
# 320x256@50 scaled to 1915x1126.
#
# The method for the three that needed a write: /freeze, snapshot, clear the
# bits one register at a time, confirm the value stuck, confirm sync and the
# capture window were unmoved, and have Michael confirm the picture. Then a full
# snapshot diff, which moved exactly the three intended bytes and nothing else.
#
# **A DISQUALIFICATION IS NOT A GUESS ABOUT WHAT THE CHIP IGNORES.** It is a
# statement that the preset writing this was noise, so the bring-up need not
# reproduce it. Two of the six needed no write at all, because a register the
# chip is not reading cannot matter -- which is a stronger argument than any
# experiment.
DISQUALIFIED = {
    "CAP_FF_STATUS":
        "s4_23, a STATUS register, and per RD-5725-1.1 valid only when "
        "cap_cntrl_[17] is 1. The presets write 0x8/0xd/0x1f into it. Cleared "
        "to 0x00 on the bench, picture unchanged.",
    # CAP_SAFE_GUARD_A was here, disqualified because CAP_SAFE_GUARD_EN is 0 in
    # all twelve tables and the guard was therefore off in every mode. That is
    # still true of the TABLES and no longer true of the firmware: Tv5725::
    # FrameBuffer now owns the whole SDRAM map and switches the guard ON, so the
    # field has a real owner and an exclusion describing it is dead. Removed
    # 2026-08-13, and stale_disqualifications() is what noticed.
    "IF_HB_ST1":
        "Measured inert 2026-08-12: set 1 was written to a zero-width window "
        "(ST1 = SP1 = 0) from a clean framing, the picture was normal, and the "
        "full suite then passed 318/0 against it.",
    "IF_HB_SP1": "The other half of the IF_HB_ST1 result.",
}

# The same, for addresses whose UNNAMED bits some table sets. Keyed by address
# because there is no field name to key on -- that is what makes them holes.
DISQUALIFIED_HOLES = {
    (3, 0x14):
        "bit 3 is RESERVED in the datasheet's own table for s3_14, between "
        "VDS_DIS_VB_ST[10:8] and VDS_DIS_VB_SP[3:0], and it reads 0 on the "
        "live chip -- the table that sets it is not the one loaded. NOT a 12th "
        "bit of VDS_DIS_VB_ST: the Name column, the bit diagram and the "
        "Function text all three say RESERVED, which is the cross-check "
        "c922c80 established.",
    (3, 0x71):
        "bit 3 is RESERVED, the same slot in the VDS_EXT_VB_* pair. It reads 1 "
        "on the live chip and still cannot matter: VDS_EXT_* programs the "
        "HBOUT/VBOUT blanking and those pads are disabled "
        "(PAD_BLK_OUT_ENZ = 1), with VDS_SYNC_IN_SEL 0 so there is no internal "
        "consumer either.",
    (4, 0x5B):
        "bits [6:0] are RESERVED -- only bit 7, MEM_FF_TOP_FF_SEL, is a field. "
        "The presets put 0x4c in them. Cleared to 0x00 on the bench, picture "
        "unchanged.",
    (5, 0x5D):
        "the string S5_5D occurs ZERO times in RD-5725-1.1. Every preset "
        "writes 0x02. Cleared to 0x00 on the bench, picture unchanged.",
}


def stale_disqualifications():
    """Entries that no longer describe anything the preset does.

    A disqualification is only meaningful while the preset still writes the
    thing. Once the tables go, or a field acquires a real owner, an entry is
    dead -- and a dead exclusion list is indistinguishable from a thorough one
    until someone re-derives it by hand.
    """
    fields, holes = _unowned_before_disqualification()
    live_names = {f[4] for f in fields}
    live_holes = {h[0] for h in holes}
    return sorted([n for n in DISQUALIFIED if n not in live_names] +
                  [f"s{s}_{r:02x}" for (s, r) in DISQUALIFIED_HOLES
                   if (s, r) not in live_holes])


def _unowned_before_disqualification():
    """(fields, holes) -- what still has no owner, and so still needs the preset.

    THE MEASURE IS THE FIELD, NOT THE ADDRESS. Counting addresses says 48 and
    means almost nothing: most of those 48 are bytes whose every NAMED bit is
    owned and whose remainder is undocumented and reads 0.

    `fields` is the real list: named, varies across the twelve tables, written
    by neither the block nor the firmware, and not covered bit-for-bit by other
    fields that are. That last clause is what removes the whole-byte convenience
    names -- SP_CS_0x3E "varies", but its only varying bit is SP_DIS_SUB_COAST,
    which the firmware owns; and PLL_VS4/PLL_2XV/PLL_4XV are covered by the
    runtime's whole-byte PLL648_CONTROL_01 write.

    `holes` is the other half nobody can name: addresses with unnamed bits that
    some table sets to 1. Undocumented AND non-zero, so "we have no business
    touching them" does not settle it -- something is being configured.

    This function exists because the previous handover said to reproduce this
    list "with the snippet in this session's history", and a number that can
    only be re-derived from a transcript is a number that has already rotted.
    """
    tables = {name: pc.read_table(name) for name in pc.ARCHIVED_PRESETS}
    runtime = runtime_written()
    block = {f[1] for f in bringup_fields()}
    fields_of = list(header_fields())

    def owned(name):
        return name in runtime or name.startswith("GBS_") or name in block

    fields = []
    for seg, reg, lo, width, name in fields_of:
        if owned(name):
            continue
        values = {table_field(t, seg, reg, lo, width) for t in tables.values()}
        values.discard(None)
        if len(values) <= 1:
            continue
        # **ASK ABOUT THE BITS THAT DIFFER, NOT ABOUT ALL OF THEM.** A whole-byte
        # convenience name "varies" whenever any one bit under it does, and the
        # bit that varies usually has an owner of its own -- SP_CS_0x3E is
        # 0x00/0x20 across the tables and that 0x20 is SP_DIS_SUB_COAST, which
        # the firmware writes. Asking about the whole span instead keeps the
        # alias in the list forever, because its undocumented top bits can never
        # acquire an owner. Those belong to `holes` if they are ever set, and to
        # nothing at all if they are not.
        differing = 0
        for a in values:
            for b in values:
                differing |= a ^ b
        need = {reg * 8 + lo + i for i in range(width) if differing >> i & 1}
        for s2, r2, lo2, w2, n2 in fields_of:
            if s2 == seg and n2 != name and owned(n2):
                need -= set(range(r2 * 8 + lo2, r2 * 8 + lo2 + w2))
        if not need:
            continue
        fields.append((seg, reg, lo, width, name, sorted(values)))

    named_bits = {}
    for seg, reg, lo, width, _ in fields_of:
        for i in range((lo + width + 7) // 8):
            low, high = max(0, lo - 8 * i), min(8, lo + width - 8 * i)
            if high > low:
                key = (seg, reg + i)
                named_bits[key] = named_bits.get(key, 0) | (((1 << (high - low)) - 1) << low)

    holes = []
    for address in sorted(set().union(*(set(t) for t in tables.values()))):
        unnamed = 0xFF & ~named_bits.get(address, 0)
        if not unnamed:
            continue
        ever = 0
        for table in tables.values():
            ever |= unnamed & (table.get(address) or 0)
        if ever:
            holes.append((address, unnamed, ever))
    return fields, holes


def unowned():
    """The same, minus what the bench disqualified. See DISQUALIFIED above."""
    fields, holes = _unowned_before_disqualification()
    return ([f for f in fields if f[4] not in DISQUALIFIED],
            [h for h in holes if h[0] not in DISQUALIFIED_HOLES])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--subsystem", help="only this prefix, e.g. VDS_")
    parser.add_argument("--gap", action="store_true",
                        help="what still has no owner, and so still needs the preset")
    args = parser.parse_args()

    if args.gap:
        fields, holes = unowned()
        print(f"--- {len(fields)} named fields with no owner ---")
        for seg, reg, lo, width, name, values in sorted(fields,
                                                        key=lambda f: (f[0], f[1], f[2])):
            shown = ", ".join(hex(v) for v in values[:6])
            print(f"    s{seg}_{reg:02x}[{lo + width - 1}:{lo}]  {name:24} "
                  f"{shown}{' ...' if len(values) > 6 else ''}")
        print(f"\n--- {len(holes)} addresses with unnamed bits that a table SETS ---")
        for (seg, reg), unnamed, ever in holes:
            print(f"    s{seg}_{reg:02x}  unnamed {unnamed:08b}  ever set {ever:08b}")
        return 0

    fields = bringup_fields()
    if args.subsystem:
        fields = [f for f in fields if f[0] == args.subsystem]

    labels = dict(SUBSYSTEM)
    grouped = collections.OrderedDict()
    for field in fields:
        grouped.setdefault(field[0], []).append(field)

    for prefix, rows in grouped.items():
        print(f"// --- {labels.get(prefix, 'everything else')} "
              f"({len(rows)} fields) ---")
        for _, name, seg, reg, lo, width, value in rows:
            call = f"GBS::{name}::write(0x{value:X});"
            print(f"    {call:<52}// s{seg}_{reg:02x}[{lo + width - 1}:{lo}]")
        print()

    print(f"// {len(fields)} fields total")
    return 0


if __name__ == "__main__":
    sys.exit(main())
