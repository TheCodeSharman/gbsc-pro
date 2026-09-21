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
    static const uint8_t SyncProcessor = 0xa;

    // The widest selector the five-bit field carries.
    static const uint8_t SignalMax = 31;

    // The source's vertical, off the input formatter's test output, and the
    // output's off the VDS's. Each enables the block that generates the signal
    // as well as selecting it: the selector is only the top half of a
    // two-level mux, and a reader that sets one half depends on whoever set
    // the other.
    // docs/investigations/the-test-bus-selection-had-two-halves.md
    static void selectInputVsync();
    static void selectOutputVsync();

    // Ask the input formatter for one of its signals, without selecting it.
    // RD-5725-1.1 tabulates no values, so which signal a number is belongs to
    // the caller -- a sweep asking each in turn, or selectInputVsync().
    static void driveFormatter(uint8_t signal);

    // Drive this signal out. Enables the bus AND the pad the signal leaves the
    // chip on: a selection nothing is driving is not a selection, and leaving
    // either to the caller is what makes a measurement silently read 0.
    //
    // RD-5725-1.1 tabulates no values for the selector, so which block drives
    // which is not derivable and that block's enable stays the caller's.
    static void select(uint8_t signal);

    // What is selected, and whether it is driven. For a caller that has to put
    // back what it found -- a sweep across every selector, say.
    static uint8_t selected();
    static bool enabled();
    static void enable(bool on);

    // Borrow the pin and put back everything the borrow moved: the selector,
    // the bus enable, the pad, and both halves of the sub-selection. A
    // borrower that restores the selector alone leaves the next reader on
    // whichever stage or signal it chose. Scope one over the borrowing.
    class Hold {
    public:
        Hold();
        ~Hold();

    private:
        uint8_t sel_;
        uint8_t pad_;
        uint8_t ifSel_;
        uint8_t ifEn_;
        uint8_t vdsEn_;
        uint8_t spModule_;
        uint8_t spSignal_;
        uint8_t spEn_;
        bool enabled_;
    };

    // The bus's value, for a signal read rather than counted. Counting edges
    // is the ESP's job, off the pin. readHigh() is the top byte of the same
    // 16 bits, which is all some callers look at.
    static uint16_t read();
    static uint8_t readHigh();

private:
    static const uint8_t InputVsync = 0x0;
    static const uint8_t OutputVsync = 0x2;
    static const uint8_t FormatterVertical = 3;

    typedef UReg<0x00, 0x4D, 0, 5> TEST_BUS_SEL;
    typedef UReg<0x00, 0x4D, 5, 1> TEST_BUS_EN;
    typedef UReg<0x00, 0x2E, 0, 16> TEST_BUS;
    typedef UReg<0x00, 0x2F, 0, 8> TEST_BUS_2F;
};

}  // namespace Tv5725

#endif  // TV5725_TEST_BUS_H_
