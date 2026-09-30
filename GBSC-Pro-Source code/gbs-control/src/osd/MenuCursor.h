#ifndef OSD_MENU_CURSOR_H_
#define OSD_MENU_CURSOR_H_

// Where the remote is in the menu. Up and Down move within a level and the ends
// join; Ok descends a submenu; Menu returns to the item that was descended
// from. Nothing here knows what any item does. docs/osd-menu.md

#include <stdint.h>

#include "MenuItem.h"
#include "MenuPage.h"

namespace Osd {

class MenuCursor {
public:
    // How deep the tree may go. The described tree is three levels; the fourth
    // is the refusal, because a cursor that ran off its stack would corrupt it.
    static const uint8_t MaxDepth = 4;

    MenuCursor(const MenuItem *root, uint8_t count);

    const MenuItem &current() const;
    uint8_t index() const;

    // 1 at the root level, one more per level descended.
    uint8_t depth() const;

    void up();
    void down();

    // False where the item leads nowhere, which leaves the cursor where it was.
    bool descend();
    bool ascend();

    // The rows in view and which is highlighted, the window following the
    // cursor.
    MenuPage page() const;

private:
    struct Level {
        const MenuItem *items;
        uint8_t count;
        uint8_t index;
        uint8_t first;
    };

    void scrollIntoView();

    Level levels_[MaxDepth];
    uint8_t depth_;
};

}  // namespace Osd

#endif  // OSD_MENU_CURSOR_H_
