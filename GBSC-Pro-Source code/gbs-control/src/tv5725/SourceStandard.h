#ifndef TV5725_SOURCE_STANDARD_H
#define TV5725_SOURCE_STANDARD_H

#include <stdint.h>

namespace Tv5725 {

// The settings Mode Detect's classification of the source implies and no
// measurement supplies: the ADC's analog filter, the sampling density, and how
// the input formatter and the video processor handle a line of that shape.
// Everything derivable from the source belongs to the geometry engine instead.
class SourceStandard {
public:
    SourceStandard(uint8_t videoStandardInput, bool inputIsYpBpR);

    // Returns the oversampling installed. postDivider is the one in force,
    // which is the previous mode's: a standard with a sampling density of its
    // own replaces it rather than reading it back.
    uint8_t apply(uint8_t postDivider) const;

private:
    bool isSd() const;           // 1 and 2, interlaced SD
    bool isProgressive() const;  // 3, 4, 8 and 9
    bool isHd() const;           // 5, 6 and 7, reached through the HD bypass switch

    uint8_t applySd() const;
    uint8_t applyProgressive() const;
    void applyHd() const;

    uint8_t standard_;
    bool inputIsYpBpR_;
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_STANDARD_H
