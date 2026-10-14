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

    // The board has two: the overlay on the television and the panel on the
    // unit. One page, drawn by each the way its device wants it.
    static const uint8_t Devices = 2;

    // AN OPEN MENU STRANDS THE UNIT, so it closes itself. The panel's only
    // input is a rotary encoder that reaches the icon tree alone, and this menu
    // being open is what gates that tree off -- so while it is up the panel
    // shows a page answering nothing but the remote, whose television may be
    // dark. Matched to what the legacy screens already wait.
    // ../../../../docs/osd-menu.md
    static const uint32_t IdleCloseMs = 16000;

    Menu(const MenuItem *root, uint8_t count, const MenuRenderer &renderer,
         const MenuContext &context);

    void alsoDrawOn(const MenuRenderer &renderer);

    bool isOpen() const;

    // Whether the four arrows are the picture's rather than the cursor's, which
    // Ok on a pad item hands over and Menu or Ok takes back.
    bool isAdjusting() const;

    void open();
    void close();

    // What to ask the sketch for, which asked() nothing for a navigation key.
    // Nothing is drawn: the overlay is on the ESP's I2C bus and a press arrives
    // from a network callback, so the drawing is left to whoever owns the bus.
    MenuCommand press(Key key);

    bool needsRedraw() const;

    // The clock, from the caller that owns the tick. Holds no time of its own
    // and press() takes none: a press says only that one happened, and this is
    // what dates it.
    void poll(uint32_t nowMs);

    // From loop(), which is the one place the bus is reached. Closing draws an
    // empty page rather than nothing: a device that keeps what it was given
    // would otherwise hold the last menu over the picture for ever.
    //
    // A VALUE ROW IS LIVE STATE and not every value moves on a press: the
    // engine acts a pass or two after the one that asked it to, so this also
    // draws again where a value on the page has moved since it was drawn.
    void drawIfNeeded();

    const MenuCursor &cursor() const;

    // What a redraw would draw: where the cursor is, and whether a pad has the
    // arrows.
    MenuPage page() const;

private:
    MenuCommand adjust(Key key);

    // The visible rows' values, summed so a move is noticed without keeping
    // them. Compared per pass, which costs the value functions and no bus.
    uint16_t valueSum() const;

    const MenuItem *root_;
    uint8_t count_;
    const MenuRenderer *devices_[Devices];
    uint8_t deviceCount_;
    const MenuContext &context_;
    MenuCursor cursor_;
    bool open_;
    bool adjusting_;
    bool redraw_;
    bool pressed_;
    uint32_t idleSinceMs_;
    uint16_t valuesDrawn_;
};

}  // namespace Osd

#endif  // OSD_MENU_H_
