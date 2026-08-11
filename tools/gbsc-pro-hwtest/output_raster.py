"""The output raster model in Python, worked out here before being ported to
firmware -- the same arrangement geometry_math.py has with the geometry engine.

The registers it computes, none of which a preset table should own:

    VDS_HSYNC_RST   htotal - 1        VDS_HS_ST / VDS_HS_SP   the hsync pulse
    VDS_VSYNC_RST   vtotal - 1        VDS_VS_ST / VDS_VS_SP   the vsync pulse
    PLL648_CONTROL_01                 the display clock seed

plus the ACTIVE WINDOW the picture is placed inside.

THE THREE FACTS IT RESTS ON, all measured rather than assumed:

1.  htotal = clock / (fieldRate x vtotal). This is the generator the whole preset
    family was authored with: all ten scaling presets land within 0.5% of their
    own divider's rated clock. See test_output_raster.py.

2.  vtotal is a property of the OUTPUT MODE alone. Each mode's PAL and NTSC tables
    ship an identical frame height, so the field rate only ever moves htotal.

3.  The divider is only a SEED. externalClockGenSyncInOutRate() steers the Si5351
    by (sourceRate / outputRate), unclamped, so the real pixel clock ends up at
    whatever the raster demands. The bench shows it: a 50 Hz lock reads htotal
    1445 -- the table's value, demanding 81.35 MHz -- not the 1438 that exactly
    81 MHz affords.

So the question is not "what clock is allowed" but "what raster will the encoder
lock to, at a clock the part tolerates". 108.46 MHz is demonstrated by two
shipped PAL presets and 162 MHz by two NTSC ones.
"""

from dataclasses import dataclass


# --- the display clock ------------------------------------------------------

# Mirrors src/tv5725/DisplayClock.h. Every value is 648 MHz over an integer,
# which is where PLL648_CONTROL_01 gets its name.
DIVIDER_HZ = {
    0x25: 40_500_000,
    0x45: 54_000_000,
    0x55: 64_800_000,
    0x65: 81_000_000,
    0x85: 108_000_000,
    0x95: 129_600_000,
    0xA5: 162_000_000,
    0x35: 81_000_000,
    0x00: 81_000_000,
}

# **MEASURED ON THE BENCH, 2026-08-11.** Swept on the RiscPC at 320x256@50
# (source line 1277 IF units, VTOTAL 308), each step recomputing the raster as a
# set and judging the picture on the TV:
#
#     81.35 MHz   1445 x 1126   shipped -- works
#    107.98 MHz   1918 x 1126   works, sharp, no tearing
#    129.55 MHz   2301 x 1126   works, sharp, no tearing
#    161.98 MHz   2877 x 1126   FLICKERS THEN GOES BLACK
#
# So the wall is between 129.6 and 162 MHz. **THE DATASHEET'S 108 IS NOT THE
# CONSTRAINT**: DS-5725-3.2 Table 15's "108 MHz at 20pF" rates the CLKOUT pin,
# which PAD_CKOUT_ENZ disables on this board because the MS9288A takes the analog
# output. It bounds a pad nothing drives.
#
# 129.6 MHz is a FLOOR on the true limit, not the limit: nothing between 129.6
# and 162 has been tried, and the mapped dividers offer nothing in between.
WORKING_CEILING_HZ = 129_600_000

# The clock is not the only limit and on this source it is not the binding one.
#
# **THE PICTURE CANNOT FILL AN ARBITRARILY WIDE RASTER.** produced = capture x
# 1024 / HSCALE, so a scale floor caps the widest picture at capture x
# (1024 / floor) and a raster wider than that just adds black. At the floor this
# model carries:
#
#     source line 1277 units, usable capture 1186 (sync pulse excluded)
#       -> widest picture 2428 px
#       -> the 162 MHz raster of 2877 is 449 px wider than can EVER be filled
#
# Visible in the sweep as % of raster filled: 88.7% at 108 MHz, 90.4% at 129.6,
# then DOWN to 84.3% at 162 -- more clock is worse before the clock itself fails.
#
# So for a 320-pixel source the two limits bracket each other around 130-160 MHz,
# and 129.6 MHz / 2301 px is the best measured state.
#
# **THE FIRMWARE'S FLOOR IS DERIVED, NOT THIS 500** -- Axis::scaleMin() is
# Scale::Unity / maxMagnification. docs/firmware-geometry-engine.md.
HSCALE_FLOOR = 500
MAX_MAGNIFICATION = 1024 / HSCALE_FLOOR

HTOTAL_MAX = 4096  # VDS_HSYNC_RST is 12 bits
VTOTAL_MAX = 2048  # VDS_VSYNC_RST is 11 bits


# --- what the output modes are ---------------------------------------------

# Frame height per output mode, taken from the shipped tables -- where the PAL and
# NTSC pair for each mode agree exactly, which is what makes this a property of
# the mode rather than a coincidence.
#
# These are not CEA-861 frame heights. 1080p is 1126 lines where CEA says 1125,
# and 720p is 751 where CEA says 750 -- each one line long. Kept as shipped for
# now because changing the frame height moves the vertical geometry, and the
# horizontal is the axis under test. One line is 0.09%.
MODE_VTOTAL = {
    "240p": 1001,
    "480p": 526,
    "576p": 626,
    "720p": 751,
    "1024p": 1067,
    "1080p": 1126,
}

MODE_ACTIVE = {
    "240p": (None, 240),
    "480p": (720, 480),
    "576p": (768, 576),
    "720p": (1280, 720),
    "1024p": (1280, 1024),
    "1080p": (1920, 1080),
}


# --- the sync pulse, from the standard rather than from the tables ----------

# **CEA-861 DEFINES SYNC AND BACK PORCH AS TIMES, NOT PIXEL COUNTS**, which is
# what makes them portable to a raster that is not the standard's. For 1080p the
# sync is 44 pixels and the back porch 148 at 148.5 MHz, and those two are
# IDENTICAL at 50 and 60 Hz -- only the front porch absorbs the rate difference
# (528 against 88). So sync + back porch is a fixed 1.293 us of every 1080p line
# regardless of rate, and that is the quantity to reproduce.
#
# Michael, 2026-08-11: "might be worthwhile making the sync pulses what is
# expected by the standard." It is also the safer bet for the one thing we cannot
# query -- EDID is unreachable, so the encoder's tolerance is unknown and
# conformant timing is the best blind guess available.
#
# (nanoseconds of sync, nanoseconds of back porch)
CEA_SYNC_NS = {
    "1080p": (44 / 148.5e6 * 1e9, 148 / 148.5e6 * 1e9),   # 296.3, 996.6
    "720p":  (40 / 74.25e6 * 1e9, 220 / 74.25e6 * 1e9),   # 538.7, 2962.9
    "576p":  (64 / 27.0e6 * 1e9,  68 / 27.0e6 * 1e9),     # 2370.4, 2518.5
    "480p":  (62 / 27.0e6 * 1e9,  60 / 27.0e6 * 1e9),     # 2296.3, 2222.2
}

# Vertical, in LINES, which need no conversion -- a line is a line at any clock.
# 1080p: sync 5, back porch 36. The shipped tables already ship a 5-line vsync
# (VDS_VS_ST/SP of 1..6), so the vertical is standard already.
CEA_VSYNC_LINES = {
    "1080p": (5, 36),
    "720p":  (5, 20),
    "576p":  (5, 39),
    "480p":  (6, 30),
}

# The only output hsync pair measured working, at 108 MHz, shipped by both 1080p
# tables. Width 52 -- 1.6x the standard's 32 at this clock. Kept as the comparison
# point for a sweep, NOT as a default: nothing models the output hsync position
# (CLAUDE.md), so this is one measurement at one clock.
MEASURED_HSYNC_108MHZ = (20, 72)


@dataclass(frozen=True)
class Raster:
    """A complete output raster: what the engine would write on a mode change."""

    mode: str
    field_rate: float
    divider: int
    htotal: int
    vtotal: int
    hsync_start: int
    hsync_stop: int
    vsync_start: int
    vsync_stop: int
    active_start: int   # first active pixel: end of sync + back porch
    active_lines_start: int

    def demanded_hz(self):
        """The pixel clock this raster asks for. The Si5351 is steered here."""
        return self.htotal * self.vtotal * self.field_rate

    def active_width(self):
        """Pixels between the back porch and the front porch.

        This is the window the geometry engine should place the picture inside.
        Today Axis::maxDisplayWindow() spans the whole raster, which is why the
        write latency eats active video instead of living in the blanking.
        """
        return self.htotal - self.active_start

    def register_writes(self):
        """(segment, register, offset, width, value), in the order to write them.

        Output timing moves as a SET or not at all -- a previous session put
        VDS_HSYNC_RST to 1919 with the sync and windows left behind and the screen
        went black. The totals go first so the sync positions are never briefly
        outside the raster.
        """
        return [
            (3, 0x01, 0, 12, self.htotal - 1),   # VDS_HSYNC_RST
            (3, 0x02, 4, 11, self.vtotal - 1),   # VDS_VSYNC_RST
            (3, 0x0A, 0, 12, self.hsync_start),  # VDS_HS_ST
            (3, 0x0B, 4, 12, self.hsync_stop),   # VDS_HS_SP
            (3, 0x0D, 0, 11, self.vsync_start),  # VDS_VS_ST
            (3, 0x0E, 4, 11, self.vsync_stop),   # VDS_VS_SP
        ]

    def describe(self):
        return (
            f"{self.mode} @ {self.field_rate:g} Hz: raster {self.htotal} x {self.vtotal}, "
            f"clock {self.demanded_hz() / 1e6:.3f} MHz (seed {self.divider:#04x}), "
            f"hsync {self.hsync_start}..{self.hsync_stop}, "
            f"vsync {self.vsync_start}..{self.vsync_stop}, "
            f"active {self.active_width()} px from {self.active_start}"
        )


def htotal_for(hz, vtotal, field_rate):
    """The widest line this clock affords. None if it is not a raster.

    FLOORED: a raster needs htotal x vtotal x fieldRate hertz, so rounding up asks
    for more clock than the target. The shipped tables all round up by 0.2-0.5%
    and this deliberately does not inherit that.
    """
    if hz <= 0 or vtotal <= 0 or field_rate <= 0:
        return None
    htotal = int(hz / (field_rate * vtotal))
    if htotal < 1 or htotal > HTOTAL_MAX:
        return None
    return htotal


def divider_for(vtotal, field_rate, ceiling_hz=WORKING_CEILING_HZ):
    """The largest seed at or under the ceiling that still yields a valid raster.

    Largest because the seed only has to put the Si5351 in range and more clock is
    more line. Skips seeds whose htotal would overflow the 12-bit register, which
    is what rules 162 MHz out for a short frame.
    """
    usable = [
        (hz, divider)
        for divider, hz in DIVIDER_HZ.items()
        if hz <= ceiling_hz and htotal_for(hz, vtotal, field_rate) is not None
    ]
    if not usable:
        return None
    return max(usable)[1]


def raster_for(mode, field_rate, ceiling_hz=WORKING_CEILING_HZ, hsync=None,
               vtotal=None):
    """A complete raster for an output mode at a measured field rate.

    `hsync` overrides the standard-derived pulse, for sweeping it -- the one
    quantity here with no model behind it.
    """
    if mode not in MODE_VTOTAL:
        raise KeyError(f"unknown output mode {mode!r}; have {sorted(MODE_VTOTAL)}")

    vtotal = MODE_VTOTAL[mode] if vtotal is None else vtotal
    divider = divider_for(vtotal, field_rate, ceiling_hz)
    if divider is None:
        return None
    htotal = htotal_for(DIVIDER_HZ[divider], vtotal, field_rate)

    # The real clock, not the seed: the Si5351 is steered to the raster, so the
    # sync pulse must be converted at the clock the line will actually run at.
    clock_hz = htotal * vtotal * field_rate

    if hsync is not None:
        hsync_start, hsync_stop = hsync
        back_porch = 0
    else:
        sync_ns, porch_ns = CEA_SYNC_NS.get(mode, CEA_SYNC_NS["1080p"])
        width = max(1, round(sync_ns * clock_hz / 1e9))
        back_porch = round(porch_ns * clock_hz / 1e9)
        # The pulse starts at 0: the front porch is whatever is left at the end of
        # the line, which is how CEA absorbs the field-rate difference.
        hsync_start, hsync_stop = 0, width

    vsync_lines, v_back_porch = CEA_VSYNC_LINES.get(mode, CEA_VSYNC_LINES["1080p"])

    return Raster(
        mode=mode,
        field_rate=field_rate,
        divider=divider,
        htotal=htotal,
        vtotal=vtotal,
        hsync_start=hsync_start,
        hsync_stop=hsync_stop,
        vsync_start=0,
        vsync_stop=vsync_lines,
        active_start=hsync_stop + back_porch,
        active_lines_start=vsync_lines + v_back_porch,
    )


def iso_frame_rasters(htotal, vtotal, tolerance=0.002, span=None, count=9):
    """Raster shapes with the same frame time, so the pixel clock never moves.

    **THE SWEEP THAT NEEDS NO FIRMWARE CHANGE, and that is why it exists.**
    Changing htotal alone changes the output frame rate, and runFrequency() can
    only correct 0.06% per pass, so a 33% move would take hundreds of iterations
    with the picture wrong throughout. Holding htotal x vtotal constant keeps the
    frame time and the clock exactly where they are, so the only thing varying is
    the SHAPE the encoder has to lock to -- reachable today with /setreg and
    /geometry alone.

    **BOUNDED TO `count` SHAPES, SPREAD ACROSS THE RANGE.** Almost every htotal
    has a vtotal pairing inside a 0.2% tolerance, so the unfiltered set is over a
    thousand entries one pixel apart -- two hours of settling time to learn
    nothing, since neighbouring shapes are indistinguishable. A handful spread
    wide is the experiment; a dense walk is not.

    Returns (htotal, vtotal) pairs ordered by htotal, always including the input.
    """
    target = htotal * vtotal
    if span is None:
        span = max(64, htotal // 3)

    low = max(1, htotal - span)
    high = min(HTOTAL_MAX, htotal + span)

    candidates = {}
    for candidate_h in range(low, high + 1):
        candidate_v = round(target / candidate_h)
        if candidate_v < 1 or candidate_v > VTOTAL_MAX:
            continue
        if abs(candidate_h * candidate_v / target - 1.0) <= tolerance:
            candidates[candidate_h] = candidate_v

    if not candidates:
        return [(htotal, vtotal)]

    # Spread: pick the candidate nearest each of `count` evenly spaced targets,
    # so the sweep covers the shape range rather than a neighbourhood.
    reachable = sorted(candidates)
    picks = {}
    for index in range(count):
        want = reachable[0] + (reachable[-1] - reachable[0]) * index / max(1, count - 1)
        nearest = min(reachable, key=lambda h: abs(h - want))
        picks[nearest] = candidates[nearest]

    picks[htotal] = vtotal  # the starting shape is always in its own sweep
    return [(h, picks[h]) for h in sorted(picks)]


# --- the shipped tables, as validation data --------------------------------


@dataclass(frozen=True)
class ShippedPreset:
    """One preset table's output raster, read out of the .h file by hand.

    Here as EVIDENCE, not as a design to reproduce. Ten independent worked
    examples of htotal = clock / (fieldRate x vtotal) is far better grounding than
    the two bench readings this model started from.
    """

    name: str
    mode: str
    field_rate: float
    divider: int
    htotal: int
    vtotal: int
    hsync_start: int
    hsync_stop: int

    def demanded_hz(self):
        return self.htotal * self.vtotal * self.field_rate


# pal_1920x1080 is listed at its ORIGINAL 1445/0x65, which is the defect this
# model exists to remove -- not at any patched value.
SHIPPED_PRESETS = (
    ShippedPreset("pal_240p",       "240p",  50.0, 0x85, 2167, 1001,  24, 152),
    ShippedPreset("pal_768x576",    "576p",  50.0, 0x65, 2600,  626, 196,  28),
    ShippedPreset("pal_1280x720",   "720p",  50.0, 0x65, 2166,  751,  24, 128),
    ShippedPreset("pal_1280x1024",  "1024p", 50.0, 0x85, 2033, 1067,   0, 132),
    ShippedPreset("pal_1920x1080",  "1080p", 50.0, 0x65, 1445, 1126,   8,  56),
    ShippedPreset("ntsc_240p",      "240p",  60.0, 0xA5, 2705, 1001,   8, 160),
    ShippedPreset("ntsc_720x480",   "480p",  60.0, 0x65, 2574,  526, 180,  12),
    ShippedPreset("ntsc_1280x720",  "720p",  60.0, 0x85, 2403,  751,  16, 144),
    ShippedPreset("ntsc_1280x1024", "1024p", 60.0, 0xA5, 2536, 1067,  16, 144),
    ShippedPreset("ntsc_1920x1080", "1080p", 60.0, 0x85, 1602, 1126,  20,  72),
)


def preset(name):
    for shipped in SHIPPED_PRESETS:
        if shipped.name == name:
            return shipped
    raise KeyError(name)
