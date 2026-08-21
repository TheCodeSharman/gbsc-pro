#!/usr/bin/env python3
"""Reading and writing TV5725 bitfields, with the transport injected.

Every geometry quantity on this chip is a bitfield straddling one or two
registers at some offset, sharing space with unrelated controls. Setting one
therefore means read-modify-write, and getting that wrong clears a neighbouring
bit that nobody is watching -- the same class of fault as the bulk preset load
that cleared DAC_RGBS_B0ENZ and turned the picture yellow, arriving by a
different route.

`test_bench_probe` drives it against an in-memory register file; on the bench the
transport is `gbs_unit`'s HTTP calls.
"""


class WriteFailed(Exception):
    """A field write that did not reach the chip.

    Loud rather than a return value, because the return value was never checked
    at any of the call sites and a silent no-op reads as a field that has no
    effect -- a measurement recorded from a write that never happened.
    """


def _span(offset, width):
    """Registers a field of this shape covers, low byte first."""
    return (offset + width + 7) // 8


class Probe:
    def __init__(self, read_register, write_register):
        self._read_register = read_register
        self._write_register = write_register

    def _read_raw(self, segment, register, offset, width):
        raw = 0
        for i in range(_span(offset, width)):
            byte = self._read_register(segment, register + i)
            if byte is None:
                return None
            raw |= byte << (8 * i)
        return raw

    def read_field(self, spec):
        """The field's value, or None if any register behind it did not read.

        None rather than a default: a geometry measurement recorded from a
        register that was never read is worse than no measurement.
        """
        _, segment, register, offset, width = spec
        raw = self._read_raw(segment, register, offset, width)
        if raw is None:
            return None
        return (raw >> offset) & ((1 << width) - 1)

    def write_field(self, spec, value):
        """Set the field, preserving every other bit in the registers it shares.

        Raises WriteFailed if it could not be done, rather than reporting it in
        a return value nobody reads.

        Out-of-range values are clamped, not wrapped: a wrapped display edge puts
        the picture at the far side of the raster, which reads as a dead unit
        rather than as a bad argument.
        """
        name, segment, register, offset, width = spec
        span = _span(offset, width)
        value = max(0, min(int(value), (1 << width) - 1))
        raw = self._read_raw(segment, register, offset, width)
        if raw is None:
            raise WriteFailed(
                f"{name}: cannot write, a register behind it did not read "
                f"(s{segment} 0x{register:02x}, {span} bytes)")
        mask = ((1 << width) - 1) << offset
        raw = (raw & ~mask) | (value << offset)
        for i in range(span):
            if not self._write_register(segment, register + i,
                                        (raw >> (8 * i)) & 0xFF):
                raise WriteFailed(
                    f"{name}: s{segment} 0x{register + i:02x} did not write. "
                    f"Byte {i} of {span}, so the field may be half set.")
        return True
