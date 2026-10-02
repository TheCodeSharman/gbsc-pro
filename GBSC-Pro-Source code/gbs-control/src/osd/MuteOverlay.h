#ifndef OSD_MUTEOVERLAY_H_
#define OSD_MUTEOVERLAY_H_

namespace Osd {

// Whether the line input is muted, on the television's top row while the remote
// is toggling it. One row, composed and sent whole. docs/osd-menu.md
class MuteOverlay {
public:
    static void draw(bool muted);
};

}  // namespace Osd

#endif  // OSD_MUTEOVERLAY_H_
