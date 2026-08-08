#ifndef TV5725_GEOMETRY_H_
#define TV5725_GEOMETRY_H_

// Where the geometry meets the TV5725's registers, and the only place they meet.
// See docs/firmware-geometry-engine.md.
//
// A header rather than a block in the .ino: arduino-cli inserts generated
// forward prototypes above any struct declared further down, so a function
// taking a struct parameter fails to compile at a line that looks fine.

#include <Arduino.h>   // `boolean`
#include <stdint.h>

#include "../../gbs_types.h"
#include "driver.h"

// Global scope, NOT namespace Tv5725, or the call in sourceFieldRateOr50Hz()
// resolves to a function that does not exist.
float getSourceFieldRate(boolean useSPBus);

namespace Tv5725 {

// The capture window and the two rasters it has to fit, read from the chip in
// one place so nothing downstream reads a register to decide what to write.
class Capture {
public:
    Capture();

    uint16_t horizontalStop() const;
    uint16_t horizontalStart() const;
    uint16_t verticalStop() const;
    uint16_t verticalStart() const;
    uint16_t linePx() const;
    uint16_t frameLines() const;
    uint16_t wrapH() const;
    uint16_t wrapV() const;

    uint16_t captureH() const;
    uint16_t captureV() const;

    // In RGBHV bypass the VDS is out of the video path and there is nothing to
    // solve; both rasters read back as nearly zero.
    bool scaling() const;

    bool usable() const;

    // A 97/98 reading mid-preset-change is a measurement in progress, not a
    // mode: the smallest real ones the VDS scales are 262 and 312.
    static const uint16_t SourceVerticalTotalMin = 200;
    static const uint16_t SourceVerticalTotalMax = 1300;

    // Read the rasters and where each capture window rolls over. False when the
    // source has not settled far enough to derive a window from.
    bool readRasters();

    void setWindows(const CaptureWindow &h, const CaptureWindow &v);

private:
    uint16_t horizontalStop_, horizontalStart_;      // IF_HB_SP2 / ST2, IF units
    uint16_t verticalStop_, verticalStart_;      // IF_VB_SP / ST, HALF-LINES
    uint16_t linePx_;         // output raster total, horizontal
    uint16_t frameLines_;     // output raster total, vertical
    uint16_t wrapH_, wrapV_;  // where each capture window rolls over
};

// The geometry engine: the user's framing, and every TV5725 register that is an
// output of it.
//
// A class rather than statics in this header, which gave every translation unit
// a silent copy and left "the framing is the truth, the registers follow"
// enforced by convention alone.
class Geometry {
public:
    Geometry();

    const PanAndZoom &framing() const;

    // Set outright, without solving: the web handler runs in a network-stack
    // callback rather than loop(), and solving measures the source field rate,
    // which spins.
    void requestFraming(const PanAndZoom &wanted);

    // Every register from the framing and the rasters. The capture window is
    // DERIVED; nothing is read back to decide it.
    bool apply();

    // A mode change has no framing worth keeping, only the previous mode's.
    // test_geometry_pads.py::test_a_preset_load_recomputes_every_register_from_scratch
    bool solveFromScratch();

    // Finish a solve the source was too unsettled to allow. Cheap when there is
    // nothing to do, so the sync watcher can call it every pass. Freeze stops it.
    bool solveIfPending();

    // Apply a framing the USER asked for. Drained from loop() whether or not
    // automation is frozen, like /setreg, /uc and /sc. Cleared before the solve,
    // so a request that cannot be met is refused rather than retried unseen.
    // test_firmware.py::test_an_explicit_framing_request_applies_while_frozen
    bool applyRequested();

    // Re-solve against the raster as it stands, for callers that have just
    // changed the output timing: `produced` is capture x 1024 / scale and has no
    // horizontalTotal term, so a window shifted by diffHTotal/2 lands where the
    // picture is not.
    bool recompute();

    // Which part of the source is grabbed, in INPUT UNITS.
    bool pan(int16_t dx, int16_t dy);

    // Zoom, in INPUT UNITS cropped off the default width. Positive crops in;
    // the picture stays as big as the raster allows and the scale follows.
    bool zoom(int16_t dh, int16_t dv);

private:
    // Only the 50/60 split is taken from it, so anything outside FrameSync's own
    // sanity window is treated as unmeasured rather than believed.
    static float sourceFieldRateOr50Hz();

    bool fail();

    bool readCapture(Capture &capture);

    // Ordered so the headroom never dips: the solver always takes the whole
    // memory window, so the only edge that can narrow it is VDS_?B_SP moving up.
    // docs/firmware-geometry-engine.md "Write ordering".
    static void write(const RegisterSolution &solved,
                      const Capture &capture);

    // A press that cannot move the window must not move the state either, or the
    // control goes dead for as many presses as it was pushed past its limit.
    bool step(const PanAndZoom &wanted);

    PanAndZoom framing_;
    bool solvePending_;      // a solve refused because the source was settling
    bool framingRequested_;  // the user asked; loop() drains it, freeze does not
};

}  // namespace Tv5725

#endif  // TV5725_GEOMETRY_H_
