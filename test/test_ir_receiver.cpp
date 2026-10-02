// Host-compiled unit tests for the IR receiver seam -- `make -C test
// ir-receiver`.
//
// A key can be injected rather than pressed, which is what lets a session drive
// the menu the remote drives. It has to behave exactly as a frame off the air
// does, because the chain reads `results.value` outside the decode block as well
// as inside it. docs/osd-menu.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/input/IrReceiver.h"

uint64_t IRrecv::frame = 0;
bool IRrecv::waiting = false;

static const uint32_t KeyMenu = 0xEA52609F;
static const uint32_t KeyDown = 0xEA5220DF;

TEST_CASE("an injected key stays on the receiver until resume, as an air one does")
{
    // IRrecv answers the same frame for as long as the state machine is stopped,
    // so decode() is not what consumes a frame -- resume() is. loop() reads that
    // way: a consumer that declines a key returns without resuming and leaves it
    // for the next one.
    IrReceiver receiver(0);
    decode_results results;
    IRrecv::waiting = false;

    receiver.inject(KeyMenu);

    REQUIRE(receiver.decode(&results));
    CHECK(results.value == KeyMenu);

    REQUIRE(receiver.decode(&results));
    CHECK(results.value == KeyMenu);
}

TEST_CASE("resuming clears it, so the next decode waits for a new one")
{
    IrReceiver receiver(0);
    decode_results results;

    receiver.inject(KeyMenu);
    REQUIRE(receiver.decode(&results));
    receiver.resume();

    CHECK_FALSE(receiver.decode(&results));

    receiver.inject(KeyDown);
    REQUIRE(receiver.decode(&results));
    CHECK(results.value == KeyDown);
}

TEST_CASE("an injected key counts as a frame taken")
{
    IrReceiver receiver(0);
    decode_results results;
    const uint32_t before = receiver.decodes();

    receiver.inject(KeyMenu);
    receiver.decode(&results);

    CHECK(receiver.decodes() == before + 1);
}

TEST_CASE("injecting again replaces a key nothing took")
{
    // Nothing is listening when the menu is closed and the key is not one it
    // answers, so an untaken key must not arrive later out of order.
    IrReceiver receiver(0);
    decode_results results;

    receiver.inject(KeyMenu);
    receiver.inject(KeyDown);

    REQUIRE(receiver.decode(&results));
    CHECK(results.value == KeyDown);
}

TEST_CASE("a frame off the air still arrives while nothing is injected")
{
    IrReceiver receiver(0);
    decode_results results;

    IRrecv::frame = KeyMenu;
    IRrecv::waiting = true;

    REQUIRE(receiver.decode(&results));
    CHECK(results.value == KeyMenu);
}

TEST_CASE("the last of loop()'s consumers answers an injected key the first declined")
{
    // loop() decodes three times a pass: the described menu, the overlay
    // branches, then OSD_IR(). Whichever is on the screen owns the remote, so
    // the menu returns WITHOUT resuming while an overlay is up. An injected key
    // that stopped answering after the first decode reached no consumer at all,
    // and left nothing to resume it -- after which decode() answered out of the
    // injected key before ever asking the receiver, and the handset was dead for
    // the life of the boot.
    IrReceiver receiver(0);
    decode_results results;
    IRrecv::waiting = false;

    receiver.inject(KeyMenu);

    REQUIRE(receiver.decode(&results));   // the described menu declines
    REQUIRE(receiver.decode(&results));   // no overlay branch matches
    REQUIRE(receiver.decode(&results));   // OSD_IR() answers it
    CHECK(results.value == KeyMenu);
    receiver.resume();

    IRrecv::frame = KeyDown;
    IRrecv::waiting = true;

    REQUIRE(receiver.decode(&results));
    CHECK(results.value == KeyDown);
}
