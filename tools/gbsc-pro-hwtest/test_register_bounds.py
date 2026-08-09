"""Every position register must be inside the raster it is a position in.

docs/tv5725-chip.md states the consequence -- "an edge at or beyond IF_HSYNC_RST
is out of bounds, blanking then never starts, the line buffer wraps" -- and a
fact in prose is not a check: `IF_HB_ST` holding **1347 on a 1277 unit line**
took a nine round bisect to find. The same run found `IF_LINE_SP` at 1341 in a
state that had just been confirmed clean by eye, so an out-of-range register does
not have to be visible to be there.

Where those values come from: writes landing in the wrong segment.
`tw::SegmentedSlave::setSeg()` caches where it last aimed the chip's register
window and skips the write when the segment has not changed, so once anything
moves the pointer, every later access to that segment agrees with the cache and
goes wherever the pointer strayed to. Segment 1 then holds segment 3's bytes at
the same offsets -- `IF_HB_ST` reads 1348, which is `VDS_DIS_HB_ST` -- and
segment 3 holds segment 1's back. test/test_segment_select.cpp pins that off the
board, where it is deterministic.

**THIS IS A GUARD, NOT A REPRODUCTION.** The runtime fault is a race and does not
reproduce on demand: a self-contained provocation -- twelve pad presses against
three threads hammering whole-segment reads -- passed three times out of three
against the unfixed firmware, while one full `pytest --host=... --source` run
reproduced it first time. A test that passes on broken firmware is worse than no
test, so that provocation is not here. This is the standing check, which costs
one pass over the register map and fails whether the damage arrived this run or
last week.
"""

import json
import os

import pytest

import bench_probe
import inrange
from gbs_unit import read_reg


HERE = os.path.dirname(os.path.abspath(__file__))

# Out of range, and NOT a stray write -- so excluded here rather than left to
# fail forever next to the ones that would be real.
#
# IF_LINE_SP reads 1341 on a 1277 unit line and was carried as damage for a day
# on the strength of "nothing writes it". Something does:
# gbs-control.ino:8525 computes ((pll_divider / 2) + 1) + 0x40, and with
# PLLAD_MD 2553 that is 1276 + 1 + 64 = 1341 exactly. IF_LINE_ST is 0x40 at the
# same time, so the pair spans precisely one line length offset by 64, which
# reads as deliberate rather than as an accident.
#
# Which leaves a question nobody has settled: either the formula is wrong, or
# IF_LINE_ST/SP are positions in a counter that legitimately runs past
# IF_HSYNC_RST and inrange is measuring them against the wrong raster. The
# preset tables do not decide it -- ntsc_240p ships 1237, inside the line. Until
# it is settled, a guess either way would be worse than the note.
KNOWN_OPEN = {"IF_LINE_SP"}


@pytest.fixture
def registers():
    with open(os.path.join(HERE, "tv5725_registers.json")) as f:
        return json.load(f)


def test_every_position_register_is_inside_its_raster(host, registers):
    probe = bench_probe.Probe(lambda s, r: read_reg(host, s, r),
                              lambda s, r, v: None)

    def read(name):
        entry = registers.get(name)
        if entry is None:
            return None
        return probe.read_field((name, entry["seg"], entry["reg"],
                                 entry["off"], entry["width"]))

    findings = [f for f in inrange.check(read) if f.name not in KNOWN_OPEN]
    assert not findings, (
        "a position register is outside its own raster:\n"
        + inrange.report(findings))
