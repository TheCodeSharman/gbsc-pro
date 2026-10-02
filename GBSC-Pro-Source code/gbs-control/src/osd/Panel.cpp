#include "Panel.h"

#include <stddef.h>

#include "MenuItem.h"
#include "MenuPage.h"

namespace Osd {

const uint8_t Panel::LevelRow;
const uint8_t Panel::LabelRow;
const uint8_t Panel::AloneRow;
const uint8_t Panel::ValueRow;

const char *const Panel::RootLevel = "Menu";
const char *const Panel::UnavailableValue = "N/A";

Panel::Frame Panel::clear_ = NULL;
Panel::WriteLine Panel::line_ = NULL;
Panel::Frame Panel::flush_ = NULL;

void Panel::writeThrough(Frame clear, WriteLine line, Frame flush)
{
    clear_ = clear;
    line_ = line;
    flush_ = flush;
}

void Panel::begin()
{
    if (clear_ != NULL)
        clear_();
}

// Only the selected row, the panel having one item in view. The others still
// arrive, because a renderer is told the page rather than asked for a row.
void Panel::row(const MenuPage &page, uint8_t index, const char *value)
{
    if (line_ == NULL || index != page.selected())
        return;
    const char *const level = page.title();
    line_(LevelRow, level != NULL ? level : RootLevel);
    const MenuItem &item = page.itemAt(index);
    // The panel has no colour, so it says in words what the overlay greys.
    if (!page.availableAt(index)) {
        line_(LabelRow, item.label());
        line_(ValueRow, UnavailableValue);
        return;
    }
    line_(item.hasValue() ? LabelRow : AloneRow, item.label());
    if (item.hasValue() && value != NULL)
        line_(ValueRow, value);
}

void Panel::end(const MenuPage &)
{
    if (flush_ != NULL)
        flush_();
}

const MenuRenderer &Panel::renderer()
{
    static const MenuRenderer instance(begin, row, end);
    return instance;
}

}  // namespace Osd
