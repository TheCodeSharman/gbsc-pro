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

import math

import geometry_math as gm
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


def test_scaling_moves_the_capture_so_more_source_comes_into_view(monkeypatch):
    """This asserted the opposite until 2026-08-06 -- that the capture was the
    user's and a scale step must not touch it. That is exactly what stopped the
    control from ever revealing more of the source, so the capture now takes the
    step and the picture keeps its size."""
    panel = RecordingPanel(monkeypatch, a_geometry_device())
    regpanel.rescale(dh=-8, dv=0)

    assert "IF_HB_SP2" in panel.wrote and "IF_HB_ST2" in panel.wrote
    capture = panel.final("IF_HB_ST2") - panel.final("IF_HB_SP2")
    _, full = gm.fit_to_raster(capture, 1445, gm.AXIS_H)
    after = capture * 1024 / panel.final("VDS_HSCALE")
    assert abs(after - full) < 1, "the picture must fill the raster, not inherit"


def test_at_the_ceiling_scaling_zooms_the_capture_instead(monkeypatch):
    """798 units fill the 1424 px window at HSCALE 574, so a further zoom has to
    come out of the capture or `produced` overruns the window."""
    panel = RecordingPanel(monkeypatch, a_geometry_device(hscale=574))
    regpanel.rescale(dh=-8, dv=0)

    assert panel.wrote["IF_HB_ST2"] - panel.wrote["IF_HB_SP2"] < 798
    produced = ((panel.wrote["IF_HB_ST2"] - panel.wrote["IF_HB_SP2"])
                * 1024 / panel.wrote["VDS_HSCALE"])
    assert produced <= 1424, "the zoom overran the memory window"


def test_an_aperture_cropping_the_picture_is_opened_to_fit(monkeypatch):
    """The bench fault, vertically: a 676-line aperture on a ~786-line picture.
    It letterboxed the top and bottom AND made the scale control look dead,
    because changing VSCALE only changed how much got thrown away."""
    panel = RecordingPanel(monkeypatch,
                           a_geometry_device(vscale=685, dis_v=676))
    regpanel.rescale(dh=0, dv=-40)

    produced = gm.produced_px(panel.final("IF_VB_ST") - panel.final("IF_VB_SP"),
                              panel.final("VDS_VSCALE"), axis=gm.AXIS_V)
    height = panel.final("VDS_DIS_VB_ST") - panel.final("VDS_DIS_VB_SP")
    assert height > 676, "still letterboxed"
    assert produced - gm.AXIS_V.margin - 1 <= height <= produced, (
        f"aperture {height} against a {produced:.1f} line picture")


def test_an_aperture_past_the_end_of_the_picture_is_pulled_in(monkeypatch):
    """The same fault horizontally and the other way up: a 1262 px aperture on
    a 1176 px picture showed 86 px of unwritten memory as garbage down the
    right-hand side."""
    panel = RecordingPanel(monkeypatch,
                           a_geometry_device(hscale=695, dis_h=1262))
    regpanel.rescale(dh=-8, dv=0)

    produced = gm.produced_px(panel.final("IF_HB_ST2") - panel.final("IF_HB_SP2"),
                              panel.final("VDS_HSCALE"), axis=gm.AXIS_H)
    width = panel.final("VDS_DIS_HB_ST") - panel.final("VDS_DIS_HB_SP")
    assert produced - gm.AXIS_H.margin - 1 <= width <= produced, (
        f"aperture {width} against a {produced:.1f} px picture")


def test_panning_recomputes_the_display_window_too(monkeypatch):
    """Pan and scale are the only controls, so between them they have to leave
    the display window right. Panning does not change how big the picture is,
    but it must not leave a stale window standing either."""
    panel = RecordingPanel(monkeypatch,
                           a_geometry_device(vscale=685, dis_v=676))

    regpanel.pan(0, 4)

    produced = gm.produced_px(panel.final("IF_VB_ST") - panel.final("IF_VB_SP"),
                              panel.final("VDS_VSCALE"), axis=gm.AXIS_V)
    height = panel.final("VDS_DIS_VB_ST") - panel.final("VDS_DIS_VB_SP")
    top, bottom = panel.final("VDS_DIS_VB_SP"), panel.final("VDS_DIS_VB_ST")
    assert abs((1125 - bottom) - top) <= gm.AXIS_V.margin + 1, "centred"
    assert produced - gm.AXIS_V.margin - 1 <= height <= produced


# --- the display window against where the scaler actually starts --------------


def test_the_display_window_never_starts_before_the_scaler_does(monkeypatch):
    """Tonight's bench fault, as a test.

    The panel pinned the picture's corner to a constant 129 while the source sat
    at x3.2, where the scaler does not start writing until VDS_HB_SP + 135. The
    leftmost ~41 px of the display window were therefore frame buffer nobody had
    written this frame -- frozen scratch, with every register reading exactly
    what it had been asked for.
    """
    panel = RecordingPanel(monkeypatch,
                           a_geometry_device(hscale=320, capture_h=200))

    regpanel.pan(4, 0)

    # The scale the PANEL solved, not the one the device came up with. Nothing
    # is inherited -- resolve_output refits from the capture -- so an expectation
    # built on the device's stale 320 is measuring a magnification the panel
    # never used. Its sibling below has always read it this way.
    # The scale the PANEL solved, not the one the device came up with. Nothing
    # is inherited -- resolve_output refits from the capture -- so an expectation
    # built on the device's stale 320 is measuring a magnification the panel
    # never used. Its sibling below has always read it this way.
    scale = panel.final("VDS_HSCALE")
    write_start = (panel.final("VDS_HB_SP")
                   + gm.AXIS_H.origin_offset(1024 / scale))

    # WHOLE PIXELS. VDS_HB_SP is rounded and the write start is fractional, so
    # the two can disagree by up to half a pixel in either direction -- 518
    # against 518.2 at scale 500. Sub-pixel cannot expose scratch and the fault
    # this guards against is 41 px, so comparing the pixel each lands in keeps
    # every bit of the guard that does work.
    assert panel.final("VDS_DIS_HB_SP") >= math.floor(write_start)


def test_the_display_window_never_ends_after_the_scaler_stops(monkeypatch):
    """The same fault at the other edge, which is where it was first seen: a band
    of unwritten memory down the right of the screen."""
    panel = RecordingPanel(monkeypatch,
                           a_geometry_device(hscale=320, capture_h=200))

    regpanel.pan(4, 0)

    scale = panel.final("VDS_HSCALE")
    write_start = (panel.final("VDS_HB_SP")
                   + gm.AXIS_H.origin_offset(1024 / scale))
    produced = gm.produced_px(panel.final("IF_HB_ST2") - panel.final("IF_HB_SP2"),
                              scale, axis=gm.AXIS_H)
    assert panel.final("VDS_DIS_HB_ST") <= write_start + produced


def test_the_picture_stays_centred_when_the_scale_changes(monkeypatch):
    """Scaling grows the picture about its middle rather than its left edge, so
    the user sees it expand toward both edges and can find whichever one their
    own panel cuts off. That is the point of centring: the scaler cannot know
    where this TV stops showing, so it gets out of the way and lets pan and scale
    find it."""
    for hscale in (900, 660):
        panel = RecordingPanel(monkeypatch, a_geometry_device(hscale=hscale))
        regpanel.pan(4, 0)

        left = panel.final("VDS_DIS_HB_SP")
        right = panel.final("VDS_DIS_HB_ST")
        assert abs((1445 - right) - left) <= gm.AXIS_H.margin + 1, (
            f"HSCALE {hscale}: {left} left, {1445 - right} right")


def test_any_pad_press_puts_the_picture_back_to_full_size(monkeypatch):
    """The starting state is CALCULATED, never inherited from the registers.

    A pan does it too, not just a zoom. Whatever the registers happen to hold
    -- a picture left small by an experiment, a scale someone typed in -- the
    next press computes the state rather than inheriting it.
    """
    panel = RecordingPanel(monkeypatch,
                           a_geometry_device(hscale=1023, vscale=1023))

    regpanel.pan(4, 0)

    capture = panel.final("IF_HB_ST2") - panel.final("IF_HB_SP2")
    scale, full = gm.fit_to_raster(capture, 1445, gm.AXIS_H)
    assert panel.final("VDS_HSCALE") == scale, "the scale must be recomputed"
    width = panel.final("VDS_DIS_HB_ST") - panel.final("VDS_DIS_HB_SP")
    assert full - gm.AXIS_H.margin - 1 <= width <= full
