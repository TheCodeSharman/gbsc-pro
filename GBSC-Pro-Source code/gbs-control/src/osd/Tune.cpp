#include "Tune.h"

namespace Osd {

const char *Tune::name(Control control)
{
    switch (control) {
    case Red:
        return "red";
    case Green:
        return "green";
    case Blue:
        return "blue";
    case LumaGain:
        return "luma";
    case Brightness:
        return "bright";
    case Contrast:
        return "contrast";
    case Saturation:
        return "saturation";
    case Format:
        return "format";
    }
    return "";
}

}  // namespace Osd
