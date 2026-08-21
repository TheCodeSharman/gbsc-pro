#ifndef TV5725_DISPLAY_CLOCK_H_
#define TV5725_DISPLAY_CLOCK_H_

// The display clock, and the output raster it has to pay for.
//
//     required clock = rasterHorizontalTotal x frameLines x sourceFieldRate
//
// so CeilingHz is a CONSTRAINT ON WHICH RASTERS ARE LEGAL, not an input. Neither
// FrameSync::findBestHTotal() nor externalClockGenSyncInOutRate() owns the
// raster: the first returns the total unchanged once the periods agree, and the
// second steers the Si5351 by an unclamped ratio, so the divider byte is only a
// seed that puts it in range.
//
// Every mapped clock is 648 MHz over an integer -- /16, /12, /10, /8, /6, /5, /4
// -- which is where the register's name comes from. It stays a table because the
// byte is not the divider: the high nibble runs 2, 4, 5, 6, 8, 9, A against
// divisors 16, 12, 10, 8, 6, 5, 4.

#include <stdint.h>

namespace Clock {
class ClockGen;
}

namespace Tv5725 {

class DisplayClock {
public:
    // The highest display clock Tvia documents as tested: its own current
    // measurement reads "162MHz 32bit memory, 108MHz Display clock", and
    // DS-5725-3.2 Table 15 rates CLKOUT at 108 MHz / 20pF.
    //
    // **A WORKING CEILING, NOT A PROVEN HARD LIMIT.** CLKOUT is disabled on this
    // board -- PAD_CKOUT_ENZ is 1 in every scaling preset, because the MS9288A
    // takes the analog output -- so Table 15 rates a pad nobody loads, and what
    // bounds the internal VCLK is stated nowhere. The register offers 129.6 and
    // 162 MHz above this.
    //
    // Table 14's 80 MHz CLKIN is the digital video INPUT port, unused here, and
    // the 162 MHz FBCLK rating is the memory interface. Neither is this.
    //
    // CEA-861 1080p wants 148.5 MHz, which nothing in the mapped range hits
    // except 162, so the MS9288A is always resampling. That is the architecture.
    static const uint32_t CeilingHz = 108000000;

    // PLL_VS4 = 11, PLL_2XV = 1: take the display clock from pin 40, which is
    // the Si5351. Every other mapped byte selects the internal PLL648 instead,
    // at a rate nothing can slew. docs/tv5725-chip.md
    static const uint8_t ExternalPclkIn = 0x75;

    // What the firmware falls back to when no byte names a clock. A GUESS, kept
    // because it is what shipped: a lost divider costs 25% of the line here
    // instead of announcing itself.
    static const uint32_t FallbackHz = 81000000;

    // The DISTINCT divider bytes, in ascending clock order, for a caller that has
    // to pick one rather than decode one. 0x35 and 0x00 are absent deliberately:
    // hzFor() maps both to 81 MHz, but they are aliases of 0x65, and three ways
    // to ask for 81 MHz would make "the largest seed under the ceiling" ambiguous.
    static const uint8_t SeedCount = 7;
    static const uint8_t Seeds[SeedCount];

    // This divider byte's display clock, or 0 when the byte does not name one.
    // 0 rather than a default: the sentinel is a byte the firmware writes itself,
    // and answering FallbackHz for it is how externalClockGenResetClock() lost
    // the divider and ran the display 25% slow.
    static uint32_t hzFor(uint8_t divider);

    // The widest horizontal total this clock affords, given the frame height it
    // must fill and the field rate it must match. 0 if any input is 0.
    //
    // FLOORED: a raster needs horizontalTotal x frameLines x fieldRate hertz, so
    // rounding up asks for a clock the part is not rated for.
    //
    // `frameLines` is the raster TOTAL, not the register: VDS_VSYNC_RST holds one
    // less.
    static uint16_t horizontalTotalFor(uint32_t hz, uint16_t frameLines,
                              float fieldRateHz);

    DisplayClock();

    // The generator this board found, if it found one. Absent means the
    // internal PLL is driving the display and there is nothing to steer.
    void driveWith(Clock::ClockGen &generator);

    // The seed the raster solve chose. A TARGET FREQUENCY, not a byte to write:
    // select() puts ExternalPclkIn in the register instead whenever a generator
    // is driving, so the register stops answering what the raster asked for.
    void hold(uint8_t seed);

    // For the paths that solve no raster -- bypass, and the serial toggle --
    // where the register is the only source there is.
    void adopt();
    // Whether a generator is attached to steer, which is what select() keys on
    // and what makes frame time lock possible at all.
    bool driving() const;

    // Point the part at the clock source that can serve the held seed: PCLKIN
    // when a generator is driving, the seed's own internal divider when none is.
    //
    // Writing the seed with a generator present runs the display off the
    // internal PLL, which frame time lock cannot steer -- the output then drifts
    // against the source with every register reading correct.
    void select();

    // Whether a seed has been held or adopted at all. 0x00 is a MAPPED seed --
    // hzFor() answers 81 MHz for it -- so an unset one would otherwise report
    // exactly the fallback frequency this exists to stop guessing.
    bool known() const;

    uint8_t seed() const;
    uint32_t hz() const;

    // Steer the generator to the held frequency and hand it the display clock.
    // Returns what it steered to, or 0 when the board has no generator.
    //
    // A seed mapping to no frequency gets FallbackHz, which is a GUESS and an
    // expensive one: 81 MHz against a raster wanting 108 costs a quarter of the
    // horizontal resolution and still looks like a working picture. Compare the
    // result against hz() to see that it happened.
    uint32_t reset();

    // What the part is running at NOW, which is not what the raster asked for:
    // the frame time lock walks it away from that on every correction. hz() is
    // what the held seed is worth, hzNow() is where the steering has got to.
    uint32_t hzNow() const;

    // Adopt a frequency nothing here steered to -- the pre-detection seed
    // ClockGen::begin() is handed, and the serial override.
    void assumeHz(uint32_t hz);

    // Walk to a new frequency rather than jumping. The pump is called between
    // steps: a slew is up to 750 I2C transactions, long enough that WiFi and
    // the watchdog need servicing.
    void slewTo(uint32_t hz, void (*pump)());

private:
    Clock::ClockGen *generator_;
    uint32_t hzNow_;
    uint8_t seed_;
    bool known_;
};

}  // namespace Tv5725

#endif  // TV5725_DISPLAY_CLOCK_H_
