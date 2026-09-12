#ifndef TV5725_RGBHV_OUTPUT_H
#define TV5725_RGBHV_OUTPUT_H

namespace Tv5725 {

// Whether a scaled mode is established for a source Mode Detect names nothing
// for.
//
// The standard byte says the source is RGBHV, which detection establishes
// before any output has been chosen. What it gets is this, and the two are not
// one fact: a scaled preset can be loaded for a source that has no established
// mode yet, which is what sends it through the sync watcher's steering block
// rather than past it.
//
// Not Tv5725::PresetLoad's scaling-RGBHV flag either, which says a preset is
// loaded. That answers whether the last load enabled scaling; this answers what
// the source is entitled to, and the bypass-refused path sets them opposite.
// docs/investigations/the-rgbhv-question-is-two-questions.md
class RgbhvOutput {
public:
    static bool isScaling();

    static void chooseScaling();
    static void chooseBypass();

private:
    static bool scaling_;
};

}  // namespace Tv5725

#endif  // TV5725_RGBHV_OUTPUT_H
