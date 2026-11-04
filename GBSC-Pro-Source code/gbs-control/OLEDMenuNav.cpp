#include "OLEDMenuNav.h"

#include <string.h>

bool oledNavFor(const char *key, OLEDMenuNav *into)
{
    if (key == NULL || into == NULL)
        return false;
    if (strcmp(key, "up") == 0)
        *into = OLEDMenuNav::UP;
    else if (strcmp(key, "down") == 0)
        *into = OLEDMenuNav::DOWN;
    else if (strcmp(key, "enter") == 0)
        *into = OLEDMenuNav::ENTER;
    else
        return false;
    return true;
}
