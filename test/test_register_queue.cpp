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
