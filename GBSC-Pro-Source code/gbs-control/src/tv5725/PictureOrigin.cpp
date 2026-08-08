#include "PictureOrigin.h"

namespace Tv5725 {

PictureOrigin::PictureOrigin() : corner_(0), windowStop_(0) {}

PictureOrigin::PictureOrigin(int32_t corner, int32_t windowStop)
    : corner_(corner), windowStop_(windowStop) {}

int32_t PictureOrigin::corner() const { return corner_; }

int32_t PictureOrigin::windowStop() const { return windowStop_; }

}  // namespace Tv5725
