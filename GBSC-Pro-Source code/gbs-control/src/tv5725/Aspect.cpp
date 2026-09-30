#include "Aspect.h"

#include "Axis.h"

namespace Tv5725 {

const uint16_t Aspect::Fill;
const uint16_t Aspect::FiveFour;
const uint16_t Aspect::FourThree;
const uint16_t Aspect::SixteenNine;

Aspect::Aspect() : tenThousandths_(Fill) {}

Aspect::Aspect(uint16_t tenThousandths) : tenThousandths_(tenThousandths) {}

uint16_t Aspect::tenThousandths() const { return tenThousandths_; }

bool Aspect::fills() const { return tenThousandths_ == Fill; }

float Aspect::roomFraction(const Axis &axis, Aspect shown) const
{
    if (fills() || shown.fills())
        return 1.0f;

    const float wanted = (float)tenThousandths_;
    const float raster = (float)shown.tenThousandths();
    if (axis.vertical())
        return wanted > raster ? raster / wanted : 1.0f;
    return wanted < raster ? wanted / raster : 1.0f;
}

bool Aspect::operator==(const Aspect &o) const
{
    return tenThousandths_ == o.tenThousandths_;
}

bool Aspect::operator!=(const Aspect &o) const { return !(*this == o); }

}  // namespace Tv5725
