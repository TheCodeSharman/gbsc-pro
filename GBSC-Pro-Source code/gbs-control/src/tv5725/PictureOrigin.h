#ifndef TV5725_PICTURE_ORIGIN_H_
#define TV5725_PICTURE_ORIGIN_H_

// Where the picture lands on the raster, and the register that puts it there.
#include <stdint.h>

namespace Tv5725 {
class PictureOrigin {
public:
    PictureOrigin();
    PictureOrigin(int32_t corner, int32_t windowStop);

    int32_t corner() const;      // first written pixel
    int32_t windowStop() const;    // VDS_?B_SP to put it there

private:
    int32_t corner_, windowStop_;
};

}  // namespace Tv5725

#endif  // TV5725_PICTURE_ORIGIN_H_
