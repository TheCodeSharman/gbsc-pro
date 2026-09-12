#ifndef TV5725_VIDEO_ROUTE_H
#define TV5725_VIDEO_ROUTE_H

namespace Tv5725 {

// Which route carries the video to the DAC, in force rather than intended.
//
// The three are alternatives in the chip and Tv5725::Chip keeps them so, which
// is why one value holds the answer: a second spelling somewhere else can say
// two of them are carrying at once, and the loop then reads both to reconstruct
// one fact.
//
// They differ in silicon rather than in configuration, which is why this is not
// a bool: the HD bypass channel carries the video with a matrix and a dynamic
// range converter in circuit, and the ADC-to-DAC route has neither.
// docs/investigations/one-bypass-route-carries-rgbhv.md
class VideoRoute {
public:
    enum Route {
        Scaler,           // the scaler drives the output
        HdBypassChannel,  // DAC_RGBS_BYPS2DAC, the HD bypass channel to the DAC
        AdcToDac,         // DAC_RGBS_ADC2DAC, nothing between ADC and DAC
    };

    static Route route();

    // The one route with readers all over the firmware, which is why it has a
    // predicate of its own: video around the scaler means no solve is coming,
    // no frame time lock and no display clock to steer.
    static bool isHdBypassChannel();

    static void toScaler();
    static void toHdBypassChannel();
    static void toAdcToDac();

private:
    static Route route_;
};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_ROUTE_H
