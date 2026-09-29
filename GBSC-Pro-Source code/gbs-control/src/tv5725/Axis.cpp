#include "Axis.h"

#include "Scale.h"

#include <math.h>

namespace Tv5725 {

Axis::Axis(uint16_t captureGranularity, uint16_t captureMargin,
           float activeStart, float activeExtent, bool vertical,
           uint16_t magnificationFloor, uint16_t scaleCeiling)
    : captureGranularity_(captureGranularity),
      captureMargin_(captureMargin),
      activeStart_(activeStart), activeExtent_(activeExtent),
      vertical_(vertical), magnificationFloor_(magnificationFloor),
      scaleCeiling_(scaleCeiling) {}

uint16_t Axis::magnificationFloor() const { return magnificationFloor_; }

uint16_t Axis::scaleCeiling() const { return scaleCeiling_; }

bool Axis::vertical() const { return vertical_; }

float Axis::activeStart() const { return activeStart_; }

float Axis::activeExtent() const { return activeExtent_; }

uint16_t Axis::captureGranularity() const { return captureGranularity_; }

uint16_t Axis::captureMargin() const { return captureMargin_; }

int16_t Axis::stepUnits(int16_t pixels, float magnification) const
{
    float wanted = (pixels < 0 ? -pixels : pixels) / magnification;

    // Rounded ONCE, in output pixels. Rounding to units first and to granules
    // after biases every request upwards: 4.73 units becomes 5, then 6, when 4
    // is the nearer of the two the hardware can reach.
    //
    // NOTHING is the nearest move to a request under half a granule, and it is
    // returned rather than floored to one: a granule is 2 x magnification
    // output pixels horizontally, five of them at the bench framing, so a floor
    // moves the picture five times as far as a one-pixel press asked for.
    const long units = lrintf(wanted / captureGranularity_) * captureGranularity_;
    return pixels < 0 ? (int16_t)-units : (int16_t)units;
}

const Axis AxisHorizontal(2, 1, 0.117f, 0.864f, false, 342, Scale::Max);

const Axis AxisVertical(1, 2, 0.061f, 0.933f, true, 205, Scale::Max);

}  // namespace Tv5725
