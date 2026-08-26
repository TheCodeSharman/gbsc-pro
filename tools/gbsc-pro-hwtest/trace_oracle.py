"""Turn a GBS_TRACE_WRITES serial trace into an ordered write oracle.

The trace is the equivalence oracle for branches no bench source can reach: a
state dump cannot see ordering, and the PLLAD latch fault leaves every register
reading correct while the PLL runs the previous divider.

A line is `<millis> W <reg>:<bytes>`. Writes to 0xF0 are segment selects, so the
segment of every following write is recoverable. A multi-byte write covers
consecutive registers from `reg`.

Helpers that read live measurements -- updateSpDynamic(), optimizeSogLevel(),
the settle loops -- write different values run to run against a source that does
not match the forced standard. So the oracle is the ORDERED subsequence common
to every run; what differs is recorded as variable rather than asserted.
"""

import re

WRITE = re.compile(r"^(\d+) W ([0-9A-Fa-f]{2}):([0-9A-Fa-f]+)$")
SEGMENT_SELECT = 0xF0

# Registers that move between CAPTURE SESSIONS even when three runs inside one
# session agree, so three runs cannot filter them and a whole-trace comparison
# reports them as a regression. Both are measured, not assumed:
#
#   s5_19  PA_SP_S -- optimizePhaseSP()'s live 34-step sweep output.
#   s0_49  PAD_SYNC_OUT_ENZ (bit 2) -- useHdmiSyncFix toggles it only when the
#          classification swaps inside the SD 50/60 families, so whether it
#          fires depends on the standard captured before this one.
#
# Excluded from compare() by default and REPORTED, never silently dropped: a
# comparison that hides what it ignored reads as proving more than it did.
SESSION_VARIABLE = {(5, 0x19), (0, 0x49)}


def parse(lines):
    """Trace lines to an ordered list of (segment, register, value).

    Segment selects are consumed, not emitted: they are how the segment is
    known, not writes the firmware is making on purpose.
    """
    out = []
    segment = None
    for line in lines:
        m = WRITE.match(line.strip())
        if not m:
            continue
        reg = int(m.group(2), 16)
        raw = m.group(3)
        values = [int(raw[i:i + 2], 16) for i in range(0, len(raw), 2)]
        if reg == SEGMENT_SELECT:
            segment = values[0]
            continue
        if segment is None:
            # A write before any segment select cannot be placed. Recorded as
            # segment -1 rather than dropped, so a trace that starts mid-stream
            # is visibly wrong instead of quietly short.
            segment = -1
        for offset, value in enumerate(values):
            out.append((segment, reg + offset, value))
    return out


def common_subsequence(a, b):
    """The longest ordered subsequence present in both."""
    # Hunt-style is unnecessary here: a load is a few hundred writes.
    n, m = len(a), len(b)
    table = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        row, nxt = table[i], table[i + 1]
        for j in range(m - 1, -1, -1):
            row[j] = nxt[j + 1] + 1 if a[i] == b[j] else max(nxt[j], row[j + 1])
    out, i, j = [], 0, 0
    while i < n and j < m:
        if a[i] == b[j]:
            out.append(a[i]); i += 1; j += 1
        elif table[i + 1][j] >= table[i][j + 1]:
            i += 1
        else:
            j += 1
    return out


def oracle(runs):
    """The ordered writes every run made, and how many each run made.

    Fewer than two runs cannot distinguish a stable write from a variable one,
    so it refuses rather than returning a sequence that looks authoritative.
    """
    if len(runs) < 2:
        raise ValueError("an oracle needs at least two runs to tell stable from variable")
    stable = parse(runs[0])
    for run in runs[1:]:
        stable = common_subsequence(stable, parse(run))
    return {
        "stable": stable,
        "runLengths": [len(parse(r)) for r in runs],
        "variable": [len(parse(r)) - len(stable) for r in runs],
    }


def compare(before, after, registers=None, ignore=SESSION_VARIABLE):
    """Two oracles, as an equivalence verdict.

    `registers` scopes the comparison to the (segment, register) pairs a change
    touches. Scoping is the honest move when a change is known to be local: an
    unscoped comparison across sessions fails on writes nothing did on purpose.
    """
    def keep(seq):
        return [w for w in seq
                if (w[0], w[1]) not in ignore
                and (registers is None or (w[0], w[1]) in registers)]

    a, b = keep(before), keep(after)
    ignored = sorted({(w[0], w[1]) for w in list(before) + list(after)
                      if (w[0], w[1]) in ignore})
    return {
        "equivalent": a == b,
        "compared": len(a),
        "ignored": ignored,
        "onlyBefore": [w for w in a if w not in b],
        "onlyAfter": [w for w in b if w not in a],
    }
