#ifndef TV5725_VIDEO_PROCESSOR_H
#define TV5725_VIDEO_PROCESSOR_H

namespace Tv5725 {

// The video display scaler's fixed configuration: filter taps, chroma tag
// slopes, peaking and coring coefficients, the scan-velocity modulator, and the
// test bypasses that are all off.
//
// All twelve preset tables agree on every field here and none of it moves with
// the mode, while RD-5725-1.1 documents what the fields do without offering a
// right value for a board -- so this is a hundred constants carried for
// continuity. The geometry, which is what does move, is Geometry's.
//
// Two absences that look like omissions:
//
//   - VDS_EXT_HB_* and VDS_EXT_VB_* do nothing on this board. They program the
//     HBOUT/VBOUT blanking for external use and PAD_BLK_OUT_ENZ is 1, so those
//     pins are off; VDS_SYNC_IN_SEL is 0 too, so there is no internal consumer
//     either. Measured stale against the live windows with a perfect picture.
//
//   - VDS_TAP6_BYPS, VDS_D_RAM_BYPS, VDS_PK_Y_H_BYPS and VDS_UV_STEP_BYPS are
//     the user's, written from uopt-> in doPostPresetLoadSteps(). Writing them
//     here would reset those preferences on every mode change.
class VideoProcessor {
public:
    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_PROCESSOR_H
