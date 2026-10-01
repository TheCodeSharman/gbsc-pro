#include "IrReceiver.h"

IrReceiver::IrReceiver(uint16_t recvPin)
    : recv_(recvPin), decodes_(0), injected_(0), hasInjected_(false),
      injectedTaken_(false)
{
}

void IrReceiver::enableIRIn() { recv_.enableIRIn(); }

bool IrReceiver::decode(decode_results *out)
{
    if (hasInjected_) {
        if (injectedTaken_)
            return false;
        injectedTaken_ = true;
        out->decode_type = NEC;
        out->value = injected_;
        out->bits = 32;
        ++decodes_;
        return true;
    }

    if (!recv_.decode(out))
        return false;
    ++decodes_;
    return true;
}

void IrReceiver::resume()
{
    hasInjected_ = false;
    injectedTaken_ = false;
    recv_.resume();
}

// hasInjected_ last: a request arrives from a network callback and loop() may
// decode between any two of these, so the flag that admits the key is set once
// the key and its taken mark are both in place.
void IrReceiver::inject(uint32_t value)
{
    injected_ = value;
    injectedTaken_ = false;
    hasInjected_ = true;
}

uint32_t IrReceiver::decodes() const { return decodes_; }
