#include "MemoryWindow.h"

namespace Tv5725 {

const uint32_t MemoryWindow::SpaceWords;
const uint8_t MemoryWindow::WordsPerPixel;
const uint32_t MemoryWindow::FieldStoreStart;
const uint32_t MemoryWindow::FieldStoreGuard;
const uint32_t MemoryWindow::CaptureStart;
const uint32_t MemoryWindow::CaptureGuard;
const uint16_t MemoryWindow::FetchMax;
const uint16_t MemoryWindow::OffsetMax;
const uint16_t MemoryWindow::DefaultFetch;
const uint16_t MemoryWindow::Line1080p;
const uint16_t MemoryWindow::Fetch1080p;
const uint16_t MemoryWindow::Offset1080p;
const uint16_t MemoryWindow::RequestsPerLine;

uint32_t MemoryWindow::captureWords()
{
    // Inclusive of the guard word itself: the guard is the last address the
    // buffer may use, not the first it may not.
    return CaptureGuard - CaptureStart + 1;
}

uint32_t MemoryWindow::marginWords()
{
    return CaptureStart - FieldStoreGuard;
}

uint32_t MemoryWindow::wordsFor(uint16_t width, uint16_t lines)
{
    return (uint32_t)width * (uint32_t)lines * WordsPerPixel;
}

bool MemoryWindow::captureFits(uint16_t width, uint16_t lines)
{
    return wordsFor(width, lines) <= captureWords();
}

uint16_t MemoryWindow::maxCaptureWidth(uint16_t lines)
{
    if (lines == 0) {
        return 0xFFFF;
    }
    const uint32_t perLine = (uint32_t)lines * WordsPerPixel;
    const uint32_t widest = captureWords() / perLine;
    return widest > 0xFFFF ? 0xFFFF : (uint16_t)widest;
}

uint16_t MemoryWindow::clampWidth(uint16_t width, uint16_t lines)
{
    const uint16_t widest = maxCaptureWidth(lines);
    return width > widest ? widest : width;
}

uint16_t MemoryWindow::fetchFor(uint16_t captureWidth)
{
    if (captureWidth == 0)
        return DefaultFetch;

    // ROUNDED UP. A line one pixel short of its source still fails to finish,
    // and it repeats -- the start of the picture reappearing at the right.
    uint32_t needed = ((uint32_t)captureWidth + RequestsPerLine - 1)
                      / RequestsPerLine;

    if (needed > FetchMax)
        needed = FetchMax;

    return (uint16_t)needed;
}

uint16_t MemoryWindow::strideFor(uint16_t lineUnits)
{
    return fetchFor(lineUnits);
}

}  // namespace Tv5725
