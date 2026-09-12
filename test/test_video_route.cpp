// Host-compiled unit tests for src/tv5725/VideoRoute.h -- `make -C test video-route`.
//
// Which route carries the video to the DAC. The routes are alternatives in the
// chip, so the value that says which is carrying holds one of them and not a
// set: a second spelling elsewhere can say two are carrying at once, and the
// loop then has to read both to reconstruct one fact.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoRoute.h"

using Tv5725::VideoRoute;

TEST_CASE("the scaler carries the video until a bypass switch takes the route")
{
    VideoRoute::toScaler();

    CHECK(VideoRoute::route() == VideoRoute::Scaler);
}

TEST_CASE("the scaler takes the route back from a bypass switch")
{
    VideoRoute::toHdBypassChannel();
    VideoRoute::toScaler();

    CHECK(VideoRoute::isHdBypassChannel() == false);
}
