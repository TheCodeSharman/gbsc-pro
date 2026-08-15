#include "MemoryMap.h"

namespace Tv5725 {

const uint32_t MemoryMap::SpaceWords;
const uint8_t MemoryMap::WordsPerPixel;
const uint32_t MemoryMap::FieldStoreStart;
const uint32_t MemoryMap::FieldStoreGuard;
const uint32_t MemoryMap::CaptureStart;
const uint32_t MemoryMap::CaptureGuard;

uint32_t MemoryMap::captureWords()
{
    // Inclusive of the guard word itself: the guard is the last address the
    // buffer may use, not the first it may not.
    return CaptureGuard - CaptureStart + 1;
}

uint32_t MemoryMap::marginWords()
{
    return CaptureStart - FieldStoreGuard;
}

uint32_t MemoryMap::wordsFor(uint16_t width, uint16_t lines)
{
    return (uint32_t)width * (uint32_t)lines * WordsPerPixel;
}

bool MemoryMap::captureFits(uint16_t width, uint16_t lines)
{
    return wordsFor(width, lines) <= captureWords();
}

uint16_t MemoryMap::clampWidth(uint16_t width, uint16_t lines)
{
    const uint16_t widest = maxCaptureWidth(lines);
    return width > widest ? widest : width;
}

uint16_t MemoryMap::maxCaptureWidth(uint16_t lines)
{
    if (lines == 0) {
        return 0xFFFF;
    }
    const uint32_t perLine = (uint32_t)lines * WordsPerPixel;
    const uint32_t widest = captureWords() / perLine;
    return widest > 0xFFFF ? 0xFFFF : (uint16_t)widest;
}

}  // namespace Tv5725
