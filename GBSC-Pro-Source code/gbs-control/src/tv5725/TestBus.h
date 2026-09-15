#ifndef TV5725_TEST_BUS_H_
#define TV5725_TEST_BUS_H_

#include <stdint.h>

#include "Tv5725.h"

namespace Tv5725 {

// Drives one chosen signal out of the chip on the debug pin, for something
// outside to count or read.
//
// The pin is shared -- the frame time lock, auto gain, the sync separator and
// every rate measurement select on it in turn -- so the registers are private
// and the selection is always a named call. A reader selects what it wants
// immediately before reading, because no selection survives another caller.
class TestBus {
public:
    static const uint8_t InputVsync = 0x0;
    static const uint8_t OutputVsync = 0x2;    // the VDS bus
    static const uint8_t SyncProcessor = 0xa;

    // The widest selector the five-bit field carries.
    static const uint8_t SignalMax = 31;

    // Drive this signal out. Enables the bus with it: a selection nothing is
    // driving is not a selection, and leaving that to the caller is what makes
    // a measurement silently read 0.
    static void select(uint8_t signal);

    // What is selected, and whether it is driven. For a caller that has to put
    // back what it found -- a sweep across every selector, say.
    static uint8_t selected();
    static bool enabled();
    static void enable(bool on);

    // The bus's value, for a signal read rather than counted. Counting edges
    // is the ESP's job, off the pin. readHigh() is the top byte of the same
    // 16 bits, which is all some callers look at.
    static uint16_t read();
    static uint8_t readHigh();

private:
    typedef UReg<0x00, 0x4D, 0, 5> TEST_BUS_SEL;
    typedef UReg<0x00, 0x4D, 5, 1> TEST_BUS_EN;
    typedef UReg<0x00, 0x2E, 0, 16> TEST_BUS;
    typedef UReg<0x00, 0x2F, 0, 8> TEST_BUS_2F;
};

}  // namespace Tv5725

#endif  // TV5725_TEST_BUS_H_
