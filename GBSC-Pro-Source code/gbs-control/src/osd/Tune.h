#ifndef OSD_TUNE_H_
#define OSD_TUNE_H_

// Which held value a row's Left and Right step, and by how many counts. The
// companion of Tv5725::Nudge, and separate from it because the units and the
// owner differ: a pad takes the arrows and asks for capture granules, where a
// tune row keeps the cursor and asks for counts of a value somebody holds.
// docs/osd-menu.md

namespace Osd {

class Tune {
public:
    enum Control {
        Red,
        Green,
        Blue,
        LumaGain,
        Brightness,
        Contrast,
        Saturation,
        Format,
    };

    // What /menu reports a tune press as.
    static const char *name(Control control);
};

}  // namespace Osd

#endif  // OSD_TUNE_H_
