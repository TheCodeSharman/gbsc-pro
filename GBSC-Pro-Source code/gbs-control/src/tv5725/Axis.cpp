#include "Axis.h"

#include <math.h>

namespace Tv5725 {

Axis::Axis(uint16_t captureGranularity, uint16_t captureMargin,
           float activeStart, float activeExtent, bool vertical)
    : captureGranularity_(captureGranularity),
      captureMargin_(captureMargin),
      activeStart_(activeStart), activeExtent_(activeExtent),
      vertical_(vertical) {}

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
    long granules = lrintf(wanted / captureGranularity_);
    if (granules < 1)
        granules = 1;

    long units = granules * captureGranularity_;
    return pixels < 0 ? (int16_t)-units : (int16_t)units;
}

const Axis AxisHorizontal(2, 0, 0.117f, 0.864f, false);

const Axis AxisVertical(1, 2, 0.061f, 0.933f, true);

}  // namespace Tv5725
