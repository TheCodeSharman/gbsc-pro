#include "RegisterQueue.h"

RegisterQueue::RegisterQueue()
    : head_(0), count_(0), inFlight_(false), inFlightCancelled_(false),
      inFlightToken_(nullptr)
{
    for (uint8_t i = 0; i < Capacity; ++i) {
        pending_[i] = false;
    }
}

bool RegisterQueue::submit(const Job &job)
{
    if (count_ >= Capacity) {
        return false;
    }

    uint8_t slot = (head_ + count_) % Capacity;
    jobs_[slot] = job;
    pending_[slot] = true;
    count_++;
    return true;
}

bool RegisterQueue::claim(Job &out)
{
    // One bus, one owner. Handing out a second job while the first is unanswered
    // is the interleaving this queue exists to prevent.
    if (inFlight_) {
        return false;
    }

    while (count_ > 0) {
        uint8_t slot = head_;
        head_ = (head_ + 1) % Capacity;
        count_--;

        // Cancelled while it waited: its request object is gone, so there is
        // nothing to answer and nothing to do.
        if (!pending_[slot]) {
            continue;
        }
        pending_[slot] = false;

        out = jobs_[slot];
        inFlight_ = true;
        inFlightCancelled_ = false;
        inFlightToken_ = jobs_[slot].token;
        return true;
    }

    return false;
}

bool RegisterQueue::claimCancelled() const
{
    return inFlightCancelled_;
}

void RegisterQueue::complete()
{
    inFlight_ = false;
    inFlightCancelled_ = false;
    inFlightToken_ = nullptr;
}

void RegisterQueue::cancel(const void *token)
{
    if (inFlight_ && inFlightToken_ == token) {
        inFlightCancelled_ = true;
    }

    for (uint8_t i = 0; i < count_; ++i) {
        uint8_t slot = (head_ + i) % Capacity;
        if (pending_[slot] && jobs_[slot].token == token) {
            pending_[slot] = false;
        }
    }
}

uint8_t RegisterQueue::waiting() const
{
    return count_;
}
