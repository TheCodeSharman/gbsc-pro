#include "Menu.h"

namespace Osd {

Menu::Menu(const MenuItem *root, uint8_t count, const MenuRenderer &renderer,
           const MenuContext &context)
    : root_(root), count_(count), renderer_(renderer), context_(context),
      cursor_(root, count), open_(false), redraw_(false)
{
}

bool Menu::isOpen() const { return open_; }

void Menu::open()
{
    cursor_ = MenuCursor(root_, count_);
    open_ = true;
    redraw_ = true;
}

void Menu::close()
{
    open_ = false;
    redraw_ = true;
}

bool Menu::needsRedraw() const { return redraw_; }

void Menu::drawIfNeeded()
{
    if (!needsRedraw())
        return;
    redraw_ = false;
    renderer_.draw(open_ ? cursor_.page() : MenuPage(), context_);
}

const MenuCursor &Menu::cursor() const { return cursor_; }

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
        redraw_ = true;
        return cursor_.current().previousCommand();
    case KeyRight:
        redraw_ = true;
        return cursor_.current().nextCommand();
    case KeyOk:
        if (cursor_.descend())
            break;
        redraw_ = true;
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

    redraw_ = true;
    return MenuCommand();
}

}  // namespace Osd
