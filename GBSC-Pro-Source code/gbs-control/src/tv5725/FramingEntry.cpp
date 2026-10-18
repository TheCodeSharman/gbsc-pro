#include "FramingEntry.h"

#include <math.h>

#include "Axis.h"

namespace Tv5725 {

namespace {
const float Whole = 10000.0f;
const float RateCentihertz = 100.0f;
}  // namespace

FramingEntry::FramingEntry()
    : lines_(0), rateCentihertz_(0), syncWidth_(0),
      horizontalOrigin_(0), horizontalExtent_(0),
      verticalOrigin_(0), verticalExtent_(0),
      shape_(Aspect::Fill), flags_(0)
{
}

uint16_t FramingEntry::tenThousandthsOf(float proportion)
{
    const long value = lrintf(proportion * Whole);
    if (value <= 0)
        return 0;
    return value > 0xFFFF ? 0xFFFF : (uint16_t)value;
}

float FramingEntry::proportionOf(uint16_t tenThousandths)
{
    return (float)tenThousandths / Whole;
}

uint8_t FramingEntry::polarityAt(SourceKey::Polarity polarity, uint8_t shift)
{
    return (uint8_t)((polarity & PolarityBits) << shift);
}

SourceKey::Polarity FramingEntry::polarityAt(uint8_t shift) const
{
    return (SourceKey::Polarity)((flags_ >> shift) & PolarityBits);
}

void FramingEntry::frame(const PanAndZoom &framing)
{
    horizontalOrigin_ = tenThousandthsOf(framing.originOn(AxisHorizontal));
    horizontalExtent_ = tenThousandthsOf(framing.extentOn(AxisHorizontal));
    verticalOrigin_ = tenThousandthsOf(framing.originOn(AxisVertical));
    verticalExtent_ = tenThousandthsOf(framing.extentOn(AxisVertical));

    flags_ &= (uint8_t)~(HorizontalTuned | VerticalTuned);
    if (framing.tunedOn(AxisHorizontal))
        flags_ |= HorizontalTuned;
    if (framing.tunedOn(AxisVertical))
        flags_ |= VerticalTuned;
}

FramingEntry::FramingEntry(const SourceKey &key, const PanAndZoom &framing,
                           Aspect shape)
    : lines_(key.lines()),
      rateCentihertz_((uint16_t)lrintf(key.rateHz() * RateCentihertz)),
      syncWidth_(tenThousandthsOf(key.syncWidth())),
      shape_(shape.tenThousandths()),
      flags_((uint8_t)(polarityAt(key.hsyncPolarity(), HsyncPolarityShift)
                       | polarityAt(key.vsyncPolarity(), VsyncPolarityShift)))
{
    frame(framing);
}

void FramingEntry::reframe(const PanAndZoom &framing, Aspect shape)
{
    frame(framing);
    shape_ = shape.tenThousandths();
}

SourceKey FramingEntry::key() const
{
    return SourceKey(lines_, (float)rateCentihertz_ / RateCentihertz,
                     proportionOf(syncWidth_),
                     polarityAt(HsyncPolarityShift),
                     polarityAt(VsyncPolarityShift));
}

PanAndZoom FramingEntry::framing() const
{
    PanAndZoom framing;
    if (flags_ & HorizontalTuned)
        framing.seedOn(AxisHorizontal, proportionOf(horizontalOrigin_),
                       proportionOf(horizontalExtent_));
    if (flags_ & VerticalTuned)
        framing.seedOn(AxisVertical, proportionOf(verticalOrigin_),
                       proportionOf(verticalExtent_));
    return framing;
}

Aspect FramingEntry::shape() const { return Aspect(shape_); }

}  // namespace Tv5725
