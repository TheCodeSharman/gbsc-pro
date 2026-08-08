#include "IrReceiver.h"

IrReceiver::IrReceiver(uint16_t recvPin) : recv_(recvPin), decodes_(0) {}

void IrReceiver::enableIRIn() { recv_.enableIRIn(); }

bool IrReceiver::decode(decode_results *out)
{
    if (!recv_.decode(out))
        return false;
    ++decodes_;
    return true;
}

void IrReceiver::resume() { recv_.resume(); }

uint32_t IrReceiver::decodes() const { return decodes_; }
