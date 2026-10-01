#ifndef OSD_MENU_RENDERER_H_
#define OSD_MENU_RENDERER_H_

// How a page reaches a device. The television overlay and the panel draw the
// same page differently, so the drawing is three calls a device supplies rather
// than a branch per menu state. docs/osd-menu.md

#include <stdint.h>

namespace Osd {

class MenuContext;
class MenuPage;

class MenuRenderer {
public:
    // A redraw is bracketed because the panel buffers a frame and flushes it.
    // The end is given the page as well: the overlay has nothing to flush and
    // uses it to clear the rows the page did not fill, which it can only do
    // once they have been drawn.
    constexpr MenuRenderer(void (*begin)(),
                           void (*row)(const MenuPage &page, uint8_t index,
                                       const char *value),
                           void (*end)(const MenuPage &page))
        : begin_(begin), row_(row), end_(end)
    {
    }

    void draw(const MenuPage &page, const MenuContext &context) const;

private:
    void (*begin_)();
    void (*row_)(const MenuPage &, uint8_t, const char *);
    void (*end_)(const MenuPage &);
};

}  // namespace Osd

#endif  // OSD_MENU_RENDERER_H_
