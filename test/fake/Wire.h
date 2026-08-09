// A host-side stand-in for Arduino's Wire, backed by an in-memory model of a
// segmented I2C slave -- six banks of 256 registers behind a segment pointer at
// register 0xF0, which is how the TV5725 is addressed.
//
// It stores bytes and answers reads rather than recording calls, so WHERE a
// write landed is observable. A write arriving in the wrong bank is the fault it
// exists to pin, and no assertion about call sequences can see it.

#ifndef FAKE_WIRE_H_
#define FAKE_WIRE_H_

#include <stdint.h>
#include <stddef.h>

#include <vector>

class FakeTwoWire
{
public:
    static const uint8_t SegmentRegister = 0xF0;
    static const uint8_t Segments = 6;

    // Where a byte ended up: the bank the slave's pointer named at the time.
    uint8_t bank[Segments][256];

    // The slave's own pointer. Nothing outside the bus may cache this and
    // expect it to stay true -- that assumption is the defect under test.
    uint8_t segment;

    FakeTwoWire() { reset(); }

    void reset()
    {
        for (uint8_t s = 0; s < Segments; ++s)
            for (int r = 0; r < 256; ++r)
                bank[s][r] = 0;
        segment = 0;
        tx_.clear();
        rx_.clear();
        rxNext_ = 0;
    }

    void beginTransmission(uint8_t) { tx_.clear(); }

    size_t write(uint8_t value)
    {
        tx_.push_back(value);
        return 1;
    }

    size_t write(uint8_t const *values, size_t size)
    {
        for (size_t i = 0; i < size; ++i)
            tx_.push_back(values[i]);
        return size;
    }

    uint8_t endTransmission()
    {
        // A bare register offset with no payload is the address phase of a
        // read; the offset is remembered for the requestFrom that follows.
        if (tx_.size() == 1) {
            readOffset_ = tx_[0];
        } else if (tx_.size() > 1) {
            uint8_t offset = tx_[0];
            for (size_t i = 1; i < tx_.size(); ++i) {
                uint8_t reg = static_cast<uint8_t>(offset + (i - 1));
                if (reg == SegmentRegister)
                    segment = tx_[i] < Segments ? tx_[i] : segment;
                else
                    bank[segment][reg] = tx_[i];
            }
        }
        tx_.clear();
        return 0;
    }

    uint8_t requestFrom(uint8_t, uint8_t size, uint8_t)
    {
        rx_.clear();
        rxNext_ = 0;
        for (uint8_t i = 0; i < size; ++i) {
            uint8_t reg = static_cast<uint8_t>(readOffset_ + i);
            rx_.push_back(reg == SegmentRegister ? segment : bank[segment][reg]);
        }
        return size;
    }

    int available() const { return static_cast<int>(rx_.size() - rxNext_); }

    int read()
    {
        if (rxNext_ >= rx_.size())
            return -1;
        return rx_[rxNext_++];
    }

private:
    std::vector<uint8_t> tx_;
    std::vector<uint8_t> rx_;
    size_t rxNext_ = 0;
    uint8_t readOffset_ = 0;
};

extern FakeTwoWire Wire;

#endif
