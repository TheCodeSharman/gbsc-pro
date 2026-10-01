#ifndef FAKE_IRRECV_H_
#define FAKE_IRRECV_H_

// A receiver standing in for the one on kRecvPin. Only what IrReceiver calls.
// What is waiting is static, because IrReceiver owns its IRrecv by value and a
// case has no other way to say a frame arrived off the air.

#include <stdint.h>

#include "IRremoteESP8266.h"

class IRrecv {
public:
    explicit IRrecv(uint16_t) : enabled_(false) {}

    void enableIRIn() { enabled_ = true; }

    bool decode(decode_results *out)
    {
        if (!waiting)
            return false;
        out->decode_type = NEC;
        out->value = frame;
        out->bits = 32;
        return true;
    }

    void resume() { waiting = false; }

    bool enabled() const { return enabled_; }

    // A frame off the air.
    static uint64_t frame;
    static bool waiting;

private:
    bool enabled_;
};

#endif  // FAKE_IRRECV_H_
