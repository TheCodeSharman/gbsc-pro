#include "MenuPage.h"

#include "MenuItem.h"

namespace Osd {

// Defined as well as declared, because binding it to a const reference is an
// ODR use and an in-class initialiser alone does not survive one.
const uint8_t MenuPage::Rows;

MenuPage::MenuPage() : rows_(0), selected_(0)
{
    for (uint8_t i = 0; i < Rows; ++i)
        items_[i] = NULL;
}

void MenuPage::add(const MenuItem &item)
{
    if (rows_ >= Rows)
        return;
    items_[rows_] = &item;
    ++rows_;
}

void MenuPage::select(uint8_t row) { selected_ = row; }

uint8_t MenuPage::rows() const { return rows_; }

const MenuItem &MenuPage::itemAt(uint8_t row) const { return *items_[row]; }

const char *MenuPage::labelAt(uint8_t row) const
{
    return row < rows_ ? items_[row]->label() : NULL;
}

uint8_t MenuPage::selected() const { return selected_; }

}  // namespace Osd
