#include "WriteTrace.h"

#include <stdio.h>

#include "Tv5725.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace {

void pause(uint16_t ms)
{
#ifdef ARDUINO
    delay(ms);
#else
    (void)ms;
#endif
}

}  // namespace

namespace Tv5725 {

const uint16_t WriteTrace::Capacity;
const uint8_t WriteTrace::LineBytes;
const uint8_t WriteTrace::PayloadMax;

WriteTrace::Entry WriteTrace::ring_[WriteTrace::Capacity];
uint16_t WriteTrace::held_ = 0;
uint32_t WriteTrace::seen_ = 0;
uint32_t WriteTrace::armedMs_ = 0;
uint8_t WriteTrace::segment_ = 0;
bool WriteTrace::recording_ = false;

void WriteTrace::arm(uint32_t nowMs)
{
    held_ = 0;
    seen_ = 0;
    armedMs_ = nowMs;
    segment_ = 0;
    recording_ = true;
}

void WriteTrace::stop()
{
    recording_ = false;
}

bool WriteTrace::recording()
{
    return recording_;
}

void WriteTrace::record(uint32_t nowMs, uint8_t reg, const uint8_t *data, uint8_t length)
{
    if (!recording_) return;

    if (reg == detail::TVAttrs::SegByteOffset) {
        if (length > 0) segment_ = data[0];
        return;
    }

    ++seen_;
    if (held_ == Capacity) return;

    Entry &entry = ring_[held_++];
    entry.segment = segment_;
    uint32_t since = nowMs - armedMs_;
    entry.sinceArmMs = since > 0xFFFF ? 0xFFFF : (uint16_t)since;
    entry.reg = reg;
    entry.length = length;
    for (uint8_t index = 0; index < PayloadMax; ++index)
        entry.data[index] = index < length ? data[index] : 0;
}

uint16_t WriteTrace::count()
{
    return held_;
}

const WriteTrace::Entry &WriteTrace::at(uint16_t index)
{
    return ring_[index];
}

uint32_t WriteTrace::seen()
{
    return seen_;
}

bool WriteTrace::overflowed()
{
    return seen_ > held_;
}

void WriteTrace::line(char *into, uint16_t index)
{
    const Entry &entry = ring_[index];
    char payload[2 * PayloadMax + 1];
    for (uint8_t byte = 0; byte < PayloadMax; ++byte) {
        const bool carried = !entry.truncated() && byte < entry.length;
        if (carried)
            snprintf(payload + 2 * byte, 3, "%02X", entry.data[byte]);
        else {
            payload[2 * byte] = '.';
            payload[2 * byte + 1] = '.';
        }
    }
    payload[2 * PayloadMax] = '\0';

    snprintf(into, LineBytes + 1, "%1u,%03u,%05u,%02X,%02u,%s\n",
             (unsigned)(entry.segment % 10), (unsigned)(index % 1000),
             (unsigned)entry.sinceArmMs,
             (unsigned)entry.reg, (unsigned)(entry.length % 100), payload);
}

void WriteTrace::replay(uint16_t first, uint16_t last, bool gaps)
{
    if (last > held_) last = held_;
    if (first >= last) return;

    const bool was = recording_;
    recording_ = false;

    uint8_t aimed = 0xFF;
    uint16_t previous = ring_[first].sinceArmMs;
    for (uint16_t index = first; index < last; ++index) {
        const Entry &entry = ring_[index];
        if (entry.truncated()) continue;
        if (entry.segment != aimed) {
            aimed = entry.segment;
            tw::detail::rawWrite(GBS_ADDR, detail::TVAttrs::SegByteOffset, &aimed, 1);
        }
        if (gaps && entry.sinceArmMs > previous)
            pause(entry.sinceArmMs - previous);
        previous = entry.sinceArmMs;
        tw::detail::rawWrite(GBS_ADDR, entry.reg, entry.data, entry.length);
    }

    recording_ = was;
}

}  // namespace Tv5725
