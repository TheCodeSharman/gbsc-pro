#include "InputScale.h"

namespace Tv5725 {

const uint16_t InputScale::Unity;
const uint16_t InputScale::Half;

InputScale::InputScale() : increment_(Unity) {}

InputScale::InputScale(uint16_t increment)
    : increment_(increment > Half ? Half : increment)
{
}

InputScale InputScale::forRatio(uint16_t wanted, uint16_t have)
{
    if (wanted == 0 || have == 0 || wanted >= have)
        return InputScale(Unity);

    const uint32_t asked = (uint32_t)Half * (uint32_t)(have - wanted)
                         / (uint32_t)wanted;
    return InputScale(asked > Half ? Half : (uint16_t)asked);
}

uint16_t InputScale::increment() const { return increment_; }

bool InputScale::minifies() const { return increment_ != Unity; }

float InputScale::ratio() const
{
    return (float)Half / (float)((uint32_t)Half + increment_);
}

uint16_t InputScale::unitsFor(uint16_t units) const
{
    return (uint16_t)((uint32_t)units * (uint32_t)Half
                      / ((uint32_t)Half + increment_));
}

uint8_t InputScale::segment() const { return (uint8_t)(increment_ >> 4); }

uint8_t InputScale::low() const { return (uint8_t)(increment_ & 0x0F); }

bool InputScale::operator==(const InputScale &o) const
{
    return increment_ == o.increment_;
}

bool InputScale::operator!=(const InputScale &o) const { return !(*this == o); }

}  // namespace Tv5725
