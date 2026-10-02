#ifndef TV5725_INPUT_SCALE_H_
#define TV5725_INPUT_SCALE_H_

// How far the input formatter's own scaling-down block compresses the line.
//
// IT IS THE ONLY MINIFICATION THE PART HAS. VDS_?SCALE divides 1024 and tops
// out at 1023, so the video display scaler's produced picture is never narrower
// than the capture it was given; this block sits ahead of it and can be.
// docs/scaling-down-path.md
//
// The block is NON-LINEAR -- eight segment increments, one per eighth of the
// line, for an anamorphic stretch. One value on all eight is the linear case
// and the only one the engine asks for.
//
// A VALUE, so a solve can carry one and hand it to whoever places a count in IF
// units. The registers are the input formatter's, which owns the byte all three
// fields share.

#include <stdint.h>

namespace Tv5725 {

class InputScale {
public:
    // RD-5725-1.1 states the increment for a scaling ratio n/m as
    // 4095 x (m - n) / n, so n == m is 0 and a ratio of one half is the whole
    // twelve bits. Nothing below a half is reachable without IF_HS_DEC_FACTOR,
    // which the line doubler owns and which cannot carry two meanings at once.
    static const uint16_t Unity = 0;
    static const uint16_t Half = 4095;

    InputScale();
    explicit InputScale(uint16_t increment);

    // The increment that shows `have` units of line in `wanted` of them.
    //
    // TRUNCATED, so the ratio lands at or above the one asked for: a larger
    // increment is a narrower picture, and a black bar is worse than an
    // overrun.
    static InputScale forRatio(uint16_t wanted, uint16_t have);

    uint16_t increment() const;
    bool minifies() const;

    // 4095 / (4095 + increment).
    float ratio() const;

    // What a count of IF units becomes once the line has been compressed into
    // fewer of them. Every count placed in those units follows it -- the
    // capture window, and the counter the line wraps at.
    uint16_t unitsFor(uint16_t units) const;

    // The two halves the registers take: IF_HS_RATE_SEG0..7 carry the top eight
    // bits and IF_HS_RATE_LOW the low nibble they share.
    uint8_t segment() const;
    uint8_t low() const;

    bool operator==(const InputScale &o) const;
    bool operator!=(const InputScale &o) const;

private:
    uint16_t increment_;
};

}  // namespace Tv5725

#endif  // TV5725_INPUT_SCALE_H_
