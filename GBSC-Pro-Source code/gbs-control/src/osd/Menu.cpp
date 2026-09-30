#include "Menu.h"

namespace Osd {

Menu::Menu(const MenuItem *root, uint8_t count, const MenuRenderer &renderer,
           const MenuContext &context)
    : root_(root), count_(count), renderer_(renderer), context_(context),
      cursor_(root, count), open_(false)
{
}

bool Menu::isOpen() const { return open_; }

void Menu::open()
{
    cursor_ = MenuCursor(root_, count_);
    open_ = true;
    draw();
}

void Menu::close() { open_ = false; }

const MenuCursor &Menu::cursor() const { return cursor_; }

void Menu::draw() const { renderer_.draw(cursor_.page(), context_); }

MenuCommand Menu::press(Key key)
{
    if (!open_) {
        if (key == KeyMenu)
            open();
        return MenuCommand();
    }

    switch (key) {
    case KeyUp:
        cursor_.up();
        break;
    case KeyDown:
        cursor_.down();
        break;
    case KeyLeft:
        draw();
        return cursor_.current().previousCommand();
    case KeyRight:
        draw();
        return cursor_.current().nextCommand();
    case KeyOk:
        if (cursor_.descend())
            break;
        draw();
        return cursor_.current().okCommand();
    case KeyMenu:
        if (!cursor_.ascend()) {
            close();
            return MenuCommand();
        }
        break;
    case KeyExit:
        close();
        return MenuCommand();
    }

    draw();
    return MenuCommand();
}

}  // namespace Osd
