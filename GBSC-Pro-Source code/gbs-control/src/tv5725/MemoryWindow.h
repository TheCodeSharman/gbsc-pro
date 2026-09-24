#ifndef TV5725_MEMORY_WINDOW_H_
#define TV5725_MEMORY_WINDOW_H_

// The region of SDRAM a capture occupies: where it lives, whether it fits, and
// how the playback stage reads it back.
//
// This is the seam between the two sides of the engine. Capture writes a region
// and playback reads it, and every input here is capture-side -- the window's
// extents and the units the line is counted in. Nothing about the output raster
// reaches it, which is what makes it the interface rather than a third party to
// the solve.
//
// Statics rather than an instance because nothing HOLDS a region. The map is
// asked at bring-up, before any capture exists; the clamp is asked while a
// capture window is being built, by the class building it; the fetch and the
// stride are asked once per solve and written straight to the chip. A
// constructed region would be built to answer one question and discarded.
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

#include <stdint.h>

namespace Tv5725 {

class MemoryWindow {
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

    // Whether it fits. The hardware's own answer to an overrun is to wrap,
    // which puts a wrong address on screen rather than reporting anything.
    static bool captureFits(uint16_t width, uint16_t lines);

    // The widest capture this many lines allows. Refusing is not enough on its
    // own -- a caller needs to know what it may have instead, or the only
    // outcomes are "works" and "black screen".
    static uint16_t maxCaptureWidth(uint16_t lines);

    // The width narrowed until it fits, untouched when it already does. Clamp
    // rather than refuse, as the source line's own bound does: a control that
    // can crop the capture to nothing is one keypress from a dead picture.
    // HORIZONTAL is what gives -- capture width is in ADC samples and PLLAD_MD
    // is 12 bits, while the line count belongs to the source.
    static uint16_t clampWidth(uint16_t width, uint16_t lines);

    // --- how playback reads the region back ---------------------------------
    //
    //     PB_FETCH_NUM  = ceil(captureWidth / RequestsPerLine)
    //     PB_CAP_OFFSET = ceil(lineUnits    / RequestsPerLine)
    //
    // Deriving the fetch from the capture holds the capture/fetch ratio fixed
    // as the picture zooms, so no framing can walk into a tearing band. The
    // stride takes the WHOLE LINE instead, so it covers the fetch at every
    // framing of it and moves only when the divider does.
    //
    // The ratio is the measured quantity and nothing may clamp it. A constant
    // floor under it holds the VALUE still while the capture keeps falling,
    // which drives the ratio off the bottom of the band -- measured at capture
    // 456, where the rule's 114 is clean and a floor's 150 puts blocks of other
    // content through flat colour. docs/known-issues.md
    // docs/investigations/horizontal-scale-corruption.md

    // PB_FETCH_NUM and PB_CAP_OFFSET are ten bits each.
    static const uint16_t FetchMax = 512;
    static const uint16_t OffsetMax = 1023;

    // An output raster nobody has swept keeps upstream's value, rather than a
    // pair tuned for a raster it is not. It also answers a capture of zero,
    // which is a failed read rather than a framing and so has no ratio.
    static const uint16_t DefaultFetch = 256;

    // The bench's 1080p output, VDS_HSYNC_RST 1444. Fetch1080p is the anchor
    // the rule is checked against, not what gets written: hand-tuned to 250 at
    // capture 1009, where the rule independently gives 253.
    static const uint16_t Line1080p = 1445;
    static const uint16_t Fetch1080p = 250;
    static const uint16_t Offset1080p = 250;

    // Measured: capture/fetch was clean to 4.04 and tore from 4.28 above, and
    // 3.56 clean against 3.26 breaking up below, so 4 sits inside the band at
    // both ends. The one number to tune if this is wrong.
    static const uint16_t RequestsPerLine = 4;

    // The burst has to cover the source pixels the line needs, and only the
    // framing knows how many. The output raster is deliberately not a
    // parameter: gating the rule on one leaves the fetch short of what a wide
    // capture asks for, and the shortfall shows as tearing.
    static uint16_t fetchFor(uint16_t captureWidth);

    // The line stride: where playback starts the next line, in the same 64-bit
    // memory words as the fetch. Below the fetch, successive lines overlap and
    // each overwrites its predecessor's tail -- which shows as a green band
    // down the right of the picture.
    //
    // Takes the LINE, not the capture on screen. The fetch grows as the picture
    // zooms out, and moving the stride re-lays the buffer out under a picture
    // being read from it, so it is sized for a capture of the whole line: no
    // framing of that line can outgrow it, and it holds still through a zoom.
    //
    // Not the line less the hsync pulse, which is the widest capture actually
    // reachable: that comes from STATUS_SYNC_PROC_HLOW_LEN, a live measurement
    // that moves by a unit between solves, and a stride that followed it would
    // be rewritten on an arbitrary pad press.
    static uint16_t strideFor(uint16_t lineUnits);
};

}  // namespace Tv5725

#endif  // TV5725_MEMORY_WINDOW_H_
