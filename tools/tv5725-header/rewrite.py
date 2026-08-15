#!/usr/bin/env python3
"""Change what tv5725.h declares, without touching how it reads.

This exists to make a commit honest. A pass over the header that both corrects
bit widths and rewrites 500 comments produces a diff nobody can review: the four
lines that change the firmware's behaviour are buried in twelve hundred that do
not. Splitting them needs a way to say "take this header and give it exactly
that set of register definitions, changing nothing else" -- which is this.

The comment attached to a field is presentation and stays with it. A field the
target does not have goes, and its comment goes with it, because a comment about
a register that is no longer declared is worse than no comment.
"""
import re

import header

COMMENT_ONLY = re.compile(r"^\s*//")


def _decl(t, indent="    "):
    return (f"{indent}typedef UReg<0x{t.seg:02X}, 0x{t.reg:02X}, "
            f"{t.offset}, {t.width}> {t.name};")


def _line_of(lines):
    """name -> index of the line declaring it."""
    out = {}
    for i, line in enumerate(lines):
        m = header.TYPEDEF.match(line)
        if m:
            out[m.group(5)] = i
    return out


def _comment_block_above(lines, i):
    """Indices of the comment lines that belong to the typedef at `i`."""
    j = i
    while j > 0 and COMMENT_ONLY.match(lines[j - 1]):
        j -= 1
    return list(range(j, i))


def apply_identities(src, target):
    """`src` reformatted not at all, but declaring exactly `target`."""
    lines = src.split("\n")
    at = _line_of(lines)
    want = {t.name: t for t in target}
    drop = set()

    for name, i in at.items():
        if name not in want:
            drop.add(i)
            drop.update(_comment_block_above(lines, i))
            continue
        t = want[name]
        m = header.TYPEDEF.match(lines[i])
        if (t.seg, t.reg, t.offset, t.width) != (
                int(m.group(1), 0), int(m.group(2), 0),
                int(m.group(3)), int(m.group(4))):
            indent = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
            lines[i] = _decl(t, indent) + lines[i][m.end():]

    # Additions go next to the field they follow in the target order, so the
    # header keeps the datasheet's grouping instead of growing a bucket of
    # new names at the end.
    insert = {}                                   # line index -> [new decls]
    order = [t.name for t in target]
    for pos, name in enumerate(order):
        if name in at:
            continue
        anchor = next((order[k] for k in range(pos - 1, -1, -1)
                       if order[k] in at), None)
        if anchor is not None:
            insert.setdefault(at[anchor] + 1, []).append(want[name])
        else:
            follow = next((order[k] for k in range(pos + 1, len(order))
                           if order[k] in at), None)
            insert.setdefault(at[follow] if follow is not None else len(lines),
                              []).insert(0, want[name])

    out = []
    for i, line in enumerate(lines):
        for t in insert.get(i, []):
            out.append(_decl(t))
        if i not in drop:
            out.append(line)
    for t in insert.get(len(lines), []):
        out.append(_decl(t))
    return "\n".join(out)


COMMENT_COL = 70
WRAP_AT = 118


def _wrap(text, column):
    """`text` as // lines, each starting at `column`."""
    room = max(28, WRAP_AT - column - 3)
    words, lines, cur = text.split(), [], ""
    for w in words:
        if cur and len(cur) + 1 + len(w) > room:
            lines.append(cur)
            cur = w
        else:
            cur = f"{cur} {w}".strip()
    if cur:
        lines.append(cur)
    return lines


def apply_wordings(src, wordings):
    """`src` with every field's comment replaced by `wordings[name]`.

    Declarations are untouched, so header.compare() returning [] is a proof
    rather than a hope. A name absent from `wordings` is left bare: a withdrawn
    description is worse than none, because it still reads as attested.

    Section headings survive. They title the block below them rather than
    describing a field, and sweeping one into the first field's comment is how
    CAPTURE_ENABLE came to be documented as "Playback / Capture / Memory
    Registers".
    """
    lines = src.split("\n")
    out, i = [], 0
    while i < len(lines):
        m = header.TYPEDEF.match(lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue

        decl = lines[i][:m.end()].rstrip()
        # A wrapped trailing comment is indented past the declaration; a
        # comment-only line at the declaration indent introduces the field
        # BELOW and is not ours to consume. Same column rule as annotated().
        column = len(lines[i]) - len(lines[i][m.end():].lstrip()) \
            if "//" in lines[i][m.end():] else None
        i += 1
        while column is not None and i < len(lines) and COMMENT_ONLY.match(lines[i]):
            if len(lines[i]) - len(lines[i].lstrip()) < column:
                break
            i += 1

        text = wordings.get(m.group(5), "").strip()
        if not text:
            out.append(decl)
            continue
        at = max(COMMENT_COL, len(decl) + 2)
        chunks = _wrap(text, at)
        out.append(f"{decl}{' ' * (at - len(decl))}// {chunks[0]}")
        out.extend(f"{' ' * at}// {c}" for c in chunks[1:])
    return "\n".join(out)
