#include "MenuItem.h"

namespace Osd {

const char *MenuItem::label() const { return label_; }

MenuItem::Kind MenuItem::kind() const { return kind_; }

char MenuItem::command() const { return command_; }

const MenuItem *MenuItem::children() const { return children_; }

uint8_t MenuItem::childCount() const { return childCount_; }

bool MenuItem::hasValue() const { return valueText_ != NULL; }

const char *MenuItem::valueText(const MenuContext &context) const
{
    return valueText_ != NULL ? valueText_(context) : NULL;
}

}  // namespace Osd
