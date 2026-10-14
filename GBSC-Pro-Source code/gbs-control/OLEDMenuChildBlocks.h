#ifndef OLED_MENU_CHILD_BLOCKS_H_
#define OLED_MENU_CHILD_BLOCKS_H_
#include <Arduino.h>
#include "OLEDMenuConfig.h"

class OLEDMenuItem;

// Child arrays for the items that have children. The item pool is the largest
// static allocation in the firmware, so an array per parent rather than per
// item is what keeps it off the leaves.
class OLEDMenuChildBlocks
{
public:
    OLEDMenuChildBlocks();
    OLEDMenuItem **acquire();
    void release(OLEDMenuItem **block);
    uint8_t freeBlocks() const;

private:
    OLEDMenuItem *blocks[OLED_MENU_MAX_PARENTS_NUM][OLED_MENU_MAX_SUBITEMS_NUM];
    bool taken[OLED_MENU_MAX_PARENTS_NUM];
};
#endif
