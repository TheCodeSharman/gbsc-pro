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
ADC_H = os.path.join(SRC, "Adc.h")
SAMPLING_CLOCK_H = os.path.join(SRC, "SamplingClock.h")
INPUT_FORMATTER_H = os.path.join(SRC, "InputFormatter.h")
VIDEO_SOURCE_LINE_H = os.path.join(SRC, "VideoSourceLine.h")
SYNC_PROCESSOR_H = os.path.join(SRC, "SyncProcessor.h")

# The integer suffixes are part of the value's spelling, not of the value: an
# unsigned constant is written `162000000u` and a reader that insists on bare
# digits refuses it as though the name were absent.
INTEGER = re.compile(r"static\s+const\s+\w+\s+(\w+)\s*=\s*"
                     r"(0x[0-9a-fA-F]+|\d+)[uUlL]*\s*;")
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


def expected_divider(lines, oversample, field_rate, doubled):
    """The divider SamplingClock::recommendedDivider() would solve, 0 for a
    line rate it would refuse.

    An approximation of it rather than a second copy: the oversampling is taken
    as given instead of searching the ratios, and the row ceiling is read off
    the ADC's rating directly. Close enough to score a divider against, the two
    agreeing inside a percent on the bench source.

    **THE BOUNDS ARE NOT APPROXIMATE AND HAVE TO BE APPLIED.** Each is a
    hardware fact rather than a backoff, and on the bench source the cap is
    what BINDS -- 2200 against the 2551 the crossover row affords, which is 14%
    and well past any tolerance a caller would set. A doubled line is held under
    DoubledLineSampleLimit because the capture path stops writing video past
    sample 2236..2256 of one, measured with the VDS line delay bypassed so it is
    the capture path and not the filter, and the eleven-bit line counter takes
    the RAW line so only the decimation separates it from the divider.
    ../../docs/known-issues.md
    """
    line_rate = int(field_rate * lines)
    if line_rate == 0 or oversample < 1:
        return 0

    rating = integer(ADC_H, "MaxSampleRateHz") // (line_rate * oversample)
    ceiling = min(rating, integer(ADC_H, "DividerMax"))
    ceiling = (ceiling * integer(SAMPLING_CLOCK_H, "RecommendedPercent")) // 100
    ceiling = min(ceiling, integer(INPUT_FORMATTER_H, "LineCounterMax")
                  * (2 if doubled else 1))
    if doubled:
        ceiling = min(ceiling, integer(SAMPLING_CLOCK_H, "DoubledLineSampleLimit"))

    # Even, so the input formatter's line counter divides the divider exactly.
    return ceiling & ~1


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
