#ifndef TV5725_WRITE_TRACE_H_
#define TV5725_WRITE_TRACE_H_

#include <stdint.h>

namespace Tv5725 {

// Every TV5725 register write in order, with its spacing, and the means to
// issue a slice of it back at bus speed.
//
// The picture lands on one of two positions per solve with every register
// identical afterwards, so what decides it is an ordering rather than a value,
// and nothing written after the fact can put the losing side back. A slice
// replayed against a source that has not moved asks which part of the sequence
// carries the choice.
// docs/investigations/the-pan-is-downstream-of-the-scaler.md
//
// **A REPLAY OVER HTTP IS NOT THE SAME EVENT.** /setreg is deferred to loop()
// and lands at tens of hertz whatever bytes it carries, so a sequence whose
// timing matters cannot be reproduced from the host at all.
//
// Static, because the recording hook is inside the bus layer every subsystem
// writes through and there is no object to hand it.
class WriteTrace {
public:
    // Bytes of one write kept. The solve's writes are one to four bytes; the
    // sixteen-byte block writes belong to zeroAll().
    static const uint8_t PayloadMax = 4;

    // Entries. Ten bytes each, out of .bss, and out of the default build: the
    // trace flag is off unless a session is asking this question.
    static const uint16_t Capacity = 512;

    struct Entry {
        uint16_t sinceArmMs;
        uint8_t segment;
        uint8_t reg;
        uint8_t length;
        uint8_t data[PayloadMax];

        bool truncated() const { return length > PayloadMax; }
    };

    // Start recording, discarding whatever is held.
    static void arm(uint32_t nowMs);
    static void stop();
    static bool recording();

    // A write to the segment register aims the window and is not an entry: it
    // becomes the segment every entry after it carries. Storing the selects
    // instead would spend half the ring on them and leave a slice meaningless
    // without everything before it.
    static void record(uint32_t nowMs, uint8_t reg, const uint8_t *data, uint8_t length);

    // Entries held, oldest first.
    static uint16_t count();
    static const Entry &at(uint16_t index);

    // Writes offered since arming. The ring keeps the oldest and stops, so
    // seen() above count() is the tail that was never recorded.
    static uint32_t seen();
    static bool overflowed();

    // One entry as text, fixed width including the newline, NUL terminated.
    // Fixed so the byte offset a chunked response counts in divides straight
    // into an entry number, leaving nothing to carry between calls.
    static const uint8_t LineBytes = 27;
    static void line(char *into, uint16_t index);

    // Issue entries [first, last) back to the chip. Each carries the segment it
    // was recorded under, so any slice lands where it was taken rather than
    // wherever the window happens to point.
    //
    // `gapMs` true reproduces the recorded spacing, which is the question of
    // whether the timing carries the choice; false issues them back to back.
    static void replay(uint16_t first, uint16_t last, bool gaps = false);

private:
    static Entry ring_[Capacity];
    static uint16_t held_;
    static uint32_t seen_;
    static uint32_t armedMs_;
    static uint8_t segment_;
    static bool recording_;
};

}  // namespace Tv5725

#endif  // TV5725_WRITE_TRACE_H_
