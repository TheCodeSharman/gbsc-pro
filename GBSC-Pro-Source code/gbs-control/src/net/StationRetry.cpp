#include "StationRetry.h"

namespace Net {

const uint32_t StationRetry::RetryIntervalMs;

StationRetry::StationRetry() : armed_(false), lastAttemptMs_(0) {}

void StationRetry::arm(uint32_t nowMs)
{
    armed_ = true;
    lastAttemptMs_ = nowMs;
}

void StationRetry::disarm() { armed_ = false; }

bool StationRetry::armed() const { return armed_; }

bool StationRetry::due(uint32_t nowMs) const
{
    if (!armed_)
        return false;
    return (int32_t)(nowMs - lastAttemptMs_) >= (int32_t)RetryIntervalMs;
}

void StationRetry::attempted(uint32_t nowMs) { lastAttemptMs_ = nowMs; }

} // namespace Net
