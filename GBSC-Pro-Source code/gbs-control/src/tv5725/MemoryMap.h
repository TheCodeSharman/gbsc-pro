#ifndef TV5725_MEMORY_MAP_H
#define TV5725_MEMORY_MAP_H

#include <stdint.h>

namespace Tv5725 {

// The SDRAM layout, and whether the capture about to be programmed fits in it.
//
// The part is one EM638325TS, "2M x 32 SDRAM": 2^21 words of 32 bits = 8 MB, and
// the buffer address registers are 21 bits. docs/tv5725-chip.md.
//
// A pixel costs one word. CAP_ADR_ADD_2 (s4_21[7]) reads 0 on the bench and in
// all twelve tables, so 24-bit RGB takes a whole 32-bit word with 8 bits unused.
// Read off the register, not inferred.
//
// The map, in words:
//
//     0x000000  field store (RFF/WFF)     the deinterlacer's fields, from 0
//     0x052000  WFF_SAFE_GUARD            its bound -- ENABLED, and measured
//               24,576 words of margin
//     0x060000  capture buffer            PB_CAP_BUF_STA_ADDR_A and _B
//     0x1FFFFF  CAP_SAFE_GUARD            the top -- enabled here
//
// The capture base is 0x060000 rather than the tables' 0x100000, which leaves
// nothing stranded between the field store's guard and the capture buffer. At
// 0x100000 a 527-line capture overruns past 1989 samples per line, and PLLAD_MD
// is 12 bits.
//
// One map for both standards. The tables' PAL 0x100000 against NTSC 0xc0000 is a
// coarse split of the map rather than a computed size -- 625/525 is 1.19 against
// the 4/3 the addresses imply.
class MemoryMap {
public:
    // 21-bit word-addressed space: 2^21 words of 32 bits = 8 MB.
    static const uint32_t SpaceWords = 1u << 21;

    // CAP_ADR_ADD_2 = 0. See above -- this is read off the chip.
    static const uint8_t WordsPerPixel = 1;

    // The deinterlacer's field store, and the bound that is already enforced.
    // Left exactly where it was measured: it works, and moving two regions at
    // once would make a bad picture ambiguous.
    static const uint32_t FieldStoreStart = 0x000000;
    static const uint32_t FieldStoreGuard = 0x052000;

    // The capture buffer: everything above the field store, to the top.
    static const uint32_t CaptureStart = 0x060000;
    static const uint32_t CaptureGuard = SpaceWords - 1;

    // Words the capture region holds.
    static uint32_t captureWords();

    // Slack between the field store's guard and the capture buffer. Not needed
    // arithmetically -- it is the number to look at if the deinterlacer ever
    // turns out to want more than its guard suggests.
    static uint32_t marginWords();

    // What a capture window costs, in words.
    static uint32_t wordsFor(uint16_t width, uint16_t lines);

    // Whether it fits. The engine must ask before writing a framing: the
    // hardware's own answer to an overrun is to wrap, which puts a wrong
    // address on screen rather than reporting anything.
    static bool captureFits(uint16_t width, uint16_t lines);

    // The widest capture this many lines allows. Refusing is not enough on its
    // own -- a caller needs to know what it may have instead, or the only
    // outcomes are "works" and "black screen".
    static uint16_t maxCaptureWidth(uint16_t lines);

    // The width narrowed until it fits, untouched when it already does. Clamp
    // rather than refuse, as readCapture() does against the source line: a
    // control that can crop the capture to nothing is one keypress from a dead
    // picture. HORIZONTAL is what gives -- capture width is in ADC samples and
    // PLLAD_MD is 12 bits, while the line count belongs to the source.
    static uint16_t clampWidth(uint16_t width, uint16_t lines);
};

}  // namespace Tv5725

#endif
