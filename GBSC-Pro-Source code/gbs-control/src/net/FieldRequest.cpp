#include "FieldRequest.h"

namespace {

// Decimal, which is what the host's catalogue stores every part of a field
// address as. One radix throughout: a spec mixing hex registers with decimal
// widths reads a width of 11 as seventeen bits.
//
// False on an empty run or a character that is not a digit, which is what makes
// "nonsense" a refusal rather than a zero.
bool takeNumber(const char *&at, uint16_t &out)
{
    uint32_t value = 0;
    uint8_t digits = 0;
    for (; *at >= '0' && *at <= '9'; ++at) {
        if (++digits > 5)
            return false;
        value = value * 10u + (uint32_t)(*at - '0');
        if (value > 0xFFFFu)
            return false;
    }
    if (digits == 0)
        return false;
    out = (uint16_t)value;
    return true;
}

}  // namespace

const uint8_t FieldRequest::Capacity;
const uint8_t FieldRequest::MaxWidth;
const uint8_t FieldRequest::MaxRegisters;

FieldRequest::FieldRequest() : count_(0) {}

uint8_t FieldRequest::count() const { return count_; }

const FieldRequest::Field &FieldRequest::at(uint8_t index) const { return fields_[index]; }

bool FieldRequest::parse(const char *spec)
{
    count_ = 0;
    if (spec == nullptr || *spec == '\0')
        return false;

    const char *at = spec;
    for (;;) {
        uint16_t segment, reg, offset, width;
        if (!takeNumber(at, segment) || *at++ != '.')
            break;
        if (!takeNumber(at, reg) || *at++ != '.')
            break;
        if (!takeNumber(at, offset) || *at++ != '.')
            break;
        if (!takeNumber(at, width))
            break;

        if (segment > 5 || reg > 0xFF || offset > 7 || width == 0 ||
            offset + width > MaxWidth)
            break;
        if (count_ >= Capacity)
            break;

        Field &field = fields_[count_++];
        field.segment = (uint8_t)segment;
        field.reg = (uint8_t)reg;
        field.offset = (uint8_t)offset;
        field.width = (uint8_t)width;

        if (*at == '\0')
            return true;
        if (*at++ != ',')
            break;
    }

    count_ = 0;
    return false;
}

uint8_t FieldRequest::bytesFor(const Field &field)
{
    return (uint8_t)((field.offset + field.width + 7u) / 8u);
}

uint32_t FieldRequest::valueFrom(const Field &field, const uint8_t *bytes)
{
    // Shift before combining: a 32-bit field at offset 0 fills the word, so
    // assembling the span first and shifting after would drop the top byte.
    uint32_t raw = 0;
    const uint8_t span = bytesFor(field);
    for (uint8_t i = 0; i < span; ++i)
        raw |= (uint32_t)bytes[i] << (8u * i);
    raw >>= field.offset;
    const uint32_t mask = (field.width >= 32) ? 0xFFFFFFFFu
                                             : ((1ul << field.width) - 1ul);
    return raw & mask;
}
