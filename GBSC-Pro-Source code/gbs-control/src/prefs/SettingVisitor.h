#ifndef PREFS_SETTING_VISITOR_H_
#define PREFS_SETTING_VISITOR_H_

// One pass over the settings, so each one's name, bound and default are stated
// once. Prefs::Settings::each() offers every setting to a visitor and the pass
// decides what happens: the default taken, a line's value applied, or a line
// rendered.
//
// Stating them three times is what the file this replaces did, and the two
// defaults drifted apart: a fresh unit took avOptions' initialiser while a file
// missing a field took the load path's clamp, and the two disagreed.
// docs/preferences-file.md

#include <stdint.h>

namespace Prefs {

class SettingVisitor {
public:
    static SettingVisitor defaulting();
    static SettingVisitor reading(const char *key, const char *value);
    static SettingVisitor writing(uint16_t index, char *out, uint8_t size);

    // A whole number, written in decimal. Anything past `max` or not a number
    // at all reads as `fallback`.
    void number(const char *key, uint8_t &at, uint8_t max, uint8_t fallback);

    // One character, written as itself.
    void character(const char *key, uint8_t &at, uint8_t fallback);

    // A NUL-terminated name, up to `size` including the terminator.
    void text(const char *key, char *at, uint8_t size, const char *fallback);

    // Whether the pass found what it was for: the key a read named, or the
    // setting a write wanted.
    bool found() const;

private:
    enum Pass { Default, Read, Write };

    explicit SettingVisitor(Pass pass);

    // Whether this setting is the one the pass wants, counting it either way.
    bool wanted(const char *key);

    // `key = value` into the caller's buffer, false when it would not fit.
    bool render(const char *key, const char *value);

    static uint8_t numberFrom(const char *value, uint8_t max, uint8_t fallback);

    Pass pass_;
    const char *key_;
    const char *value_;
    uint16_t index_;
    uint16_t seen_;
    char *out_;
    uint8_t size_;
    bool found_;
};

}  // namespace Prefs

#endif  // PREFS_SETTING_VISITOR_H_
