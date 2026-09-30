#include "MenuRenderer.h"

#include <stddef.h>

namespace Osd {

void MenuRenderer::draw(const MenuPage &page) const
{
    if (begin_ != NULL)
        begin_();
    if (row_ != NULL) {
        for (uint8_t row = 0; row < page.rows(); ++row)
            row_(row, page.labelAt(row), page.valueAt(row),
                 row == page.selected());
    }
    if (end_ != NULL)
        end_();
}

}  // namespace Osd
