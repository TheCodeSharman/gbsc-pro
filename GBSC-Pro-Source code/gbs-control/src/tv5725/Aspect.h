#ifndef TV5725_ASPECT_H_
#define TV5725_ASPECT_H_

// The shape a picture is to be SHOWN in, width over height in ten-thousandths.
//
// It cannot be measured. The chip sees sync edges rather than pixels, so the
// source's pixel clock is unknowable and how many samples a line is cut into is
// our own choice -- 320x256 and 640x256 arrive as one key. So it is a
// convention: defaulted from the raster the source matched, stored per source,
// and overridable from the remote. docs/aspect-ratio.md
//
// Ten-thousandths because that is what the framing file already carries, so a
// stored shape is one more integer in a grammar that has no floats.

#include <stdint.h>

namespace Tv5725 {

class Axis;

class Aspect {
public:
    // Fill is the absence of a shape rather than a shape: the picture takes
    // whatever raster it is given, which is what the engine did before shapes
    // existed.
    static const uint16_t Fill = 0;
    static const uint16_t FiveFour = 12500;
    static const uint16_t FourThree = 13333;
    static const uint16_t SixteenNine = 17778;

    Aspect();
    explicit Aspect(uint16_t tenThousandths);

    uint16_t tenThousandths() const;
    bool fills() const;

    // How far the room on `axis` must narrow for a raster shaped `shown` to
    // display this shape. One on the axis that still fills, and one on both
    // where either shape is absent.
    //
    // The raster's own units are not square and need not be: the encoder
    // resamples the whole line into the standard's active pixel count, so a
    // picture taking this fraction of the room takes the same fraction of the
    // emitted width. That is also why the chain's carried width must not enter
    // it -- after the compensation the picture lands on the FULL emitted width,
    // so folding it in would letterbox every SD output by a few percent.
    float roomFraction(const Axis &axis, Aspect shown) const;

    // The next shape the menu offers. Filling is not among them: the panel is
    // 16:9, so it and SixteenNine are one picture and the step changed nothing.
    // A shape on none of them -- anything an older framing file carries --
    // steps onto the first, so the ring is reachable from outside it.
    Aspect next() const;

    bool operator==(const Aspect &o) const;
    bool operator!=(const Aspect &o) const;

private:
    uint16_t tenThousandths_;
};

}  // namespace Tv5725

#endif  // TV5725_ASPECT_H_
