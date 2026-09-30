#include "MenuPage.h"

namespace Osd {

// Defined as well as declared: doctest's CHECK binds each operand to a const
// reference, which is an ODR use. CODING_STYLE.md.
const uint8_t MenuPage::Rows;

MenuPage::MenuPage() : rows_(0), selected_(0)
{
    for (uint8_t i = 0; i < Rows; ++i) {
        labels_[i] = NULL;
        values_[i] = NULL;
    }
}

void MenuPage::add(const char *label, const char *value)
{
    if (rows_ >= Rows)
        return;
    labels_[rows_] = label;
    values_[rows_] = value;
    ++rows_;
}

void MenuPage::select(uint8_t row) { selected_ = row; }

uint8_t MenuPage::rows() const { return rows_; }

const char *MenuPage::labelAt(uint8_t row) const
{
    return row < rows_ ? labels_[row] : NULL;
}

const char *MenuPage::valueAt(uint8_t row) const
{
    return row < rows_ ? values_[row] : NULL;
}

uint8_t MenuPage::selected() const { return selected_; }

}  // namespace Osd
