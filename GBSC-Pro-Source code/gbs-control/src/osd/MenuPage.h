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
    // The television overlay carries three, and a level is cut into pages of
    // that many rather than scrolled a row at a time, so a page is a fixed
    // group of three and the overlay's page character counts them.
    static const uint8_t Rows = 3;

    MenuPage();

    void nameLevel(const char *title);
    void add(const MenuItem &item);
    void select(uint8_t row);
    void numberPage(uint8_t number, bool previous, bool next);
    void markAdjusting();

    // The item this level was descended from, or NULL at the root. A renderer
    // composes its own breadcrumb from it.
    const char *title() const;

    uint8_t rows() const;

    // Where a row sits in its level, counting from one: what the root ring
    // carries beside each label.
    uint8_t positionAt(uint8_t row) const;
    const MenuItem &itemAt(uint8_t row) const;
    const char *labelAt(uint8_t row) const;

    // Whether Ok on the row descends a level. A pad leads somewhere too and
    // is not marked: Ok hands it the arrows rather than changing level.
    bool descendsAt(uint8_t row) const;

    uint8_t selected() const;

    // Whether the selected row's pad has the arrows, which a device draws as
    // well as the row -- the overlay as four arrows where a value would be.
    bool adjusting() const;

    // 1 for the first page of the level, and 0 where there is no level at all,
    // which is the page a closed menu draws.
    uint8_t number() const;
    bool hasPreviousPage() const;
    bool hasNextPage() const;

private:
    const char *title_;
    const MenuItem *items_[Rows];
    uint8_t rows_;
    uint8_t selected_;
    uint8_t number_;
    bool previous_;
    bool next_;
    bool adjusting_;
};

}  // namespace Osd

#endif  // OSD_MENU_PAGE_H_
