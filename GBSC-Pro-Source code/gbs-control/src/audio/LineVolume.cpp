#include "LineVolume.h"

namespace Audio {

const uint8_t LineVolume::Maximum;
const uint8_t LineVolume::Default;

uint8_t LineVolume::attenuationDb(uint8_t setting)
{
    return inRange(setting);
}

uint8_t LineVolume::displayLevel(uint8_t setting)
{
    return (uint8_t)(Maximum - inRange(setting));
}

uint8_t LineVolume::louder(uint8_t setting)
{
    return setting > 0 ? (uint8_t)(setting - 1) : 0;
}

uint8_t LineVolume::quieter(uint8_t setting)
{
    return setting < Maximum ? (uint8_t)(setting + 1) : Maximum;
}

uint8_t LineVolume::inRange(uint8_t setting)
{
    return setting > Maximum ? Maximum : setting;
}

}  // namespace Audio
