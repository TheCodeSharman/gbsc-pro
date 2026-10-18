#ifndef TV5725_AUTO_GAIN_H_
#define TV5725_AUTO_GAIN_H_

#include <stdint.h>

namespace Tv5725 {

// Keeps the converter's analog gain below the level where it clips.
//
// The gain a source wants is a fact about that source's output amplitude and
// the board's attenuation of it, and nothing on the board measures either --
// so the colour space seeds a gain hot enough that a standard source clips,
// and this walks it back until nothing does. A seed is therefore a starting
// point rather than an answer, and a source hotter or weaker than nominal
// lands where its own white does.
//
// The walk only ever backs off. A level below the band says nothing about how
// much headroom is left, so a source showing a dark scene would read as weak
// and a loop that raised the gain on that reading would clip the moment the
// scene brightened.
class AutoGain {
public:
    // Where a channel stops, in the ten bits TestBus::LevelMax states. The
    // value a channel stops at is PER CHANNEL and sits below full scale, so
    // this is a band rather than the top of the range -- an equality against
    // full scale is a condition that never holds on one of the two inputs,
    // which is the shape of the defect this replaced. The gap below it is
    // wide.
    // docs/investigations/the-auto-gain-loop-read-a-selector-with-no-level.md
    static const uint16_t Saturated = 0x3F8;

    // How far below that the picture's top is held. Content a little brighter
    // than anything the walk has seen has somewhere to go, which is what keeps
    // a highlight arriving later from clipping flat. Sixteen of a thousand is
    // under a twentieth of one step of PM5544's staircase, so what it costs is
    // below what the instrument can see.
    static const uint16_t Headroom = 16;

    // What a sample has to reach to count against the walk.
    static const uint16_t BackOffLevel = Saturated - Headroom;

    // What the brightest sample of a pass has to reach before the pass says
    // anything about where this source's white sits. Below it a pass reports
    // only that nothing bright was on screen, which is not the same as a gain
    // that fits -- and a walk that settled on one would hold the seed and clip
    // everything brighter that followed.
    static const uint16_t EvidenceLevel = 0x380;

    // Larger is LESS gain, so backing off counts up. All three channels move
    // together, because the colour space set them alike and one moving on its
    // own is a tint.
    static const uint8_t Step = 4;

    // How many reads a pass takes of each channel. Each is whatever the
    // converter held at the moment the bus was read, and the reads take long
    // enough on this bus to span many lines.
    static const uint8_t SamplesPerChannel = 24;

    // How many of a pass's samples -- three channels of SamplesPerChannel --
    // must reach BackOffLevel before the gain moves. About a twelfth of them,
    // so the walk answers to a picture whose top is too high rather than to an
    // edge that overshoots.
    //
    // The floor it has to clear is a read that comes back with every bit set,
    // a few times a pass, on whichever channel's turn it falls. The counts past
    // the knee and below it leave a gap either side of this.
    //
    // A pass that misses costs nothing, because the next one steps; a pass
    // that fires wrongly costs a step the walk can never take back.
    static const uint8_t OverToStep = 8;

    // How long a step needs before the measurement can judge it. The level is
    // read after the frame buffer, so a gain change is a frame or two away
    // from the reading, and a walk that steps again before then judges a gain
    // it has already left -- which takes it to the end of the register and
    // leaves a dark picture no further pass can recover.
    static const uint16_t SettleMs = 64;

    // How often a walk with nothing left to correct looks again. A pass is
    // three channels of SamplesPerChannel plus a settling wait each, so
    // looking at the stepping rate for ever spends a chunk of every loop on a
    // source that has nothing to correct. What it costs is the delay before a
    // picture that starts clipping is noticed.
    static const uint16_t QuietMs = 1000;

    // How often a walk that has found the source's level looks again. It does
    // not stop: the gain is settled against the content that was on screen
    // while it settled, and content brighter than that has to be answered --
    // so the interval goes long rather than the walk ending. At this rate a
    // pass costs a fraction of a percent of the loop.
    static const uint16_t SettledMs = 15000;

    // How many passes showing near-white and asking for no step before the
    // source's level counts as found. Evidence rather than absence: a run of
    // dark passes says nothing and never settles the walk.
    static const uint8_t EvidenceRun = 8;

    // One channel's pass: the highest level it reached, and how many of the
    // samples reached BackOffLevel.
    struct Reading {
        uint16_t peak;
        uint8_t over;
    };

    struct Levels {
        Reading red;
        Reading green;
        Reading blue;
    };

    static bool saturated(const Levels &measured);

    // The gain a measurement asks for, from the one in force.
    static uint8_t gainFor(uint8_t inForce, const Levels &measured);

    // The highest level each channel reached. Borrows the test bus and puts it
    // back.
    static Levels peaks();

    // One pass: measure, decide, hold. Does nothing until the previous step
    // has had time to reach the measurement, and nothing until a second pass
    // agrees -- a pass over the threshold on its own is as likely to be the
    // all-bits read as a picture, and the walk cannot take a step back.
    // Reports whether the gain moved.
    static bool run(uint32_t nowMs);

    // Start the walk again. The source that is arriving is not the one the
    // last pass judged, and its gain has been seeded afresh.
    static void forget();

private:
    // What this pass says about the source's level: a step asked for, a pass
    // of evidence that the gain fits, or a dark pass that says neither.
    static void judgeSettling(bool over, const Levels &measured);

    static uint32_t lastLookMs_;
    static uint8_t evidenceRun_;
    static bool looked_;
    static bool overSeen_;
    static bool settled_;
};

}  // namespace Tv5725

#endif  // TV5725_AUTO_GAIN_H_
