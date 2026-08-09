#ifndef TV5725_SCALE_H_
#define TV5725_SCALE_H_

// VDS_?SCALE: the register, what it magnifies by, and what it produces.
#include <stdint.h>

namespace Tv5725 {
// VDS_?SCALE divides 1024, so a SMALLER register means MORE magnification.
class Scale {
public:
    static const uint16_t Unity = 1024;

    // The register's own limits. How far an AXIS is willing to magnify is a
    // picture-quality judgement, so it lives on Axis -- see Axis::scaleMin.
    static const uint16_t Min = 256;
    static const uint16_t Max = 1023;

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
