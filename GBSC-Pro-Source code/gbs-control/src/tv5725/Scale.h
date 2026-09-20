#ifndef TV5725_SCALE_H_
#define TV5725_SCALE_H_

// VDS_?SCALE: the register, what it magnifies by, and what it produces.
#include <stdint.h>

namespace Tv5725 {
// VDS_?SCALE divides 1024, so a SMALLER register means MORE magnification.
class Scale {
public:
    static const uint16_t Unity = 1024;

    // The 10-bit field's top, and the only one of the two the part states. Below
    // unity it cannot MINIFY at all: 1024/1023 is a magnification of 1.001.
    static const uint16_t Max = 1023;

    // How far this firmware will magnify. RD-5725-1.1 states NO minimum -- it
    // gives only HSCALE = 1024 x in / out and the field is 10 bits -- so there
    // is no hardware bound here to name, and this is a picture-quality choice.
    //
    // Past 3.0x the solve can no longer centre the picture and pins the memory
    // window at the write floor, and there the scaler selects wrong samples --
    // bars of equal source width come out unequal and split into hairlines,
    // which no interpolation does. Measured entering the floor at VDS_HSCALE
    // 334 on two sources with rasters 1920 and 1280, and the damage rising with
    // magnification from there. 1024/3 is 341.33, so 342 is the largest
    // magnification at or under 3.0.
    //
    // The zoom exists to bring a source's active picture up to full screen, and
    // that is reached well inside this. docs/known-issues.md
    static const uint16_t Min = 342;

    Scale();
    explicit Scale(uint16_t reg);

    uint16_t reg() const;

    // A register of 0 is a dropped read, not a setting. Treating it as 1:1
    // gives a plausible width that is wrong, so the caller has to decide.
    bool valid() const;

    float magnification() const;

    float produced(uint16_t captureUnits) const;

    // Comparable against the register bounds and against each other, so no
    // caller has to reach through to reg() to say something obvious.
    bool operator==(const Scale &o) const;
    bool operator!=(const Scale &o) const;
    bool operator<(const Scale &o) const;
    bool operator>(const Scale &o) const;
    bool operator<=(const Scale &o) const;
    bool operator>=(const Scale &o) const;
    bool operator<=(uint16_t r) const;
    bool operator>=(uint16_t r) const;
    bool operator==(uint16_t r) const;

private:
    uint16_t reg_;
};

}  // namespace Tv5725

#endif  // TV5725_SCALE_H_
