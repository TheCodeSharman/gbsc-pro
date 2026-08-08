#ifndef INPUT_IR_RECEIVER_H_
#define INPUT_IR_RECEIVER_H_

// The IR receiver: IRrecv, plus a count of the frames taken off it.

#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <stdint.h>

class IrReceiver {
public:
    explicit IrReceiver(uint16_t recvPin);

    void enableIRIn();

    bool decode(decode_results *out);

    void resume();

    // Frames taken off the receiver since boot. Only ever increases, so a
    // difference across a call is how many that call consumed.
    uint32_t decodes() const;

private:
    IRrecv recv_;
    uint32_t decodes_;
};

#endif  // INPUT_IR_RECEIVER_H_
