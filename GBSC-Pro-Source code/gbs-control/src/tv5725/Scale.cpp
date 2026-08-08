#include "Scale.h"

namespace Tv5725 {

const uint16_t Scale::Unity;
const uint16_t Scale::Min;
const uint16_t Scale::Max;

Scale::Scale() : reg_(Max) {}

Scale::Scale(uint16_t reg) : reg_(reg) {}

uint16_t Scale::reg() const { return reg_; }

bool Scale::valid() const { return reg_ != 0; }

float Scale::magnification() const
{
    return valid() ? (float)Unity / reg_ : 0.0f;
}

float Scale::produced(uint16_t captureUnits) const
{
    float factor = magnification();
    return factor == 0.0f ? 0.0f : captureUnits * factor;
}

bool Scale::operator==(const Scale &o) const { return reg_ == o.reg_; }
bool Scale::operator!=(const Scale &o) const { return reg_ != o.reg_; }
bool Scale::operator<(const Scale &o) const { return reg_ < o.reg_; }
bool Scale::operator>(const Scale &o) const { return reg_ > o.reg_; }
bool Scale::operator<=(const Scale &o) const { return reg_ <= o.reg_; }
bool Scale::operator>=(const Scale &o) const { return reg_ >= o.reg_; }
bool Scale::operator<=(uint16_t r) const { return reg_ <= r; }
bool Scale::operator>=(uint16_t r) const { return reg_ >= r; }
bool Scale::operator==(uint16_t r) const { return reg_ == r; }

}  // namespace Tv5725
