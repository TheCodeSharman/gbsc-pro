#include "MenuItem.h"

namespace Osd {

const char *MenuItem::label() const { return label_; }

const MenuCommand &MenuItem::okCommand() const { return ok_; }

const MenuCommand &MenuItem::nextCommand() const { return next_; }

const MenuCommand &MenuItem::previousCommand() const { return previous_; }

const MenuItem *MenuItem::children() const { return children_; }

uint8_t MenuItem::childCount() const { return childCount_; }

bool MenuItem::leadsSomewhere() const
{
    return children_ != NULL && childCount_ != 0;
}

bool MenuItem::hasValue() const { return valueText_ != NULL; }

const char *MenuItem::valueText(const MenuContext &context) const
{
    return valueText_ != NULL ? valueText_(context) : NULL;
}

}  // namespace Osd
