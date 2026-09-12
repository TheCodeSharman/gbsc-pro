#ifndef TV5725_SOURCE_STANDARD_H
#define TV5725_SOURCE_STANDARD_H

#include <stdint.h>

namespace Tv5725 {

// The settings Mode Detect's classification of the source implies and no
// measurement supplies: the ADC's analog filter, and how the input formatter
// and the video processor handle a line of that shape. Everything derivable
// from the source belongs to the geometry engine instead.
//
// **NOT THE ADC PLL GROUP.** Adc::applySampleRate() owns the divider, the
// crossover row, the VCO gain and the clock tap, and derives all four from the
// clock the engine's divider and measured line rate make between them. A
// literal here is a second derivation of the same thing against a post divider
// nobody measured.
class SourceStandard {
public:
    SourceStandard(uint8_t videoStandardInput, bool inputIsYpBpR);

    void apply() const;

private:
    bool isSd() const;           // 1 and 2, interlaced SD
    bool isProgressive() const;  // 3, 4, 8 and 9
    bool isHd() const;           // 5, 6 and 7, reached through the HD bypass switch

    void applySd() const;
    void applyProgressive() const;
    void applyHd() const;

    uint8_t standard_;
    bool inputIsYpBpR_;
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_STANDARD_H
