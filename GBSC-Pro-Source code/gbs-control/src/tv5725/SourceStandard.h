#ifndef TV5725_SOURCE_STANDARD_H
#define TV5725_SOURCE_STANDARD_H

#include <stdint.h>

namespace Tv5725 {

// The one register pair a classification of the source still decides: where the
// sync processor looks for vertical sync inside composite sync. Everything else
// it used to write is derived from something the engine measures.
//
// **THIS CLASS IS THE VIDEO STANDARD CONCEPT AND IS BEING RETIRED.** The window
// belongs to Tv5725::SourceTiming, whose published rasters already state where
// vertical sync sits in a frame. docs/video-source-acquisition.md
class SourceStandard {
public:
    explicit SourceStandard(uint8_t videoStandardInput);

    void apply() const;

private:
    bool isProgressive() const;  // 3, 4, 8 and 9

    uint8_t standard_;
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_STANDARD_H
