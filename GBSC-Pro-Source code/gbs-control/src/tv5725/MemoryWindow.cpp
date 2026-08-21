#include "MemoryWindow.h"

namespace Tv5725 {

MemoryWindow::MemoryWindow() {}

MemoryWindow::MemoryWindow(const BlankingTiming &horizontal, const BlankingTiming &vertical)
    : horizontal_(horizontal), vertical_(vertical) {}

const BlankingTiming &MemoryWindow::horizontal() const { return horizontal_; }

const BlankingTiming &MemoryWindow::vertical() const { return vertical_; }

}  // namespace Tv5725
