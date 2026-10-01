#include "MenuPage.h"

#include "MenuItem.h"

namespace Osd {

// Defined as well as declared, because binding it to a const reference is an
// ODR use and an in-class initialiser alone does not survive one.
const uint8_t MenuPage::Rows;

MenuPage::MenuPage()
    : title_(NULL), rows_(0), selected_(0), number_(0), previous_(false),
      next_(false), adjusting_(false)
{
    for (uint8_t i = 0; i < Rows; ++i)
        items_[i] = NULL;
}

void MenuPage::nameLevel(const char *title) { title_ = title; }

void MenuPage::add(const MenuItem &item)
{
    if (rows_ >= Rows)
        return;
    items_[rows_] = &item;
    ++rows_;
}

void MenuPage::select(uint8_t row) { selected_ = row; }

void MenuPage::numberPage(uint8_t number, bool previous, bool next)
{
    number_ = number;
    previous_ = previous;
    next_ = next;
}

void MenuPage::markAdjusting() { adjusting_ = true; }

const char *MenuPage::title() const { return title_; }

uint8_t MenuPage::rows() const { return rows_; }

uint8_t MenuPage::positionAt(uint8_t row) const
{
    return (uint8_t)((number_ - 1) * Rows + row + 1);
}

const MenuItem &MenuPage::itemAt(uint8_t row) const { return *items_[row]; }

const char *MenuPage::labelAt(uint8_t row) const
{
    return row < rows_ ? items_[row]->label() : NULL;
}

bool MenuPage::descendsAt(uint8_t row) const
{
    return row < rows_ && items_[row]->leadsSomewhere()
           && !items_[row]->isPad();
}

uint8_t MenuPage::selected() const { return selected_; }

bool MenuPage::adjusting() const { return adjusting_; }

uint8_t MenuPage::number() const { return number_; }

bool MenuPage::hasPreviousPage() const { return previous_; }

bool MenuPage::hasNextPage() const { return next_; }

}  // namespace Osd
