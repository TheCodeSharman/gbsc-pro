#ifndef GEOMETRY_FIT_H_
#define GEOMETRY_FIT_H_

// The scale that fills a raster, and how big the picture then is.
#include <stdint.h>
#include "Scale.h"

namespace Tv5725 {
class RasterFit {
public:
    RasterFit();
    RasterFit(Scale scale, float produced);

    Scale scale() const;
    float produced() const;

private:
    Scale scale_;
    float produced_;
};

}  // namespace Tv5725

#endif  // GEOMETRY_FIT_H_
