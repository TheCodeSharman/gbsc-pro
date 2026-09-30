#ifndef OSD_MENU_PAGE_H_
#define OSD_MENU_PAGE_H_

// One screen of the menu as text: the rows in view and which of them the cursor
// is on. Nothing here knows how a row is drawn, so the television overlay and
// the panel are two renderers of one page and a host test reads what the menu
// SAYS without either device. docs/osd-menu.md

#include <stddef.h>
#include <stdint.h>

namespace Osd {

class MenuPage {
public:
    // The television overlay carries three. The chain cut its pages to match by
    // hand, a page letter per three items; the window follows the cursor
    // instead.
    static const uint8_t Rows = 3;

    MenuPage();

    void add(const char *label, const char *value);
    void select(uint8_t row);

    uint8_t rows() const;
    const char *labelAt(uint8_t row) const;

    // NULL where the row shows no value.
    const char *valueAt(uint8_t row) const;

    uint8_t selected() const;

private:
    const char *labels_[Rows];
    const char *values_[Rows];
    uint8_t rows_;
    uint8_t selected_;
};

}  // namespace Osd

#endif  // OSD_MENU_PAGE_H_
