#include "MenuCursor.h"

#include <stddef.h>

namespace Osd {

const uint8_t MenuCursor::MaxDepth;

MenuCursor::MenuCursor(const MenuItem *root, uint8_t count) : depth_(1)
{
    levels_[0].items = root;
    levels_[0].count = count;
    levels_[0].index = 0;
    levels_[0].first = 0;
}

const MenuItem &MenuCursor::current() const
{
    const Level &level = levels_[depth_ - 1];
    return level.items[level.index];
}

uint8_t MenuCursor::index() const { return levels_[depth_ - 1].index; }

uint8_t MenuCursor::depth() const { return depth_; }

void MenuCursor::up()
{
    Level &level = levels_[depth_ - 1];
    if (level.count == 0)
        return;
    level.index = level.index == 0 ? (uint8_t)(level.count - 1)
                                   : (uint8_t)(level.index - 1);
    scrollIntoView();
}

void MenuCursor::down()
{
    Level &level = levels_[depth_ - 1];
    if (level.count == 0)
        return;
    level.index = (uint8_t)((level.index + 1) % level.count);
    scrollIntoView();
}

bool MenuCursor::descend()
{
    const MenuItem &item = current();
    if (item.children() == NULL || item.childCount() == 0 || depth_ >= MaxDepth)
        return false;

    levels_[depth_].items = item.children();
    levels_[depth_].count = item.childCount();
    levels_[depth_].index = 0;
    levels_[depth_].first = 0;
    ++depth_;
    return true;
}

bool MenuCursor::ascend()
{
    if (depth_ <= 1)
        return false;
    --depth_;
    return true;
}

void MenuCursor::scrollIntoView()
{
    Level &level = levels_[depth_ - 1];
    if (level.count <= MenuPage::Rows) {
        level.first = 0;
        return;
    }
    if (level.index < level.first)
        level.first = level.index;
    else if (level.index >= level.first + MenuPage::Rows)
        level.first = (uint8_t)(level.index - MenuPage::Rows + 1);
}

MenuPage MenuCursor::page() const
{
    const Level &level = levels_[depth_ - 1];
    MenuPage page;
    if (depth_ > 1) {
        const Level &parent = levels_[depth_ - 2];
        page.nameLevel(parent.items[parent.index].label());
    }
    for (uint8_t row = 0; row < MenuPage::Rows; ++row) {
        const uint8_t at = (uint8_t)(level.first + row);
        if (at >= level.count)
            break;
        page.add(level.items[at]);
    }
    page.select((uint8_t)(level.index - level.first));
    return page;
}

}  // namespace Osd
