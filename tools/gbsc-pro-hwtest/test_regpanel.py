"""regpanel's register reads, without a unit.

The unit answers every request with `Connection: close` — ESPAsyncWebServer does
no keep-alive — so one request costs one TCP connection, and the connection sits
in TIME-WAIT for a minute afterwards. Reading fields a register at a time is
therefore not merely slow, it starves the unit: /api/status polls every 1500 ms
and cost 27 connections a poll, ~18 a second, which is how 877 sockets ended up
in TIME-WAIT against a device whose websocket server caps at five clients.

These tests pin the batching that fixes it. They inject a counting fetch, so
they are about how many round trips a read costs and what it decodes to — never
about which URL was built.
"""

import json
import math
import urllib.parse

import regpanel


class CountingFetch:
    """A fetch(segment, first, last) -> bytes that serves from a fake device and
    counts round trips. One call is one TCP connection to the real unit."""

    def __init__(self, segments):
        self.segments = segments
        self.calls = []

    def __call__(self, segment, first, last):
        self.calls.append((segment, first, last))
        return bytes(self.segments[segment][first:last + 1])


def a_device():
    """Segment bytes with the values this bench actually had on 2026-08-05:
    PLLAD_MD 2553, IF_HSYNC_RST 1276, capture 264..1062."""
    seg0 = [0] * 0x40
    seg0[0x1B], seg0[0x1C] = 311 & 0xFF, 311 >> 8          # VTOTAL 311
    seg0[0x16] = 0x0F                                       # STATUS_16
    seg1 = [0] * 0x40
    seg1[0x2B] = 0x05                                       # preset ID
    put(seg1, 0x0E, 0, 11, 1276)                            # IF_HSYNC_RST
    put(seg1, 0x20, 0, 12, 0)                               # IF_LINE_ST
    put(seg1, 0x22, 0, 12, 1277)                            # IF_LINE_SP
    put(seg1, 0x1A, 0, 11, 264)                             # IF_HB_SP2
    put(seg1, 0x18, 0, 11, 1062)                            # IF_HB_ST2
    seg5 = [0] * 0x60
    put(seg5, 0x12, 0, 12, 2553)                            # PLLAD_MD
    put(seg5, 0x4B, 0, 12, 2374)                            # SP_RT_HS_SP
    return {0: seg0, 1: seg1, 5: seg5}


def put(segment, register, offset, width, value):
    """Lay a field into fake segment bytes, low byte first, as the chip does."""
    for i in range((offset + width + 7) // 8):
        segment[register + i] |= ((value << offset) >> (8 * i)) & 0xFF


def test_reading_many_fields_costs_one_round_trip_per_segment():
    """The defect: 8 status fields spanning 13 registers cost 13 connections."""
    fetch = CountingFetch(a_device())

    regpanel.read_fields(fetch, regpanel.STATUS)

    segments_touched = {segment for segment, _, _ in fetch.calls}
    assert len(fetch.calls) == len(segments_touched), (
        f"{len(fetch.calls)} round trips for {len(segments_touched)} segments — "
        f"one connection per segment is the budget, got {fetch.calls}"
    )


def test_batched_reads_decode_to_the_same_values_as_single_reads():
    """Batching must not change what a field is worth, including the multi-byte
    ones and the ones that start mid-byte."""
    fetch = CountingFetch(a_device())

    values = regpanel.read_fields(fetch, regpanel.STATUS)

    assert values["VTOTAL"] == 311
    assert values["STATUS_16"] == 0x0F
    assert values["preset ID"] == 0x05


def test_a_status_poll_costs_one_round_trip_per_segment():
    """The poll runs every 1500 ms and is what starved the unit. It touches
    segments 0, 1 and 5, so its budget is three connections, not 27."""
    fetch = CountingFetch(a_device())

    regpanel.read_fields(fetch, regpanel.STATUS + regpanel.INVARIANTS)

    assert len(fetch.calls) == 3, f"{len(fetch.calls)} round trips: {fetch.calls}"


def test_invariants_pass_on_the_settings_that_were_on_the_bench():
    fetch = CountingFetch(a_device())
    values = regpanel.read_fields(fetch, regpanel.STATUS + regpanel.INVARIANTS)

    failed = [c["label"] for c in regpanel.check_invariants(values) if not c["ok"]]

    assert failed == [], f"good bench settings reported as violations: {failed}"


def test_a_violated_invariant_is_reported():
    """PLLAD_MD moved without IF_HSYNC_RST following it — the fault the panel
    exists to catch, since nothing in the firmware keeps them in step."""
    fetch = CountingFetch(a_device())
    values = regpanel.read_fields(fetch, regpanel.STATUS + regpanel.INVARIANTS)
    values["PLLAD_MD"] = 1276

    failed = [c["label"] for c in regpanel.check_invariants(values) if not c["ok"]]

    assert "line length" in failed


# --- the display window is always computed ------------------------------------


def a_geometry_device(hscale=900, vscale=489, dis_h=None, dis_v=None,
                      capture_h=798):
    """The hand alignment of 2026-08-05, which the vertical model is measured
    against: capture 264..1062 horizontally and 513 half-lines vertically, in a
    1445x1125 output raster with the memory windows opened to 19..1443/19..1123.

    The display windows fit the picture, which is the only state the panel
    produces. Both axes use capture x 1024 / scale — the measured behaviour on
    both, see test_the_vertical_formula_reproduces_a_measured_picture.

    `dis_h`/`dis_v` override the aperture to build a STALE one deliberately,
    which is the bench fault of 2026-08-05: an aperture that no longer matches
    the capture, cropping the picture or exposing memory past the end of it.
    """
    seg0 = [0] * 0x40
    seg0[0x1B], seg0[0x1C] = 311 & 0xFF, 311 >> 8           # VTOTAL 311
    seg1 = [0] * 0x40
    put(seg1, 0x0E, 0, 11, 1276)                            # IF_HSYNC_RST
    put(seg1, 0x1A, 0, 11, 264)                             # IF_HB_SP2
    put(seg1, 0x18, 0, 11, 264 + capture_h)                 # IF_HB_ST2
    put(seg1, 0x1E, 0, 11, 20)                              # IF_VB_SP
    put(seg1, 0x1C, 0, 11, 533)                             # IF_VB_ST
    seg3 = [0] * 0x60
    put(seg3, 0x01, 0, 12, 1444)                            # VDS_HSYNC_RST
    put(seg3, 0x02, 4, 11, 1124)                            # VDS_VSYNC_RST
    put(seg3, 0x16, 0, 10, hscale)                          # VDS_HSCALE
    put(seg3, 0x17, 4, 10, vscale)                          # VDS_VSCALE
    put(seg3, 0x05, 4, 12, 19)                              # VDS_HB_SP
    put(seg3, 0x04, 0, 12, 1443)                            # VDS_HB_ST
    put(seg3, 0x08, 4, 11, 19)                              # VDS_VB_SP
    put(seg3, 0x07, 0, 11, 1123)                            # VDS_VB_ST
    if dis_h is None:
        dis_h = min(round(capture_h * 1024 / hscale), 1443 - 19)
    if dis_v is None:
        dis_v = min(round((533 - 20) * 1024 / vscale), 1123 - 19)
    put(seg3, 0x11, 4, 12, 19)                              # VDS_DIS_HB_SP
    put(seg3, 0x10, 0, 12, 19 + dis_h)                      # VDS_DIS_HB_ST
    put(seg3, 0x14, 4, 11, 19)                              # VDS_DIS_VB_SP
    put(seg3, 0x13, 0, 11, 19 + dis_v)                      # VDS_DIS_VB_ST
    return {0: seg0, 1: seg1, 3: seg3}


class RecordingPanel:
    """regpanel wired to a fake device: reads are served from segment bytes,
    writes are recorded by field name rather than by register."""

    def __init__(self, monkeypatch, device):
        self.wrote = {}
        self.by_spec = {spec: name
                        for name, spec in regpanel.GEOMETRY_SPECS.items()}
        self.fetch = CountingFetch(device)
        monkeypatch.setattr(regpanel, "fetch_span", self.fetch)
        monkeypatch.setattr(regpanel, "write_field", self._write)
        monkeypatch.setattr(regpanel.time, "sleep", lambda _: None)

    def _write(self, segment, register, offset, width, value):
        self.wrote[self.by_spec[(segment, register, offset, width)]] = value
        return True

    def final(self, name):
        """What the field ends up at: written if it was written, otherwise what
        it already was. A register left alone is an answer too -- the display
        window's near edge is deliberately never computed."""
        if name in self.wrote:
            return self.wrote[name]
        return regpanel.read_fields(self.fetch, regpanel.GEOMETRY_FIELDS)[name]


class RecordingEngine:
    """regpanel wired to a fake /geometry.

    Pan and zoom go to the ENGINE now, so what a press does is the request it
    makes -- not the registers it writes. The panel no longer solves anything,
    which is why the placement tests that used to live here are gone: that
    behaviour is the firmware's, and the host geometry tests and the pad tests own it.
    """

    def __init__(self, monkeypatch, held):
        self.held = dict(held)
        self.asked = []
        monkeypatch.setattr(regpanel, "_get", self._get)
        monkeypatch.setattr(regpanel.time, "sleep", lambda _: None)

    def _get(self, path, timeout=5):
        if path == "/geometry":
            return json.dumps(self.held)
        if path.startswith("/geometry?"):
            wanted = {name: int(value) for name, value in
                      urllib.parse.parse_qsl(path.split("?", 1)[1])}
            self.asked.append(wanted)
            self.held.update(wanted)
            return json.dumps(self.held)
        raise AssertionError(f"unexpected request {path}")


def test_a_pan_press_asks_the_engine_rather_than_writing_registers(monkeypatch):
    engine = RecordingEngine(monkeypatch, {"zh": 0, "zv": 0, "ph": 0, "pv": 0})

    assert regpanel.pan(4, -2)["ok"]

    assert engine.asked == [{"zh": 0, "zv": 0, "ph": 4, "pv": -2}]


def test_a_zoom_press_is_negated_into_the_engines_sense(monkeypatch):
    """**THE SIGN FLIPS.** These pads send a NEGATIVE delta to crop in, where the
    engine's zoom is POSITIVE to crop in. Getting it backwards puts the zoom pads
    the wrong way round, which the picture shows and nothing else would."""
    engine = RecordingEngine(monkeypatch, {"zh": 0, "zv": 0, "ph": 0, "pv": 0})

    assert regpanel.rescale(-6, 3)["ok"]

    assert engine.asked == [{"zh": 6, "zv": -3, "ph": 0, "pv": 0}]


def test_a_press_accumulates_on_the_framing_the_engine_holds(monkeypatch):
    """Read what the engine holds, add the delta, hand it back. The panel keeps
    no framing of its own -- one owner, and it is not this one."""
    engine = RecordingEngine(monkeypatch, {"zh": 10, "zv": 0, "ph": 5, "pv": 0})

    regpanel.pan(3, 0)

    assert engine.asked[-1] == {"zh": 10, "zv": 0, "ph": 8, "pv": 0}


def test_a_press_that_asks_for_nothing_does_not_reach_the_engine(monkeypatch):
    engine = RecordingEngine(monkeypatch, {"zh": 0, "zv": 0, "ph": 0, "pv": 0})

    assert not regpanel.pan(0, 0)["ok"]
    assert not regpanel.rescale(0, 0)["ok"]

    assert engine.asked == []


def section_fields():
    return [name for _, fields in regpanel.GROUPS for name, *_ in fields]


def test_every_register_appears_in_a_section():
    """A register only reachable by typing its name is a register nobody finds.
    PB_CAP_OFFSET was in that state while it was the subject of the
    investigation."""
    missing = sorted(set(regpanel.REGISTER_DOC) - set(section_fields()))

    assert missing == [], (
        f"{len(missing)} registers reachable only by search, e.g. {missing[:12]}")


def test_no_gbs_control_pseudo_registers_are_offered():
    """GBS_* are gbs-control's own option bits parked in chip scratch space, not
    TV5725 registers, so they belong in neither the sections nor the search."""
    assert [n for n in regpanel.REGISTER_DOC if n.startswith("GBS_")] == []
    assert [n for n in section_fields() if n.startswith("GBS_")] == []


def test_no_register_is_listed_in_two_sections():
    names = section_fields()
    duplicated = sorted({n for n in names if names.count(n) > 1})

    assert duplicated == [], f"listed more than once: {duplicated}"


def test_the_playback_offset_and_fetch_lead_the_default_view():
    """They are one quantity in two registers and are read together or not at
    all, so they open the panel side by side."""
    _, first_fields = regpanel.GROUPS[0]
    names = [name for name, *_ in first_fields]

    assert names[:2] == ["PB_CAP_OFFSET", "PB_FETCH_NUM"]


def a_full_device():
    """Every segment the panel now touches, all registers readable."""
    return {seg: [0] * 0x100 for seg in range(6)}


def test_the_whole_panel_costs_one_round_trip_per_segment():
    """Every register has a section now, so a refresh reads ~950 fields. Read a
    field at a time that is ~950 TCP connections per refresh against a unit whose
    websocket server caps at five clients — the starvation this file exists for."""
    fetch = CountingFetch(a_full_device())
    every = [f for _, fields in regpanel.GROUPS for f in fields]

    regpanel.read_fields(fetch, every)

    segments = {segment for segment, _, _ in fetch.calls}
    assert len(fetch.calls) == len(segments), (
        f"{len(fetch.calls)} round trips for {len(segments)} segments")


def test_a_capture_window_reaching_the_line_end_is_reported():
    """IF_HB_ST2 == IF_HSYNC_RST rolls the picture. The window must stay strictly
    inside the line, not merely reach its last unit -- and the check that says so
    passed on the state that rolled."""
    fetch = CountingFetch(a_device())
    values = regpanel.read_fields(fetch, regpanel.STATUS + regpanel.INVARIANTS)
    values["IF_HB_ST2"] = values["IF_HSYNC_RST"]

    failed = [c["label"] for c in regpanel.check_invariants(values) if not c["ok"]]

    assert "capture window" in failed
