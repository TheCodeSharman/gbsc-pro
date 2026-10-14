// The whole of SSD1306Wire.h that the OLED menu's item and child storage need.
// The real header pulls in Arduino.h, so this one does too.

#ifndef FAKE_SSD1306WIRE_H_
#define FAKE_SSD1306WIRE_H_

#include <Arduino.h>

enum OLEDDISPLAY_TEXT_ALIGNMENT
{
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_RIGHT = 1,
    TEXT_ALIGN_CENTER = 2,
    TEXT_ALIGN_CENTER_BOTH = 3
};

#endif
