#include "OLEDMenuChildBlocks.h"
#include <string.h>

OLEDMenuChildBlocks::OLEDMenuChildBlocks()
{
    memset(this->taken, 0, sizeof(this->taken));
}

OLEDMenuItem **OLEDMenuChildBlocks::acquire()
{
    for (uint8_t i = 0; i < OLED_MENU_MAX_PARENTS_NUM; ++i)
    {
        if (!this->taken[i])
        {
            this->taken[i] = true;
            memset(this->blocks[i], 0, sizeof(this->blocks[i]));
            return this->blocks[i];
        }
    }
    return NULL;
}

void OLEDMenuChildBlocks::release(OLEDMenuItem **block)
{
    for (uint8_t i = 0; i < OLED_MENU_MAX_PARENTS_NUM; ++i)
    {
        if (this->blocks[i] == block)
        {
            this->taken[i] = false;
            return;
        }
    }
}

uint8_t OLEDMenuChildBlocks::freeBlocks() const
{
    uint8_t free = 0;
    for (uint8_t i = 0; i < OLED_MENU_MAX_PARENTS_NUM; ++i)
    {
        if (!this->taken[i])
        {
            ++free;
        }
    }
    return free;
}
