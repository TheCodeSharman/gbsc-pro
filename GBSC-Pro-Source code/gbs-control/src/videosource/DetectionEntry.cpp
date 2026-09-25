#include "DetectionEntry.h"

const uint16_t DetectionEntry::WindowMs;

DetectionEntry::Step DetectionEntry::stepAt(bool signalPresent, uint32_t waitedMs)
{
    if (signalPresent)
        return Act;
    return waitedMs >= WindowMs ? GiveUp : Wait;
}
