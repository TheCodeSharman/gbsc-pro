#ifndef OSD_MENU_ITEM_H_
#define OSD_MENU_ITEM_H_

// One node of the menu the remote drives: what it is called, what it does, and
// where it leads. A menu is a static array of these and a submenu is a pointer
// to another, so navigation is traversal rather than a target written out by
// hand at every branch. docs/osd-menu.md

#include <stddef.h>
#include <stdint.h>

#include "MenuCommand.h"

namespace Osd {

class MenuContext;

class MenuItem {
public:
    typedef const char *(*ValueText)(const MenuContext &);

    // Built through these rather than through a constructor, so a described tree
    // reads as a table of intent and each row says which of the four shapes it
    // is. constexpr throughout: a running constructor would put every item in
    // RAM, and this unit has about eighteen kilobytes of heap once WiFi and the
    // servers have taken theirs.
    static constexpr MenuItem submenu(const char *label,
                                      const MenuItem *children, uint8_t count)
    {
        return MenuItem(label, MenuCommand(), MenuCommand(), MenuCommand(),
                        children, count, NULL);
    }

    static constexpr MenuItem action(const char *label, char letter)
    {
        return MenuItem(label, MenuCommand::user(letter), MenuCommand(),
                        MenuCommand(), NULL, 0, NULL);
    }

    static constexpr MenuItem serialAction(const char *label, char letter)
    {
        return MenuItem(label, MenuCommand::serial(letter), MenuCommand(),
                        MenuCommand(), NULL, 0, NULL);
    }

    static constexpr MenuItem choice(const char *label, char letter,
                                     ValueText value)
    {
        return MenuItem(label, MenuCommand::user(letter), MenuCommand(),
                        MenuCommand(), NULL, 0, value);
    }

    static constexpr MenuItem serialChoice(const char *label, char letter,
                                           ValueText value)
    {
        return MenuItem(label, MenuCommand::serial(letter), MenuCommand(),
                        MenuCommand(), NULL, 0, value);
    }

    // Left and Right step the value. `ok` may be absent, and the three letters
    // need not share a surface -- ADC gain steps through /uc? and toggles
    // automatic gain through /sc?.
    static constexpr MenuItem adjust(const char *label, MenuCommand ok,
                                     MenuCommand next, MenuCommand previous,
                                     ValueText value)
    {
        return MenuItem(label, ok, next, previous, NULL, 0, value);
    }

    const char *label() const;

    // What Ok, Right and Left ask the sketch to do. The menu never acts itself:
    // one action per option, reached from the remote, the web and the serial
    // console alike.
    const MenuCommand &okCommand() const;
    const MenuCommand &nextCommand() const;
    const MenuCommand &previousCommand() const;

    const MenuItem *children() const;
    uint8_t childCount() const;

    bool hasValue() const;
    const char *valueText(const MenuContext &context) const;

private:
    constexpr MenuItem(const char *label, MenuCommand ok, MenuCommand next,
                       MenuCommand previous, const MenuItem *children,
                       uint8_t childCount, ValueText valueText)
        : label_(label), ok_(ok), next_(next), previous_(previous),
          children_(children), childCount_(childCount), valueText_(valueText)
    {
    }

    const char *label_;
    MenuCommand ok_;
    MenuCommand next_;
    MenuCommand previous_;
    const MenuItem *children_;
    uint8_t childCount_;
    ValueText valueText_;
};

}  // namespace Osd

#endif  // OSD_MENU_ITEM_H_
