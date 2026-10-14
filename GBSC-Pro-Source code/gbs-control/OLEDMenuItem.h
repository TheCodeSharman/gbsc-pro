/*
   See https://github.com/PSHarold/OLED-SSD1306-Menu
   for original code, license and documentation
*/
#ifndef OLED_MENU_ITEM_H_
#define OLED_MENU_ITEM_H_
#include <string.h>
#include "SSD1306Wire.h"
#include "OLEDMenuConfig.h"
#include "OLEDMenuChildBlocks.h"

class OLEDMenuItem;
class OLEDMenuManager;
enum class OLEDMenuNav;
typedef bool (*MenuItemHandler)(OLEDMenuManager *, OLEDMenuItem *, OLEDMenuNav, bool);

class OLEDMenuItem
{
    friend class OLEDMenuManager;

public:
    // False where no block was free or the child would not fit one, which
    // leaves the item as it was.
    bool addSubItem(OLEDMenuItem *item, OLEDMenuChildBlocks &blocks);
    void clearSubItems(OLEDMenuChildBlocks &blocks);

private:
    OLEDDISPLAY_TEXT_ALIGNMENT alignment;
    void calculate();

public:
    bool used;
    uint16_t tag;
    uint16_t imageWidth;
    uint16_t imageHeight;
    uint16_t alignOffset; // used if imageWidth < OLED_MENU_WIDTH
    uint8_t pageInParent;
    uint8_t maxPageIndex;
    uint8_t numSubItem; // 子项目数
    const uint8_t *xbmImage;
    const char *str;
    const uint8_t *font;
    MenuItemHandler handler;
    OLEDMenuItem *parent;
    bool alwaysScrolls;
    int16_t scrollOffset;
    OLEDMenuItem **subItems; // NULL until the first child
};
#endif
