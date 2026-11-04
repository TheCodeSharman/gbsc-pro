#ifndef OLED_MENU_NAV_H_
#define OLED_MENU_NAV_H_

// The three gestures the encoder has. Scrolling up past the first item selects
// the status bar, and ENTER there leaves the level, so these reach every page
// and there is no fourth gesture.
enum class OLEDMenuNav
{
    IDLE = 0,
    UP,
    DOWN,
    ENTER,
};

// The gesture `key` names, false where it names none. Shared with the HTTP
// route that drives this tree, so a name the encoder has no gesture for is
// refused rather than read as IDLE -- which would answer 200 to a press that
// is never made.
bool oledNavFor(const char *key, OLEDMenuNav *into);

#endif
