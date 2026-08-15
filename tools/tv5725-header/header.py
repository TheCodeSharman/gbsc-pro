#!/usr/bin/env python3
"""Read tv5725.h as data: what registers it declares, and what it says about them.

The header is 2000 lines of `typedef UReg<seg, reg, offset, width> NAME;` and
nothing else mechanical, which makes it easy to reformat and dangerously easy to
reformat *wrongly*. Every field is a bit-slice the firmware reads and writes, so
a dropped typedef or a shifted width is a silent behaviour change that compiles.

So the point of this module is the split between the two things a header line
carries:

    identity      seg, reg, offset, width -- what the firmware actually does
    presentation  the name's comment, and where the whitespace goes

A formatting pass may move any amount of presentation and must move no identity
at all. `identities()` is what makes that checkable rather than hopeful.
"""
import re
from collections import namedtuple

Typedef = namedtuple("Typedef", "name seg reg offset width")

COMMENT_ONLY = re.compile(r"^\s*//")

TYPEDEF = re.compile(
    r"^\s*typedef\s+UReg<\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,"
    r"\s*(\d+)\s*,\s*(\d+)\s*>\s*([A-Za-z_]\w*)\s*;")


def typedefs(src):
    """Every register field the header declares, in the order it declares them."""
    out = []
    for line in src.split("\n"):
        m = TYPEDEF.match(line)
        if m:
            out.append(Typedef(name=m.group(5), seg=int(m.group(1), 0),
                               reg=int(m.group(2), 0), offset=int(m.group(3)),
                               width=int(m.group(4))))
    return out


def identities(src):
    """name -> where it points. Order is presentation, so it is dropped here."""
    return {t.name: t[1:] for t in typedefs(src)}


# Section titles that no pattern can tell from a short description. "Mode
# Detect" and "HDBypass" read exactly like field documentation, so they are
# listed rather than guessed at -- a rule loose enough to catch them would
# delete real comments. Both were missed on the first pass and became field
# descriptions.
NAMED_HEADINGS = {"hdbypass", "mode detect"}


def is_heading(comment):
    """A comment that titles the block below it rather than describing a field.

    gbs-control's own section markers. They must not be swept into the first
    field's description -- an earlier pass did that to STATUS_00, and the
    regrouping did it to four more, one of which (CAPTURE_ENABLE) ended up
    documented as "Playback / Capture / Memory Registers" while the datasheet
    text it should have carried went unused. They are dropped wholesale when the
    header is regrouped, since the datasheet chapters replace them.
    """
    text = comment.strip()
    # Every section title in this file ENDS with "Registers", plural, give or
    # take trailing punctuation. Matching the word anywhere is too greedy: it
    # eats "whole register for convenience", which is a description carried by
    # six fields.
    return bool(text.strip("/ ").lower() in NAMED_HEADINGS
                or re.search(r"\bregisters\s*[-/ ]*$", text, re.I)
                or re.match(r"^Arbitary names", text)
                or re.fullmatch(r"[A-Z_]+\s*-*", text))


def annotated(src):
    """Every field with the comment attached to it, in declaration order.

    A field's comment may sit above the typedef, trail it, or wrap across both,
    and the three are interchangeable -- which is the whole difficulty. Layout
    is discarded here so that what remains is only what the header *says*.
    """
    lines = src.split("\n")
    out, pending = [], []
    consumed = set()
    for i, line in enumerate(lines):
        m = TYPEDEF.match(line)
        if not m:
            if COMMENT_ONLY.match(line):
                if i not in consumed:
                    pending.append(line.split("//", 1)[1])
            elif line.strip():
                pending = []
            continue
        text = [t for t in pending if not is_heading(t)]
        trailing = line[m.end():]
        # A comment-only line between two typedefs is ambiguous -- it wraps the
        # one above or introduces the one below -- and only the column it starts
        # in tells them apart. A wrapped trailing comment is indented to the
        # comment column; a description for the field below sits at the
        # declaration indent. Reading the ambiguity the other way is what made
        # an earlier conversion pass alternate field by field, and it is why
        # ADC_5_00 ("convenience") appeared to own ADC_CLK_PA's description.
        if "//" in trailing:
            text.append(trailing.split("//", 1)[1])
            column = len(line) - len(line[m.end():].lstrip())
            for j in range(i + 1, len(lines)):
                if not COMMENT_ONLY.match(lines[j]):
                    break
                if len(lines[j]) - len(lines[j].lstrip()) < column:
                    break
                text.append(lines[j].split("//", 1)[1])
                consumed.add(j)
        joined = " ".join(" ".join(t.split()) for t in text).strip()
        # Tested once assembled, not only on the lines above the declaration: a
        # previous regrouping moved two headings INTO the trailing column, where
        # filtering `pending` alone could never see them again.
        if is_heading(joined):
            joined = ""
        out.append((Typedef(name=m.group(5), seg=int(m.group(1), 0),
                            reg=int(m.group(2), 0), offset=int(m.group(3)),
                            width=int(m.group(4))), joined))
        pending = []
    return out


def wordings(src):
    """name -> what its comment says, with every trace of layout removed.

    Collapsing to one whitespace-free string is what lets a reflow be told apart
    from a rewrite: a commit that only moves comments leaves this identical, and
    a commit that documents something new cannot.
    """
    return {t.name: "".join(c.split()) for t, c in annotated(src)}


def duplicates(src):
    """Names declared more than once, which `identities` would collapse."""
    seen, dup = set(), []
    for t in typedefs(src):
        if t.name in seen and t.name not in dup:
            dup.append(t.name)
        seen.add(t.name)
    return dup


def aliases(src):
    """[(bits, [names])] where one field is declared under more than one name.

    The inverse of duplicates(), and the more dangerous of the two. A repeated
    NAME collapses in identities() and is loud; a repeated FIELD does not
    collapse at all -- both names compile, both address the same bits, and
    nothing says which one was meant.

    That is how thirteen of 2c5af95's invented names survived it: a fragment
    lifted from a wrapped PDF line (AIN from "...GAIN", _GAIN from
    "SVM gain control") lands on the SAME bits as the real field, so it looks
    like a second valid name rather than an error.
    """
    by_bits = {}
    for t in typedefs(src):
        by_bits.setdefault(t[1:], set()).add(t.name)
    return [(bits, sorted(names))
            for bits, names in sorted(by_bits.items()) if len(names) > 1]


def fragments(src):
    """Names that are tails of a wrapped PDF line rather than fields.

    Every real TV5725 field name carries a module prefix -- VDS_, SP_, MEM_ --
    so every one of them contains an underscore. Across RD-5725-1.1 the only
    names that do not are wrap fragments, plus RSERVED, the PDF's own typo for
    RESERVED. fielddocs.py drops them from the docs parse; this is the same rule
    applied to the header, which had no guard at all.

    Complementary to aliases(), not a replacement. A fragment landing on the
    EXACT bits of a real field (AIN on VDS_BLEV_GAIN) is an alias and that
    catches it; one landing on a strict SUBRANGE (ZE1 inside VDS_VSYN_SIZE1)
    collides with nothing and only the missing underscore gives it away.
    """
    return sorted({t.name for t in typedefs(src) if "_" not in t.name})


def where(bits):
    seg, reg, offset, width = bits
    return f"s{seg} 0x{reg:02X} offset {offset} width {width}"


def compare(before, after):
    """What changed about the registers themselves. Empty means formatting only."""
    was, now = identities(before), identities(after)
    out = []
    for name in sorted(set(was) - set(now)):
        out.append(f"removed {name} ({where(was[name])})")
    for name in sorted(set(now) - set(was)):
        out.append(f"added   {name} ({where(now[name])})")
    for name in sorted(set(was) & set(now)):
        if was[name] != now[name]:
            out.append(f"moved   {name}: {where(was[name])} -> {where(now[name])}")
    return out
