#include "Menu.h"

namespace Osd {

Menu::Menu(const MenuItem *root, uint8_t count, const MenuRenderer &renderer,
           const MenuContext &context)
    : root_(root), count_(count), deviceCount_(1), context_(context),
      cursor_(root, count), open_(false), adjusting_(false),
      redraw_(false), valuesDrawn_(0)
{
    devices_[0] = &renderer;
    for (uint8_t i = 1; i < Devices; ++i)
        devices_[i] = NULL;
}

void Menu::alsoDrawOn(const MenuRenderer &renderer)
{
    if (deviceCount_ < Devices)
        devices_[deviceCount_++] = &renderer;
}

bool Menu::isOpen() const { return open_; }

bool Menu::isAdjusting() const { return adjusting_; }

void Menu::open()
{
    cursor_ = MenuCursor(root_, count_);
    open_ = true;
    adjusting_ = false;
    redraw_ = true;
}

void Menu::close()
{
    open_ = false;
    adjusting_ = false;
    redraw_ = true;
}

bool Menu::needsRedraw() const { return redraw_; }

uint16_t Menu::valueSum() const
{
    if (!open_)
        return 0;

    const MenuPage shown = cursor_.page();
    uint16_t sum = 0;
    for (uint8_t row = 0; row < shown.rows(); ++row) {
        const char *text = shown.itemAt(row).valueText(context_);
        if (text == NULL)
            continue;
        sum = (uint16_t)(sum + row + 1);
        for (const char *c = text; *c != '\0'; ++c)
            sum = (uint16_t)(sum * 31 + (uint8_t)*c);
    }
    return sum;
}

void Menu::drawIfNeeded()
{
    if (!redraw_ && valueSum() != valuesDrawn_)
        redraw_ = true;
    if (!needsRedraw())
        return;
    redraw_ = false;
    const MenuPage drawn = open_ ? page() : MenuPage();
    for (uint8_t i = 0; i < deviceCount_; ++i)
        devices_[i]->draw(drawn, context_);
    valuesDrawn_ = valueSum();
}

const MenuCursor &Menu::cursor() const { return cursor_; }

MenuPage Menu::page() const
{
    MenuPage drawn = cursor_.page();
    if (adjusting_)
        drawn.markAdjusting();
    return drawn;
}

// The arrows are the picture's while a pad holds them, and Ok gives them back.
MenuCommand Menu::adjust(Key key)
{
    redraw_ = true;
    const MenuPad &pad = cursor_.current().pad();
    switch (key) {
    case KeyUp:
        return pad.up();
    case KeyDown:
        return pad.down();
    case KeyLeft:
        return pad.left();
    case KeyRight:
        return pad.right();
    case KeyExit:
        close();
        break;
    case KeyOk:
    case KeyMenu:
        adjusting_ = false;
        break;
    }
    return MenuCommand();
}

MenuCommand Menu::press(Key key)
{
    if (!open_) {
        if (key == KeyMenu)
            open();
        return MenuCommand();
    }

    if (adjusting_)
        return adjust(key);

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
        if (cursor_.current().isPad()) {
            adjusting_ = true;
            break;
        }
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
