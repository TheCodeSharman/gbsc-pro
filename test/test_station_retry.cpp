// Host-compiled unit tests for Net::StationRetry -- `make -C test station-retry`.
//
// The AP fallback used to be terminal: PersWiFiManager::startApMode() cleared
// the one timestamp handleWiFi() gates on, so nothing attempted the station
// again for the life of the boot and the unit left the network until someone
// power-cycled it. This is the cadence that makes the attempt repeat.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/net/StationRetry.h"

using Net::StationRetry;

TEST_CASE("nothing is due until the retry is armed")
{
    StationRetry retry;
    CHECK_FALSE(retry.due(1000000));
}

TEST_CASE("an armed retry waits the interval out before the first attempt")
{
    StationRetry retry;
    retry.arm(1000);
    CHECK_FALSE(retry.due(1000 + StationRetry::RetryIntervalMs - 1));
}

TEST_CASE("an armed retry comes due once the interval has passed")
{
    StationRetry retry;
    retry.arm(1000);
    CHECK(retry.due(1000 + StationRetry::RetryIntervalMs));
}

TEST_CASE("an attempt that failed is retried an interval later, for ever")
{
    StationRetry retry;
    retry.arm(1000);
    uint32_t now = 1000;
    for (int attempt = 0; attempt < 500; ++attempt) {
        now += StationRetry::RetryIntervalMs;
        REQUIRE(retry.due(now));
        retry.attempted(now);
        CHECK_FALSE(retry.due(now + StationRetry::RetryIntervalMs - 1));
    }
}

TEST_CASE("a disarmed retry asks for nothing further")
{
    StationRetry retry;
    retry.arm(1000);
    retry.disarm();
    CHECK_FALSE(retry.due(1000 + StationRetry::RetryIntervalMs * 10));
}

// The surrounding code takes the same care -- debugPinSpanEdges() uses a signed
// difference so its deadline survives the wrap rather than spinning for another
// 49 days. An unsigned comparison here would stop retrying for that long, which
// is the defect this class exists to remove.
TEST_CASE("the cadence survives the millis wrap")
{
    StationRetry retry;
    const uint32_t beforeWrap = 0xFFFFFFFFu - (StationRetry::RetryIntervalMs / 2);
    retry.arm(beforeWrap);
    const uint32_t afterWrap = beforeWrap + StationRetry::RetryIntervalMs;
    CHECK(afterWrap < beforeWrap);
    CHECK(retry.due(afterWrap));
}
