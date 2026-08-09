"""Unit tests for the fault characterisation recorder. No hardware needed."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import characterise


# A settled bench state: 1009 unit capture, HSCALE 823, and a display window
# wide enough to hold the whole picture.
BENCH = {
    "VDS_HSCALE": 823, "VDS_VSCALE": 486,
    "IF_HB_SP2": 134, "IF_HB_ST2": 1143,
    "IF_VB_SP": 46, "IF_VB_ST": 578,
    "VDS_HB_SP": 9, "VDS_HB_ST": 1443,
    "VDS_VB_SP": 1, "VDS_VB_ST": 1124,
    "VDS_DIS_HB_SP": 95, "VDS_DIS_HB_ST": 1348,
    "VDS_DIS_VB_SP": 3, "VDS_DIS_VB_ST": 1120,
    "IF_HSYNC_RST": 1276, "VDS_HSYNC_RST": 1444, "VDS_VSYNC_RST": 1125,
    "PLLAD_MD": 2553,
    "IF_LD_RAM_BYPS": 0, "IF_LD_SEL_PROV": 1, "IF_PRGRSV_CNTRL": 1,
    "WFF_LINE_FLIP": 0, "RFF_LINE_FLIP": 0, "RFF_ADR_ADD_2": 0,
    "MADPT_PD_RAM_BYPS": 1,
    "STATUS_IF_INP_INT": 0,
    "STATUS_SYNC_PROC_VTOTAL": 311, "STATUS_SYNC_PROC_HLOW_LEN": 181,
}


def reader(**overrides):
    state = dict(BENCH, **overrides)

    def read(name):
        return state.get(name)
    return read


def test_an_observation_records_every_co_varying_register():
    # The point of the tool: an OSD sweep moves the capture and both windows
    # along with the scale, so a row that held only VDS_HSCALE could never say
    # which of them tracked the fault.
    row = characterise.observe(reader(), "onset-1", "glitch")
    for name in characterise.FIELDS:
        assert name in row["registers"], f"{name} not recorded"


def test_the_output_hsync_pulse_is_recorded():
    # The output hsync pulse is a third quantity that flips the fault. Leaving it
    # out of a row is the same aliasing that invalidated the first dataset, on a
    # different axis.
    row = characterise.observe(reader(), "x", "clean")
    assert "VDS_HS_ST" in row["registers"]
    assert "VDS_HS_SP" in row["registers"]


def test_the_playback_burst_registers_are_recorded():
    # PB_FETCH_NUM 256 -> 204 is what cleared the fault, and 519 rows were
    # recorded without it in them. Tuning it between presses -- which is what
    # happens once a fix is being refined -- makes rows differ in the one thing
    # the row does not carry. Same aliasing as the first dataset, on the axis
    # that now matters most.
    row = characterise.observe(reader(), "x", "clean")
    for name in ("PB_FETCH_NUM", "PB_CAP_OFFSET", "PB_REQ_SEL"):
        assert name in row["registers"], f"{name} not recorded"


def test_every_field_resolves_in_the_register_map():
    # Twelve of these were hand-typed wrong before the tool resolved them
    # through the map, which would have filled a table with plausible numbers
    # read from the wrong registers.
    import json, os
    here = os.path.dirname(os.path.abspath(characterise.__file__))
    with open(os.path.join(here, "tv5725_registers.json")) as f:
        regs = json.load(f)
    missing = [n for n in characterise.FIELDS if n not in regs]
    assert missing == [], f"not in tv5725_registers.json: {missing}"


def test_the_verdict_and_the_photo_travel_with_the_registers():
    row = characterise.observe(reader(), "peak-1", "peak",
                               note="every second line", photo="IMG_4021.jpeg")
    assert row["verdict"] == "peak"
    assert row["note"] == "every second line"
    assert row["photo"] == "IMG_4021.jpeg"


def test_a_dropped_read_is_recorded_as_a_hole_not_dropped():
    # A missing cell is a fact about the row. Silently omitting it would let a
    # later reader assume the register was fine.
    row = characterise.observe(reader(VDS_HSCALE=None), "onset-1", "glitch")
    assert row["registers"]["VDS_HSCALE"] is None


def test_a_picture_inside_its_display_window_is_measurable():
    # produced = capture x 1024 / scale = 1009 x 1024 / 823 = 1255.4 px, against
    # a display window of 1348 - 95 = 1253... which is 2 px short.
    row = characterise.observe(reader(VDS_DIS_HB_ST=1500), "clean-1", "clean")
    assert characterise.picture_wider_than_display(row) is False


def test_a_picture_overrunning_its_display_window_is_flagged():
    # The retracted headroom rule was measured exactly here: readings taken with
    # 24 to 163 px of picture outside the aperture, where the tearing it was
    # meant to detect could not be seen.
    row = characterise.observe(reader(VDS_DIS_HB_ST=1000), "glitch-1", "glitch")
    assert characterise.picture_wider_than_display(row) is True


def test_an_unreadable_row_is_not_judged_either_way():
    row = characterise.observe(reader(VDS_HSCALE=None), "onset-1", "glitch")
    assert characterise.picture_wider_than_display(row) is None


def test_the_table_shows_every_row(tmp_path):
    path = str(tmp_path / "index.jsonl")
    characterise.append(path, characterise.observe(reader(), "a", "clean"))
    characterise.append(path, characterise.observe(reader(VDS_HSCALE=795), "b", "glitch"))

    rendered = characterise.table(characterise.load(path))
    assert "a" in rendered and "b" in rendered
    assert "823" in rendered and "795" in rendered


def test_an_index_that_does_not_exist_yet_is_empty_not_an_error(tmp_path):
    assert characterise.load(str(tmp_path / "nothing.jsonl")) == []
