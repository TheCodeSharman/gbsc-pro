"""The firmware's own constants, read out of the C++ rather than copied here.

A copied constant diverges in silence, and the test that is meant to pin it
cannot see the drift when it asserts the copy against itself:
`TransmittedWindowDelayPx` moved to 6 in the firmware and the mirror stayed at
20, which puts the modelled encoder window 14 units late and mis-attributes the
black at the near edge of every record a sweep writes.

Parsed, a constant the firmware moves moves with it, and one the firmware
RENAMES fails the lookup rather than keeping a stale value. Every accessor
raises where the name is not in the source, so nothing here can answer with a
default. test_firmware_constants.py asks for each name the tools use.
"""

import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "GBSC-Pro-Source code", "gbs-control", "src", "tv5725")

OUTPUT_MODE_H = os.path.join(SRC, "OutputMode.h")
OUTPUT_MODE_CPP = os.path.join(SRC, "OutputMode.cpp")
OUTPUT_WINDOW_CPP = os.path.join(SRC, "OutputWindow.cpp")
AXIS_CPP = os.path.join(SRC, "Axis.cpp")
SCALE_H = os.path.join(SRC, "Scale.h")
DISPLAY_CLOCK_CPP = os.path.join(SRC, "DisplayClock.cpp")

INTEGER = re.compile(r"static\s+const\s+\w+\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;")
MODE_ROW = re.compile(r"const OutputMode Mode(\w+)\(\s*\"[^\"]*\"\s*,([\d,\s]+)\)")
WRITE_START = re.compile(r"static const WriteStart (\w+)\s*=\s*\{\s*([\d.]+)f?\s*,"
                         r"\s*([\d.]+)f?\s*,\s*(\d+)\s*\}")
AXIS_ROW = re.compile(r"const Axis Axis(\w+)\(\s*(\d+)\s*,\s*(\d+)\s*,"
                      r"\s*([\d.]+)f\s*,\s*([\d.]+)f")
SEED_ARRAY = re.compile(r"Seeds\[[^\]]*\]\s*=\s*\{([^}]*)\}", re.S)
SEED_CASE = re.compile(r"case\s+(0x[0-9a-fA-F]+)\s*:\s*return\s+(\d+)\s*;")


def _text(path):
    with open(path, encoding="utf-8") as source:
        return source.read()


def integer(path, name):
    """One `static const <type> <name> = <number>;`, in whichever base it is
    written in."""
    for found, value in INTEGER.findall(_text(path)):
        if found == name:
            return int(value, 0)
    raise KeyError(f"{name} is not a static const integer in {path}")


def output_modes():
    """OutputMode.cpp's table, by the name its instance carries, each row in the
    constructor's own argument order. Bypass is left out: it states no raster at
    all, so every field of it is zero and nothing here can be asked of it."""
    rows = {}
    for name, numbers in MODE_ROW.findall(_text(OUTPUT_MODE_CPP)):
        row = tuple(int(v) for v in numbers.split(",") if v.strip())
        if any(row):
            rows[name] = row
    if not rows:
        raise KeyError(f"no OutputMode rows in {OUTPUT_MODE_CPP}")
    return rows


def write_start(axis):
    """OutputWindow::writeStart()'s (constant, perMagnification, floor)."""
    for found, constant, per, floor in WRITE_START.findall(_text(OUTPUT_WINDOW_CPP)):
        if found == axis:
            return (float(constant), float(per), int(floor))
    raise KeyError(f"no WriteStart {axis} in {OUTPUT_WINDOW_CPP}")


def axis(name):
    """Axis.cpp's (captureGranularity, captureMargin, activeStart, activeExtent)."""
    for found, granularity, margin, start, extent in AXIS_ROW.findall(_text(AXIS_CPP)):
        if found == name:
            return (int(granularity), int(margin), float(start), float(extent))
    raise KeyError(f"no Axis{name} in {AXIS_CPP}")


def clock_seeds():
    """The display clock's seeds in rate order -- the bytes DisplayClock::Seeds
    offers, each through hzFor()."""
    text = _text(DISPLAY_CLOCK_CPP)
    array = SEED_ARRAY.search(text)
    if not array:
        raise KeyError(f"no Seeds array in {DISPLAY_CLOCK_CPP}")
    rates = dict(SEED_CASE.findall(text))
    seeds = []
    for byte in re.findall(r"0x[0-9a-fA-F]+", array.group(1)):
        if byte.lower() not in {k.lower() for k in rates}:
            raise KeyError(f"hzFor() states no rate for seed {byte}")
        seeds.append(int(next(v for k, v in rates.items()
                              if k.lower() == byte.lower())))
    return tuple(sorted(seeds))
