#ifndef OSD_MENU_PAD_H_
#define OSD_MENU_PAD_H_

// The four directions of a pad: what each arrow asks for while the pad holds
// them. One object an item points at rather than four commands inside every
// item, which would spend the RAM on every item that is not a pad.
// docs/osd-menu.md

#include "MenuCommand.h"

namespace Osd {

class MenuPad {
public:
    constexpr MenuPad(MenuCommand up, MenuCommand down, MenuCommand left,
                      MenuCommand right)
        : up_(up), down_(down), left_(left), right_(right)
    {
    }

    const MenuCommand &up() const;
    const MenuCommand &down() const;
    const MenuCommand &left() const;
    const MenuCommand &right() const;

private:
    MenuCommand up_;
    MenuCommand down_;
    MenuCommand left_;
    MenuCommand right_;
};

}  // namespace Osd

#endif  // OSD_MENU_PAD_H_
