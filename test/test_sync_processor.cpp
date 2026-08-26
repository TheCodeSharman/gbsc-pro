// Host-compiled unit tests for SyncProcessor::applyForSyncType() --
// `make -C test sync-processor`.
//
// Fields are read back through their own typedefs, never through a hand-written
// address: a wrong address does not error, it returns a plausible number.
//
// "Not written" is proved by running under two COMPLEMENTARY poisons and
// checking the two disagree. One poison cannot tell a field written 0 from a
// field left at a poison whose bit is already 0.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"

using namespace Tv5725;

static const uint8_t Poisons[2] = {0xA5, 0x5A};

template <typename Field>
static uint32_t applied(bool csync)
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::applyForSyncType(csync);
    return Field::read();
}

template <typename Field>
static bool wasWritten(bool csync)
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        SyncProcessor::applyForSyncType(csync);
        under[i] = Field::read();
    }
    return under[0] == under[1];
}

TEST_CASE("separate sync runs off the source's own H and V, uncoasted")
{
    const bool csync = false;

    CHECK(applied<SyncProcessor::SP_SOG_SRC_SEL>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_EXT_SYNC_SEL>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_SOG_MODE>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_NO_COAST_REG>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_PRE_COAST>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_POST_COAST>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_H_PULSE_IGNOR>(csync) == 0xff);
    CHECK(applied<SyncProcessor::SP_SYNC_BYPS>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_HS_POL_ATO>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_VS_POL_ATO>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_HS_LOOP_SEL>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_H_PROTECT>(csync) == 0);
}

TEST_CASE("csync coasts around the vertical interval and protects the line")
{
    const bool csync = true;

    CHECK(applied<SyncProcessor::SP_SOG_SRC_SEL>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_EXT_SYNC_SEL>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_SOG_MODE>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_NO_COAST_REG>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_PRE_COAST>(csync) == 4);
    CHECK(applied<SyncProcessor::SP_POST_COAST>(csync) == 7);
    CHECK(applied<SyncProcessor::SP_SYNC_BYPS>(csync) == 0);
    CHECK(applied<SyncProcessor::SP_HS_LOOP_SEL>(csync) == 1);
    CHECK(applied<SyncProcessor::SP_H_PROTECT>(csync) == 1);
}

TEST_CASE("the three fields csync never wrote are still not written")
{
    // Adding a write here would be a behaviour change wearing the clothes of a
    // move. The separate-sync branch owns all three.
    CHECK_FALSE(wasWritten<SyncProcessor::SP_HS_POL_ATO>(true));
    CHECK_FALSE(wasWritten<SyncProcessor::SP_VS_POL_ATO>(true));
    CHECK_FALSE(wasWritten<SyncProcessor::SP_H_PULSE_IGNOR>(true));

    CHECK(wasWritten<SyncProcessor::SP_HS_POL_ATO>(false));
    CHECK(wasWritten<SyncProcessor::SP_VS_POL_ATO>(false));
    CHECK(wasWritten<SyncProcessor::SP_H_PULSE_IGNOR>(false));
}

TEST_CASE("sync-on-green is enabled on both paths")
{
    // Tv5725::Adc's field, written by both copies this replaced, so it travels
    // with them rather than being left behind.
    CHECK(applied<Adc::ADC_SOGEN>(false) == 1);
    CHECK(applied<Adc::ADC_SOGEN>(true) == 1);
}
