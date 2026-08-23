#ifndef REGISTER_QUEUE_H_
#define REGISTER_QUEUE_H_

#include <stdint.h>

#include "FieldRequest.h"

// Register work handed from the web server's handlers to loop(), so the I2C bus
// has one owner. docs/register-bus-ownership.md
//
// Cancellation is the delicate part, not the queueing: loop() answers a parked
// AsyncWebServerRequest that the server may already have freed.
class RegisterQueue
{
public:
    enum Kind : uint8_t
    {
        None = 0,
        ReadOne,
        ReadRange,
        WriteOne,
        ReadFields,
    };

    struct Job
    {
        Kind kind = None;
        void *token = nullptr;
        uint8_t segment = 0;
        uint8_t first = 0;
        uint8_t last = 0;
        uint8_t value = 0;
    };

    // Four is enough for the three routes plus one in flight, and small enough
    // that a queue full of parked requests cannot eat the heap the console needs.
    static const uint8_t Capacity = 4;

    RegisterQueue();

    // Called from the web server's handlers. False means full, which the caller
    // turns into a status the client can retry on rather than a parked request
    // nobody ever answers.
    bool submit(const Job &job);

    // A field list, which does not fit in Job: 48 fields in each of four slots
    // is memory this part does not have. One list is held here and one request
    // may be outstanding, so the rule is the queue's rather than the route's.
    // False means full or already outstanding.
    bool submitFields(const FieldRequest &request, void *token);

    // The list the in-flight ReadFields job is for.
    const FieldRequest &fields() const;

    // Called from loop(). Takes the oldest waiting job and holds it in flight;
    // false if there is nothing to do or one is already in flight.
    bool claim(Job &out);

    // Whether the job in flight has been cancelled under us. Ask after the I2C
    // and before answering.
    bool claimCancelled() const;

    // Release the in-flight slot, answered or not.
    void complete();

    // Called from the disconnect callback. Drops a job that has not been claimed
    // yet, and marks the in-flight one so loop() knows not to answer it.
    void cancel(const void *token);

    uint8_t waiting() const;

private:
    FieldRequest fields_;
    bool fieldsOutstanding_;
    Job jobs_[Capacity];
    volatile bool pending_[Capacity];
    volatile uint8_t head_;
    volatile uint8_t count_;
    volatile bool inFlight_;
    volatile bool inFlightCancelled_;
    Kind inFlightKind_;
    void *inFlightToken_;
};

#endif
