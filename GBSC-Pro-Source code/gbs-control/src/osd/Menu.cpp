#include "Menu.h"

namespace Osd {

Menu::Menu(const MenuItem *root, uint8_t count, const MenuRenderer &renderer)
    : root_(root), count_(count), renderer_(renderer), cursor_(root, count),
      open_(false)
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

void Menu::draw() const { renderer_.draw(cursor_.page()); }

char Menu::press(Key key)
{
    if (!open_) {
        if (key == KeyMenu)
            open();
        return 0;
    }

    switch (key) {
    case KeyUp:
        cursor_.up();
        break;
    case KeyDown:
        cursor_.down();
        break;
    case KeyOk:
        if (cursor_.current().kind() == MenuItem::Submenu) {
            cursor_.descend();
            break;
        }
        draw();
        return cursor_.current().command();
    case KeyMenu:
        if (!cursor_.ascend()) {
            close();
            return 0;
        }
        break;
    case KeyExit:
        close();
        return 0;
    }

    draw();
    return 0;
}

}  // namespace Osd
