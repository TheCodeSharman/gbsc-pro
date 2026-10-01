#ifndef FAKE_IRREMOTE_ESP8266_H_
#define FAKE_IRREMOTE_ESP8266_H_

// Enough of IRremoteESP8266 for a host build. The library reaches the ESP's
// timers and a GPIO, so the real one cannot be compiled here.

#include <stdint.h>

enum decode_type_t { UNKNOWN = -1, NEC = 3 };

struct decode_results {
    decode_type_t decode_type;
    uint64_t value;
    uint16_t bits;
};

#endif  // FAKE_IRREMOTE_ESP8266_H_
