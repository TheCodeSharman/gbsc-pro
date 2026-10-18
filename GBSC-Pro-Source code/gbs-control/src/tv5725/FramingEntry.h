#ifndef TV5725_FRAMING_ENTRY_H_
#define TV5725_FRAMING_ENTRY_H_

// One stored framing, held as what a save and a load keeps rather than as what
// the engine works in.
//
// How many sources the table can carry is bounded by the ESP8266's globals, and
// the live form costs two and a half times the stored one for no precision the
// file persists: the key's rate is quantised to RateStepsPerHz and every
// proportion the file states is in ten-thousandths. One input unit is at least
// eight ten-thousandths on any line this chip captures, so the window a framing
// describes survives the round trip exactly even though the float does not.
//
// The file's record is the same record: FramingLine is this as text.
// docs/framing-presets.md

#include <stdint.h>

#include "Aspect.h"
#include "PanAndZoom.h"
#include "SourceKey.h"

namespace Tv5725 {

class FramingEntry {
public:
    FramingEntry();
    FramingEntry(const SourceKey &key, const PanAndZoom &framing, Aspect shape);

    SourceKey key() const;
    PanAndZoom framing() const;
    Aspect shape() const;

    // The stored key stands: what a re-tune replaces is the framing and the
    // shape, and the reading that found the entry is not what identifies it.
    void reframe(const PanAndZoom &framing, Aspect shape);

    // The grammar's unit, and the store's. Clamped rather than wrapped: a
    // proportion outside the region it is a proportion of describes no window,
    // and a wrap would describe a plausible wrong one.
    static uint16_t tenThousandthsOf(float proportion);
    static float proportionOf(uint16_t tenThousandths);

private:
    // Which axes carry a framing of their own, and the two polarities. An axis
    // nobody has framed has no proportion, so it is not the same as one framed
    // at zero: CaptureWindow gives the first the computed default for the mode
    // and the second a window sixteen units wide.
    enum Flag {
        HsyncPolarityShift = 0,
        VsyncPolarityShift = 2,
        PolarityBits = 0x03,
        HorizontalTuned = 0x10,
        VerticalTuned = 0x20
    };

    static uint8_t polarityAt(SourceKey::Polarity polarity, uint8_t shift);
    SourceKey::Polarity polarityAt(uint8_t shift) const;
    void frame(const PanAndZoom &framing);

    uint16_t lines_;
    uint16_t rateCentihertz_;
    uint16_t syncWidth_;
    uint16_t horizontalOrigin_, horizontalExtent_;
    uint16_t verticalOrigin_, verticalExtent_;
    uint16_t shape_;
    uint8_t flags_;
};

}  // namespace Tv5725

#endif  // TV5725_FRAMING_ENTRY_H_
