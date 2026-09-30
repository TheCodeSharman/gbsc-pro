#ifndef OSD_MENU_RENDERER_H_
#define OSD_MENU_RENDERER_H_

// How a page reaches a device. The television overlay and the panel draw the
// same page differently, so the drawing is three calls a device supplies rather
// than a branch per menu state. docs/osd-menu.md

#include <stdint.h>

#include "MenuPage.h"

namespace Osd {

class MenuRenderer {
public:
    // A redraw is bracketed because the panel buffers a frame and flushes it;
    // the overlay writes characters as they arrive and ends with nothing to do.
    constexpr MenuRenderer(void (*begin)(),
                           void (*row)(uint8_t index, const char *label,
                                       const char *value, bool selected),
                           void (*end)())
        : begin_(begin), row_(row), end_(end) {}

    void draw(const MenuPage &page) const;

private:
    void (*begin_)();
    void (*row_)(uint8_t, const char *, const char *, bool);
    void (*end_)();
};

}  // namespace Osd

#endif  // OSD_MENU_RENDERER_H_
