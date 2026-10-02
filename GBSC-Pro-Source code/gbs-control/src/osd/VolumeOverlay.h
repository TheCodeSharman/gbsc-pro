#ifndef OSD_VOLUMEOVERLAY_H_
#define OSD_VOLUMEOVERLAY_H_

#include <stdint.h>

namespace Osd {

// The line-input volume, on the television's top row while the remote is
// changing it. One row, composed and sent whole. docs/osd-menu.md
class VolumeOverlay {
public:
    // A filled block in the bar's colour, which is what the row's background
    // is: the same value means the colour at the even address and the glyph at
    // the odd one.
    static const char Bar = 0x11;
    static const char Title = 0x16;
    static const char Body = 0x17;

    static void draw(uint8_t level);
};

}  // namespace Osd

#endif  // OSD_VOLUMEOVERLAY_H_
