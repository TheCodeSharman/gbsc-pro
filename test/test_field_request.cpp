// Host-compiled unit tests for src/net/FieldRequest.h -- `make -C test field-request`.
//
// One HTTP request naming many fields, instead of one request per field. Every
// register read is queued into loop(), so a host reading 608 of them one at a
// time both takes minutes and starves the loop it is measuring -- which makes
// the transient it was looking for unobservable.
//
// The wire form is addresses in decimal, not names: the firmware has no runtime name table
// and shipping 956 of them would cost flash and RAM on a part already at 78%
// and 57%. The catalogue that resolves names lives on the host.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "../GBSC-Pro-Source code/gbs-control/src/net/FieldRequest.h"

TEST_CASE("one field, as segment.register.offset.width")
{
    FieldRequest request;

    REQUIRE(request.parse("0.27.0.11"));
    REQUIRE(request.count() == 1);
    CHECK(request.at(0).segment == 0x00);
    CHECK(request.at(0).reg == 27);
    CHECK(request.at(0).offset == 0);
    CHECK(request.at(0).width == 11);
}

TEST_CASE("many fields, comma separated, in the order asked for")
{
    FieldRequest request;

    REQUIRE(request.parse("0.27.0.11,5.18.0.12,4.33.0.1"));
    REQUIRE(request.count() == 3);
    CHECK(request.at(1).segment == 5);
    CHECK(request.at(1).reg == 18);
    CHECK(request.at(2).reg == 33);
    CHECK(request.at(2).width == 1);
}

TEST_CASE("a spec that cannot be read is refused rather than half-parsed")
{
    FieldRequest request;

    SUBCASE("empty") { CHECK_FALSE(request.parse("")); }
    SUBCASE("too few parts") { CHECK_FALSE(request.parse("0.27.0")); }
    SUBCASE("a segment the chip does not have") { CHECK_FALSE(request.parse("9.27.0.11")); }
    SUBCASE("a width of zero reads nothing") { CHECK_FALSE(request.parse("0.27.0.0")); }
    SUBCASE("a width past the widest field the chip has") {
        CHECK_FALSE(request.parse("0.27.0.33"));
    }
    SUBCASE("an offset past a byte") { CHECK_FALSE(request.parse("0.27.8.1")); }
    SUBCASE("rubbish") { CHECK_FALSE(request.parse("nonsense")); }

    CHECK(request.count() == 0);
}

TEST_CASE("the list is bounded, because the buffer is static")
{
    // A request longer than the buffer is refused whole. Truncating it would
    // answer with values the caller cannot match to what it asked for.
    std::string spec = "0.27.0.11";
    for (uint8_t i = 0; i < FieldRequest::Capacity; ++i)
        spec += ",0.27.0.11";

    FieldRequest request;
    CHECK_FALSE(request.parse(spec.c_str()));
}

TEST_CASE("a full buffer is accepted")
{
    std::string spec = "0.27.0.11";
    for (uint8_t i = 1; i < FieldRequest::Capacity; ++i)
        spec += ",0.27.0.11";

    FieldRequest request;
    REQUIRE(request.parse(spec.c_str()));
    CHECK(request.count() == FieldRequest::Capacity);
}

TEST_CASE("a field is assembled from the bytes it spans, little end first")
{
    // STATUS_SYNC_PROC_VTOTAL is eleven bits at s0_1b, so it takes all of 0x1b
    // and three bits of 0x1c. 311 is 0x137.
    FieldRequest request;
    REQUIRE(request.parse("0.27.0.11"));

    const uint8_t bytes[2] = {0x37, 0x01};
    CHECK(FieldRequest::valueFrom(request.at(0), bytes) == 311);

    SUBCASE("bits above the field are masked off") {
        const uint8_t noisy[2] = {0x37, 0xF9};
        CHECK(FieldRequest::valueFrom(request.at(0), noisy) == 311);
    }

    SUBCASE("an offset shifts the low byte down") {
        REQUIRE(request.parse("3.2.4.11"));   // VDS_VSYNC_RST
        const uint8_t high[2] = {0x40, 0x46};
        CHECK(FieldRequest::valueFrom(request.at(0), high) == 1124);
    }
}

TEST_CASE("how many bytes a field needs is what the reader must fetch")
{
    FieldRequest request;

    REQUIRE(request.parse("4.33.0.1"));
    CHECK(FieldRequest::bytesFor(request.at(0)) == 1);

    REQUIRE(request.parse("0.27.0.11"));
    CHECK(FieldRequest::bytesFor(request.at(0)) == 2);

    REQUIRE(request.parse("5.22.4.2"));
    CHECK(FieldRequest::bytesFor(request.at(0)) == 1);

    REQUIRE(request.parse("3.2.4.11"));
    CHECK(FieldRequest::bytesFor(request.at(0)) == 2);
}

TEST_CASE("the wide address fields span more than two registers")
{
    // MaxWidth was 16 and bytesFor() capped at two registers, on the premise
    // that no field is wider. The catalogue refutes it: fourteen fields span
    // three registers or more, and VDS_FR_SELECT is 32 bits over four. Nine of
    // them are the 21-bit SDRAM address fields -- the same wide fields the
    // datasheet's line wrapping loses, so they are easy to believe absent.
    //
    // The danger is one-directional: a field read narrower than it is comes
    // back truncated and says nothing.
    FieldRequest request;

    SUBCASE("21 bits over three, as the SDRAM address fields are") {
        // WFF_SAFE_GUARD_A, s4_0x44[0+21].
        REQUIRE(request.parse("4.68.0.21"));
        CHECK(FieldRequest::bytesFor(request.at(0)) == 3);

        const uint8_t bytes[3] = {0x34, 0x12, 0x15};
        CHECK(FieldRequest::valueFrom(request.at(0), bytes) == 0x151234u);
    }

    SUBCASE("32 bits over four, as VDS_FR_SELECT is") {
        // s3_0x1b[0+32].
        REQUIRE(request.parse("3.27.0.32"));
        CHECK(FieldRequest::bytesFor(request.at(0)) == 4);

        const uint8_t bytes[4] = {0x78, 0x56, 0x34, 0x12};
        CHECK(FieldRequest::valueFrom(request.at(0), bytes) == 0x12345678u);
    }

    SUBCASE("10 bits at offset 7 still needs three, because the offset pushes it") {
        // VDS_C2_TAG_HIGH_SLOPE, s3_0x67[7+10]: bits 7..16.
        REQUIRE(request.parse("3.103.7.10"));
        CHECK(FieldRequest::bytesFor(request.at(0)) == 3);

        const uint8_t bytes[3] = {0x80, 0xFF, 0x01};
        CHECK(FieldRequest::valueFrom(request.at(0), bytes) == 0x3FFu);
    }

    SUBCASE("and a width past 32 is still refused") {
        CHECK_FALSE(request.parse("3.27.0.33"));
        CHECK_FALSE(request.parse("3.27.7.32"));
    }
}
