#include "SettingVisitor.h"

#include <stddef.h>

namespace Prefs {

namespace {

bool sameText(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

void decimal(uint8_t value, char *out)
{
    char digits[3];
    uint8_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0);

    for (uint8_t i = 0; i < count; ++i)
        out[i] = digits[count - 1 - i];
    out[count] = '\0';
}

}  // namespace

SettingVisitor::SettingVisitor(Pass pass)
    : pass_(pass), key_(NULL), value_(NULL), index_(0), seen_(0), out_(NULL),
      size_(0), found_(false)
{
}

SettingVisitor SettingVisitor::defaulting() { return SettingVisitor(Default); }

SettingVisitor SettingVisitor::reading(const char *key, const char *value)
{
    SettingVisitor visit(Read);
    visit.key_ = key;
    visit.value_ = value;
    return visit;
}

SettingVisitor SettingVisitor::writing(uint16_t index, char *out, uint8_t size)
{
    SettingVisitor visit(Write);
    visit.index_ = index;
    visit.out_ = out;
    visit.size_ = size;
    return visit;
}

bool SettingVisitor::found() const { return found_; }

bool SettingVisitor::wanted(const char *key)
{
    const uint16_t at = seen_++;
    if (found_)
        return false;

    switch (pass_) {
        case Default:
            return true;
        case Read:
            return sameText(key_, key);
        case Write:
            return at == index_;
    }
    return false;
}

bool SettingVisitor::render(const char *key, const char *value)
{
    uint8_t at = 0;
    const char *const parts[3] = {key, " = ", value};
    for (uint8_t part = 0; part < 3; ++part)
        for (const char *from = parts[part]; *from; ++from) {
            if (at + 1 >= size_)
                return false;
            out_[at++] = *from;
        }

    out_[at] = '\0';
    return true;
}

uint8_t SettingVisitor::numberFrom(const char *value, uint8_t max,
                                   uint8_t fallback)
{
    uint16_t read = 0;
    uint8_t digits = 0;
    for (; *value >= '0' && *value <= '9'; ++value, ++digits) {
        read = read * 10 + (uint8_t)(*value - '0');
        if (read > max)
            return fallback;
    }

    return (digits > 0 && *value == '\0') ? (uint8_t)read : fallback;
}

void SettingVisitor::number(const char *key, uint8_t &at, uint8_t max,
                            uint8_t fallback)
{
    if (!wanted(key))
        return;

    char value[4];
    switch (pass_) {
        case Default:
            at = fallback;
            return;
        case Read:
            at = numberFrom(value_, max, fallback);
            found_ = true;
            return;
        case Write:
            decimal(at, value);
            found_ = render(key, value);
            return;
    }
}

void SettingVisitor::character(const char *key, uint8_t &at, uint8_t fallback)
{
    if (!wanted(key))
        return;

    char value[2];
    switch (pass_) {
        case Default:
            at = fallback;
            return;
        case Read:
            at = value_[0] != '\0' ? (uint8_t)value_[0] : fallback;
            found_ = true;
            return;
        case Write:
            value[0] = (char)at;
            value[1] = '\0';
            found_ = render(key, value);
            return;
    }
}

void SettingVisitor::text(const char *key, char *at, uint8_t size,
                          const char *fallback)
{
    if (!wanted(key))
        return;

    if (pass_ == Write) {
        found_ = render(key, at);
        return;
    }

    const char *from = fallback;
    if (pass_ == Read) {
        found_ = true;
        if (value_[0] != '\0')
            from = value_;
    }

    uint8_t into = 0;
    for (; from[into] != '\0' && into + 1 < size; ++into)
        at[into] = from[into];
    at[into] = '\0';
}

}  // namespace Prefs
