#ifndef OSD_MENU_H_
#define OSD_MENU_H_

// The menu as the remote sees it: a key in, a redraw and at most one command
// letter out. Nothing here acts on the letter -- one action per option, reached
// from the remote, the web and the serial console alike. docs/osd-menu.md

#include <stdint.h>

#include "MenuCursor.h"
#include "MenuItem.h"
#include "MenuRenderer.h"

namespace Osd {

class Menu {
public:
    // The five keys the menu answers, from OSD_TV/remote.h. Volume is the
    // overlay's own and never reaches here.
    enum Key { KeyUp, KeyDown, KeyOk, KeyMenu, KeyExit };

    Menu(const MenuItem *root, uint8_t count, const MenuRenderer &renderer);

    bool isOpen() const;

    void open();
    void close();

    // The letter to queue as a user command, or 0.
    char press(Key key);

    const MenuCursor &cursor() const;

private:
    void draw() const;

    const MenuItem *root_;
    uint8_t count_;
    const MenuRenderer &renderer_;
    MenuCursor cursor_;
    bool open_;
};

}  // namespace Osd

#endif  // OSD_MENU_H_
