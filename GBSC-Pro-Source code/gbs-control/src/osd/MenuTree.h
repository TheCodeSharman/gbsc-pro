#ifndef OSD_MENU_TREE_H_
#define OSD_MENU_TREE_H_

// The menu the remote walks, as data. One declaration per option, so adding or
// removing one is a line rather than five coordinated edits across neighbours'
// Up and Down targets. docs/osd-menu.md

#include <stdint.h>

#include "MenuItem.h"

namespace Osd {

class MenuTree {
public:
    static const MenuItem *root();
    static uint8_t rootCount();
};

}  // namespace Osd

#endif  // OSD_MENU_TREE_H_
