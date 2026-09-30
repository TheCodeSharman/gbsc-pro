#ifndef OSD_MENU_ITEM_H_
#define OSD_MENU_ITEM_H_

// One node of the menu the remote drives: what it is called, what it does, and
// where it leads. A menu is a static array of these and a submenu is a pointer
// to another, so navigation is traversal rather than a target written out by
// hand at every branch. docs/osd-menu.md

#include <stddef.h>
#include <stdint.h>

namespace Osd {

class MenuContext;

class MenuItem {
public:
    // Submenu leads somewhere, Action does one thing, Choice does one thing and
    // shows what it is currently set to.
    enum Kind { Submenu, Action, Choice };

    // constexpr so a described tree is constant-initialised into flash. A
    // running constructor would put every item in RAM, and this unit has about
    // eighteen kilobytes of heap once WiFi and the servers have taken theirs.
    constexpr MenuItem(const char *label, Kind kind, char command,
                       const MenuItem *children, uint8_t childCount,
                       const char *(*valueText)(const MenuContext &))
        : label_(label), kind_(kind), command_(command), children_(children),
          childCount_(childCount), valueText_(valueText) {}

    const char *label() const;
    Kind kind() const;

    // The letter an Ok queues as a user command, or 0. The menu never mutates
    // an option itself: one action per option, reached from the remote, the web
    // and the serial console alike.
    char command() const;

    const MenuItem *children() const;
    uint8_t childCount() const;

    bool hasValue() const;
    const char *valueText(const MenuContext &context) const;

private:
    const char *label_;
    Kind kind_;
    char command_;
    const MenuItem *children_;
    uint8_t childCount_;
    const char *(*valueText_)(const MenuContext &);
};

}  // namespace Osd

#endif  // OSD_MENU_ITEM_H_
