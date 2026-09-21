"""The jog writes on every keypress, so its write path is the tool.

A creep that cannot write is indistinguishable from a unit that will not take
the value: the prompt advances, the screen does not move, and the operator reads
it as the register being ignored.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import creep_bypass_window
import setfield


def test_a_keypress_writes_the_field_it_names(monkeypatch):
    held = {0x3D: 0xAF, 0x3E: 0x01}  # HD_HB_SP holding 431
    written = []

    monkeypatch.setattr(setfield.gbs_unit, "read_reg",
                        lambda host, segment, register: held.get(register, 0))
    def accepted(host, segment, register, value):
        written.append((segment, register, value))
        return {"ok": True}

    monkeypatch.setattr(creep_bypass_window, "write_reg", accepted)

    assert creep_bypass_window.write_named("unit", "HD_HB_SP", 445)
    assert written == [(1, 0x3D, 0xBD)]


def test_a_neighbour_sharing_the_byte_survives():
    """HD_HB_SP's top nibble shares s1_3e with HD_HB_ST's, so a jog that wrote
    the byte would take the far edge with it."""
    spec = setfield.load_map()["HD_HB_SP"]
    assert spec["seg"] == 1 and spec["width"] == 12
