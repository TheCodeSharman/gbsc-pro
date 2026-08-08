"""preset_drift's split of a register diff into owned and unexplained.

The whole value of the tool is the classification: a raw diff between a
Python-driven state and a firmware-driven one is dominated by the geometry
engine doing its job, and the few registers nobody in the geometry path claims
are the ones worth a bench session.
"""

import preset_drift


def snapshot(**fields):
    """A byte map with the given field values, via the real register map."""
    registers = preset_drift.load_map()
    values = {}
    for name, value in fields.items():
        spec = registers[name]
        span = (spec["off"] + spec["width"] + 7) // 8
        raw = value << spec["off"]
        for index in range(span):
            key = (spec["seg"], spec["reg"] + index)
            values[key] = values.get(key, 0) | ((raw >> (8 * index)) & 0xFF)
    return values


def classify(before, after):
    registers = preset_drift.load_map()
    owned, unexplained = preset_drift.drift(
        snapshot(**before), snapshot(**after), registers
    )
    return ([row[0] for row in owned], [row[0] for row in unexplained])


def test_a_register_the_engine_writes_is_owned():
    owned, _ = classify({"VDS_HSCALE": 648}, {"VDS_HSCALE": 762})
    assert "VDS_HSCALE" in owned


def test_a_register_the_engine_never_writes_is_unexplained():
    _, unexplained = classify({"VDS_HS_ST": 32}, {"VDS_HS_ST": 8})
    assert "VDS_HS_ST" in unexplained


def test_the_output_sync_pulse_is_not_claimed_by_the_engine():
    # geometry_regs.h deliberately leaves VDS_HS_ST/SP to applyBestHTotal, so a
    # change here must surface rather than be absorbed as expected.
    _, unexplained = classify({"VDS_HS_SP": 217}, {"VDS_HS_SP": 56})
    assert "VDS_HS_SP" in unexplained


def test_an_unchanged_register_is_reported_nowhere():
    owned, unexplained = classify({"VDS_HSCALE": 648}, {"VDS_HSCALE": 648})
    assert owned == [] and unexplained == []


def test_the_capture_window_set_two_is_owned_but_set_zero_is_not():
    # Three horizontal blanking sets exist; the engine writes only set 2. Set 0
    # moving is exactly the drift this tool was written to catch.
    owned, unexplained = classify(
        {"IF_HB_ST2": 1039, "IF_HB_ST": 2},
        {"IF_HB_ST2": 1074, "IF_HB_ST": 258},
    )
    assert "IF_HB_ST2" in owned
    assert "IF_HB_ST" in unexplained
