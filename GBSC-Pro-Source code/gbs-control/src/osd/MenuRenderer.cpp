#include "MenuRenderer.h"

#include <stddef.h>

#include "MenuItem.h"
#include "MenuPage.h"

namespace Osd {

void MenuRenderer::draw(const MenuPage &page, const MenuContext &context) const
{
    if (begin_ != NULL)
        begin_();
    if (row_ != NULL)
        for (uint8_t row = 0; row < page.rows(); ++row)
            row_(page, row, page.itemAt(row).valueText(context));
    if (end_ != NULL)
        end_(page);
}

}  // namespace Osd
