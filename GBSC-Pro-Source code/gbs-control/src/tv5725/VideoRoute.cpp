#include "VideoRoute.h"

namespace Tv5725 {

VideoRoute::Route VideoRoute::route_ = VideoRoute::Scaler;

VideoRoute::Route VideoRoute::route() { return route_; }

bool VideoRoute::isHdBypassChannel() { return route_ == HdBypassChannel; }

void VideoRoute::toScaler() { route_ = Scaler; }

void VideoRoute::toHdBypassChannel() { route_ = HdBypassChannel; }

}  // namespace Tv5725
