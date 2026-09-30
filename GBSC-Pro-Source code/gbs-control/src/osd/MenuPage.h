#ifndef OSD_MENU_PAGE_H_
#define OSD_MENU_PAGE_H_

// One screen of the menu: the items in view and which of them the cursor is on.
// It carries the items rather than their text, because what a row is currently
// set to is only knowable from a context and nothing here has one.
// docs/osd-menu.md

#include <stddef.h>
#include <stdint.h>

namespace Osd {

class MenuItem;

class MenuPage {
public:
    // The television overlay carries three. The chain cut its pages to match by
    // hand, a page letter per three items; the window follows the cursor
    // instead.
    static const uint8_t Rows = 3;

    MenuPage();

    void add(const MenuItem &item);
    void select(uint8_t row);

    uint8_t rows() const;
    const MenuItem &itemAt(uint8_t row) const;
    const char *labelAt(uint8_t row) const;

    uint8_t selected() const;

private:
    const MenuItem *items_[Rows];
    uint8_t rows_;
    uint8_t selected_;
};

}  // namespace Osd

#endif  // OSD_MENU_PAGE_H_
