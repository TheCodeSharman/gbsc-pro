#ifndef TV5725_NUDGE_H_
#define TV5725_NUDGE_H_

// Which control a remote's pad is asking for. The number of granules is the
// caller's, a hold ramp being the remote's own, and the sign is the direction.

namespace Tv5725 {

class Nudge {
public:
    enum Control { HorizontalPan, VerticalPan, HorizontalZoom, VerticalZoom };

    // What /menu reports a pad press as.
    static const char *name(Control control);
};

}  // namespace Tv5725

#endif  // TV5725_NUDGE_H_
