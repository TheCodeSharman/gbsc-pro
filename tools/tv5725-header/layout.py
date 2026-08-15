#!/usr/bin/env python3
"""Render register fields in the datasheet's grouping, comments in a column.

The shape, which is Michael's specification:

    chapter heading   left-aligned at column 0, so it breaks the indented run
                      of declarations and the eye catches it
    one register      its fields, one blank line between each
    between registers two blank lines, so a register reads as a unit

Comments -- generated and hand-written alike -- sit in a column at 70 and wrap
into the same column. The rule for the column is not aesthetic: the longest
declaration in the header is 67 characters, so 70 is the narrowest column every
declaration clears without pushing the file wider.
"""
import textwrap

COLUMN = 70
WIDTH = 130
INDENT = "    "


def declaration(t):
    return (f"{INDENT}typedef UReg<0x{t.seg:02X}, 0x{t.reg:02X}, "
            f"{t.offset}, {t.width}> {t.name};")


def field(t, comment):
    """One typedef, with its comment wrapped into the trailing column."""
    decl = declaration(t)
    if not comment:
        return [decl]
    body = textwrap.wrap(comment, WIDTH - COLUMN - 3) or [""]
    if len(decl) >= COLUMN:
        # No declaration in this header is this long, but a future one could be,
        # and silently jamming the comment against the semicolon would be worse
        # than giving it its own line.
        out = [decl]
    else:
        out = [f"{decl}{' ' * (COLUMN - len(decl))}// {body.pop(0)}"]
    out += [f"{' ' * COLUMN}// {line}" for line in body]
    return out


def heading(number, title, seg):
    """Left-aligned at column 0, so it breaks the indented run of declarations."""
    return ["", f"// {title}", ""]


def render(groups):
    """`groups`: ordered [(chapter number, title, segment, registers)] where each
    register is an ordered list of (Typedef, comment)."""
    out = []
    for number, title, seg, registers in groups:
        out += heading(number, title, seg)
        for i, fields in enumerate(registers):
            if i:
                out += ["", ""]
            for j, (t, comment) in enumerate(fields):
                if j:
                    out.append("")
                out += field(t, comment)
    return out
