#include "MenuRenderer.h"

#include <stddef.h>

#include "MenuItem.h"
#include "MenuPage.h"

namespace Osd {

void MenuRenderer::draw(const MenuPage &page, const MenuContext &context) const
{
    if (begin_ != NULL)
        begin_();
    if (row_ != NULL) {
        for (uint8_t row = 0; row < page.rows(); ++row) {
            const MenuItem &item = page.itemAt(row);
            row_(row, item.label(), item.valueText(context),
                 row == page.selected());
        }
    }
    if (end_ != NULL)
        end_();
}

}  // namespace Osd
