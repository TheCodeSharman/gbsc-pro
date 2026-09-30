#ifndef OSD_MENU_H_
#define OSD_MENU_H_

// The menu as the remote sees it: a key in, a redraw and at most one command
// letter out. Nothing here acts on the letter -- one action per option, reached
// from the remote, the web and the serial console alike. docs/osd-menu.md

#include <stdint.h>

#include "MenuCommand.h"
#include "MenuCursor.h"
#include "MenuItem.h"
#include "MenuRenderer.h"

namespace Osd {

class MenuContext;

class Menu {
public:
    // The keys the menu answers, from OSD_TV/remote.h. Volume is the overlay's
    // own and never reaches here.
    enum Key { KeyUp, KeyDown, KeyLeft, KeyRight, KeyOk, KeyMenu, KeyExit };

    Menu(const MenuItem *root, uint8_t count, const MenuRenderer &renderer,
         const MenuContext &context);

    bool isOpen() const;

    void open();
    void close();

    // What to ask the sketch for, which asked() nothing for a navigation key.
    // Nothing is drawn: the overlay is on the ESP's I2C bus and a press arrives
    // from a network callback, so the drawing is left to whoever owns the bus.
    MenuCommand press(Key key);

    bool needsRedraw() const;

    // From loop(), which is the one place the bus is reached. Closing draws an
    // empty page rather than nothing: a device that keeps what it was given
    // would otherwise hold the last menu over the picture for ever.
    void drawIfNeeded();

    const MenuCursor &cursor() const;

private:
    const MenuItem *root_;
    uint8_t count_;
    const MenuRenderer &renderer_;
    const MenuContext &context_;
    MenuCursor cursor_;
    bool open_;
    bool redraw_;
};

}  // namespace Osd

#endif  // OSD_MENU_H_
