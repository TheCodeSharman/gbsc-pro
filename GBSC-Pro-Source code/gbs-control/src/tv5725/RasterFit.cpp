#include "RasterFit.h"

namespace Tv5725 {

RasterFit::RasterFit() : scale_(Scale(Scale::Max)), produced_(0.0f) {}

RasterFit::RasterFit(Scale scale, float produced) : scale_(scale), produced_(produced) {}

Scale RasterFit::scale() const { return scale_; }

float RasterFit::produced() const { return produced_; }

}  // namespace Tv5725
