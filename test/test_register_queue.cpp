// Host-compiled unit tests for RegisterQueue -- `make -C test register-queue`.
//
// /getreg, /getregs and /setreg are the only things touching the TV5725 from
// outside loop(), and measured 2026-08-09 they are the whole of the concurrency:
// idle routes gave zero re-entries over 30 s of OSD and sync watcher activity,
// and /getregs saturated the counter.
//
// **THE DELICATE PART IS LIFETIME, NOT QUEUEING.** The handler parks the request
// object and loop() answers it later, so a client disconnecting in between has
// the async side delete it while loop() may be holding it. Cancellation is
// therefore most of what these tests are about.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/net/RegisterQueue.h"
#include "../GBSC-Pro-Source code/gbs-control/src/net/FieldRequest.h"

// Stand-ins for AsyncWebServerRequest*. The queue never dereferences a token --
// that is the point of it being opaque -- so any distinct addresses will do.
static int requestA, requestB, requestC;
static void *const TokenA = &requestA;
static void *const TokenB = &requestB;
static void *const TokenC = &requestC;

static RegisterQueue::Job readOne(void *token, uint8_t segment, uint8_t reg)
{
    RegisterQueue::Job job;
    job.kind = RegisterQueue::ReadOne;
    job.token = token;
    job.segment = segment;
    job.first = reg;
    return job;
}

TEST_CASE("a submitted job comes back to whoever drains the queue")
{
    RegisterQueue queue;
    RegisterQueue::Job taken;

    CHECK(queue.submit(readOne(TokenA, 1, 0x10)));
    REQUIRE(queue.claim(taken));

    CHECK(taken.kind == RegisterQueue::ReadOne);
    CHECK(taken.token == TokenA);
    CHECK(taken.segment == 1);
    CHECK(taken.first == 0x10);
}

TEST_CASE("an empty queue has nothing to claim")
{
    RegisterQueue queue;
    RegisterQueue::Job taken;

    CHECK_FALSE(queue.claim(taken));
}

TEST_CASE("jobs are answered in the order they arrived")
{
    RegisterQueue queue;
    RegisterQueue::Job taken;

    queue.submit(readOne(TokenA, 1, 0x10));
    queue.submit(readOne(TokenB, 3, 0x11));

    REQUIRE(queue.claim(taken));
    CHECK(taken.token == TokenA);
    queue.complete();

    REQUIRE(queue.claim(taken));
    CHECK(taken.token == TokenB);
}

TEST_CASE("a full queue refuses work rather than dropping it silently")
{
    // The handler turns a refusal into an HTTP status the caller can retry on. A
    // queue that overwrote its oldest entry would instead leave a parked request
    // that loop() never answers, and the client would hang until it timed out.
    RegisterQueue queue;

    for (uint8_t i = 0; i < RegisterQueue::Capacity; ++i) {
        CHECK(queue.submit(readOne(TokenA, 1, i)));
    }

    CHECK_FALSE(queue.submit(readOne(TokenB, 1, 0x10)));
}

TEST_CASE("a cancelled job is never handed out")
{
    // The client disconnected before loop() got to it. Its request object is
    // gone, so answering it would write into freed memory.
    RegisterQueue queue;
    RegisterQueue::Job taken;

    queue.submit(readOne(TokenA, 1, 0x10));
    queue.submit(readOne(TokenB, 3, 0x11));
    queue.cancel(TokenA);

    REQUIRE(queue.claim(taken));
    CHECK(taken.token == TokenB);
}

TEST_CASE("cancelling the job already in flight is visible before it is answered")
{
    // The dangerous ordering, and the reason claim() and complete() are separate
    // calls: loop() has taken the job and is in the middle of the I2C when the
    // disconnect arrives. It must be able to ask, after the transfer and before
    // it replies, whether the thing it is about to reply to still exists.
    RegisterQueue queue;
    RegisterQueue::Job taken;

    queue.submit(readOne(TokenA, 1, 0x10));
    REQUIRE(queue.claim(taken));
    CHECK_FALSE(queue.claimCancelled());

    queue.cancel(TokenA);

    CHECK(queue.claimCancelled());
}

TEST_CASE("cancelling somebody else's job does not touch the one in flight")
{
    RegisterQueue queue;
    RegisterQueue::Job taken;

    queue.submit(readOne(TokenA, 1, 0x10));
    queue.submit(readOne(TokenB, 3, 0x11));
    REQUIRE(queue.claim(taken));

    queue.cancel(TokenB);

    CHECK_FALSE(queue.claimCancelled());
}

TEST_CASE("completing a job frees its slot for reuse")
{
    RegisterQueue queue;
    RegisterQueue::Job taken;

    for (uint8_t i = 0; i < RegisterQueue::Capacity; ++i) {
        queue.submit(readOne(TokenA, 1, i));
    }
    REQUIRE(queue.claim(taken));
    queue.complete();

    CHECK(queue.submit(readOne(TokenC, 1, 0x10)));
}

TEST_CASE("a job cannot be claimed while another is in flight")
{
    // One bus, one owner. Handing out a second job before the first is answered
    // is the very interleaving this queue exists to prevent.
    RegisterQueue queue;
    RegisterQueue::Job first, second;

    queue.submit(readOne(TokenA, 1, 0x10));
    queue.submit(readOne(TokenB, 3, 0x11));

    REQUIRE(queue.claim(first));
    CHECK_FALSE(queue.claim(second));
}

TEST_CASE("a write job carries its value")
{
    RegisterQueue queue;
    RegisterQueue::Job job, taken;
    job.kind = RegisterQueue::WriteOne;
    job.token = TokenA;
    job.segment = 3;
    job.first = 0x10;
    job.value = 0x44;

    queue.submit(job);
    REQUIRE(queue.claim(taken));

    CHECK(taken.kind == RegisterQueue::WriteOne);
    CHECK(taken.value == 0x44);
}

TEST_CASE("a range job carries both ends")
{
    RegisterQueue queue;
    RegisterQueue::Job job, taken;
    job.kind = RegisterQueue::ReadRange;
    job.token = TokenA;
    job.segment = 3;
    job.first = 0x00;
    job.last = 0xFF;

    queue.submit(job);
    REQUIRE(queue.claim(taken));

    CHECK(taken.first == 0x00);
    CHECK(taken.last == 0xFF);
}

// --- many fields in one job --------------------------------------------------

static FieldRequest twoFields()
{
    FieldRequest request;
    REQUIRE(request.parse("5.18.0.12,0.27.0.11"));
    return request;
}

TEST_CASE("a field request is claimed like any other job, and carries its list")
{
    // The list cannot live in Job: a 48-field buffer in each of four slots is
    // memory this part does not have. It lives here instead, which is also what
    // makes "one outstanding" enforceable rather than a rule the route can
    // forget.
    RegisterQueue queue;
    RegisterQueue::Job taken;

    CHECK(queue.submitFields(twoFields(), TokenA));
    REQUIRE(queue.claim(taken));

    CHECK(taken.kind == RegisterQueue::ReadFields);
    CHECK(taken.token == TokenA);
    REQUIRE(queue.fields().count() == 2);
    CHECK(queue.fields().at(0).segment == 5);
    // The spec is decimal throughout, so this is PLLAD_MD at s5_0x12.
    CHECK(queue.fields().at(0).reg == 18);
    CHECK(queue.fields().at(1).width == 11);
}

TEST_CASE("a second field request is refused while the first is outstanding")
{
    // One buffer, so a second would overwrite the list the first is still
    // waiting on and answer it with somebody else's fields.
    RegisterQueue queue;

    CHECK(queue.submitFields(twoFields(), TokenA));
    CHECK_FALSE(queue.submitFields(twoFields(), TokenB));

    SUBCASE("and still refused once it is in flight") {
        RegisterQueue::Job taken;
        REQUIRE(queue.claim(taken));
        CHECK_FALSE(queue.submitFields(twoFields(), TokenB));
    }

    SUBCASE("completing it lets the next one in") {
        RegisterQueue::Job taken;
        REQUIRE(queue.claim(taken));
        queue.complete();
        CHECK(queue.submitFields(twoFields(), TokenB));
    }
}

TEST_CASE("cancelling a waiting field request frees the buffer")
{
    // The client disconnected before loop() ever saw it, so nothing will
    // complete() and the buffer would otherwise be held for good.
    RegisterQueue queue;
    RegisterQueue::Job taken;

    CHECK(queue.submitFields(twoFields(), TokenA));
    queue.cancel(TokenA);

    CHECK(queue.submitFields(twoFields(), TokenB));
    REQUIRE(queue.claim(taken));
    CHECK(taken.token == TokenB);
}
