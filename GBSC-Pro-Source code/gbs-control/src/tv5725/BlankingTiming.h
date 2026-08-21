#ifndef TV5725_BLANKING_TIMING_H_
#define TV5725_BLANKING_TIMING_H_

// One axis of a blanking window, in the register's own sense: stop() is where
// blanking stops and the picture begins, start() is where it starts again.
#include <stdint.h>

namespace Tv5725 {
// IF units horizontally, HALF-LINES vertically.
class BlankingTiming {
public:
    BlankingTiming();
    BlankingTiming(uint16_t stop, uint16_t start);

    uint16_t stop() const;
    uint16_t start() const;
    uint16_t width() const;
    bool usable() const;

private:
    uint16_t stop_, start_;
};

}  // namespace Tv5725

#endif  // TV5725_BLANKING_TIMING_H_
