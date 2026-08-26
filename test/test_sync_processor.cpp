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

TEST_CASE("the SD vertical sync positions are one value each, not two halves")
{
    // SP_SDCS_VSST and SP_SDCS_VSSP are 11 bits split across a low byte and a
    // three-bit high field in a different register. Written as halves they
    // drift: setOutModeHdBypass() sets a start of 301, so the high field holds
    // 1, and a later path writing only the low byte with 2 leaves 258.
    Wire.reset();
    Wire.poison(0x00);

    SyncProcessor::writeSdVsyncStart(301);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 1);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 45);

    SUBCASE("a smaller value afterwards clears the high field") {
        SyncProcessor::writeSdVsyncStart(2);
        CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 0);
        CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 2);
    }

    SUBCASE("the stop position is the same shape") {
        SyncProcessor::writeSdVsyncStop(520);
        CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 2);
        CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 8);

        SyncProcessor::writeSdVsyncStop(0);
        CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
        CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 0);
    }
}

// SP_EXT_SYNC_SEL: 0 takes H and V from the dedicated pins, 1 leaves the sync
// processor on composite or sync-on-green. It travels with the input choice, so
// it is set from an InputSource row rather than from the sync type.

template <typename Field>
static uint32_t afterExternalSync(uint8_t sel)
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    SyncProcessor::selectExternalSync(sel);
    return Field::read();
}

template <typename Field>
static bool externalSyncWrote(uint8_t sel)
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        SyncProcessor::selectExternalSync(sel);
        under[i] = Field::read();
    }
    return under[0] == under[1];
}

TEST_CASE("the external sync select carries the input's choice")
{
    CHECK(afterExternalSync<SyncProcessor::SP_EXT_SYNC_SEL>(0) == 0);
    CHECK(afterExternalSync<SyncProcessor::SP_EXT_SYNC_SEL>(1) == 1);
}

TEST_CASE("choosing the external sync touches nothing the sync type owns")
{
    CHECK(externalSyncWrote<SyncProcessor::SP_EXT_SYNC_SEL>(1));

    CHECK_FALSE(externalSyncWrote<SyncProcessor::SP_SOG_MODE>(1));
    CHECK_FALSE(externalSyncWrote<SyncProcessor::SP_PRE_COAST>(1));
    CHECK_FALSE(externalSyncWrote<SyncProcessor::SP_POST_COAST>(1));
    CHECK_FALSE(externalSyncWrote<SyncProcessor::SP_SOG_SRC_SEL>(1));
}
