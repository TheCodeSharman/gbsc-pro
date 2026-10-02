#ifndef OSD_PANEL_H_
#define OSD_PANEL_H_

// The menu on the 128x64 panel, which shows one item where the television shows
// three rows: the level, the selected row's label and what it is set to.
// docs/osd-menu.md

#include <stdint.h>

#include "MenuRenderer.h"

namespace Osd {

class MenuPage;

class Panel {
public:
    // One line of text at a y offset, and the pair bracketing a frame the part
    // buffers. The SSD1306 on the board, or a recorder in a host test.
    typedef void (*WriteLine)(uint8_t y, const char *text);
    typedef void (*Frame)();

    // Where the three lines sit. A row with no value takes AloneRow instead of
    // LabelRow, so a submenu reads as one line under the level rather than as a
    // label with nothing beneath it.
    static const uint8_t LevelRow = 0;
    static const uint8_t LabelRow = 22;
    static const uint8_t AloneRow = 28;
    static const uint8_t ValueRow = 44;

    // What the level with nothing above it is called, a page naming the item it
    // was descended from and the root having none.
    static const char *const RootLevel;

    // What an unavailable row reads as here, the panel having no colour to grey
    // it with.
    static const char *const UnavailableValue;

    static void writeThrough(Frame clear, WriteLine line, Frame flush);

    static const MenuRenderer &renderer();

private:
    static void begin();
    static void row(const MenuPage &page, uint8_t index, const char *value);
    static void end(const MenuPage &page);

    static Frame clear_;
    static WriteLine line_;
    static Frame flush_;
};

}  // namespace Osd

#endif  // OSD_PANEL_H_
