#include "Nudge.h"

namespace Tv5725 {

const char *Nudge::name(Control control)
{
    switch (control) {
    case HorizontalPan:
        return "hpan";
    case VerticalPan:
        return "vpan";
    case HorizontalZoom:
        return "hzoom";
    case VerticalZoom:
        return "vzoom";
    }
    return "";
}

}  // namespace Tv5725
