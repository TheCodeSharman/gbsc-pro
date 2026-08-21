#ifndef TV5725_BLANKING_TIMING_H_
#define TV5725_BLANKING_TIMING_H_

// One axis of a blanking window, in the register's own sense: stop() is where
// blanking stops and the picture begins, start() is where it starts again.
#include <stdint.h>

namespace Tv5725 {
class BlankingTiming {
public:
    BlankingTiming();
    BlankingTiming(int32_t stop, int32_t start);

    int32_t stop() const;
    int32_t start() const;
    int32_t width() const;
    bool usable() const;

private:
    int32_t stop_, start_;
};

}  // namespace Tv5725

#endif  // TV5725_BLANKING_TIMING_H_
