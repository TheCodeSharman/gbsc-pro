#ifndef AUDIO_LINEVOLUME_H_
#define AUDIO_LINEVOLUME_H_

// The line input's level, in decibels of attenuation for the PT2257.
// docs/audio-path.md.
#include <stdint.h>

namespace Audio {
class LineVolume {
public:
    // How many decibels of attenuation the setting spans. The part reaches 79,
    // and the divider feeding the encoder costs a further 6 that no setting
    // recovers.
    static const uint8_t Maximum = 50;

    // Where a unit with nothing saved starts. It leaves headroom for a source
    // hotter than the chain expects rather than picking a level, so a quiet
    // source can still be taken to 0.
    static const uint8_t Default = 12;

    static uint8_t attenuationDb(uint8_t setting);

    // The overlay counts the other way, so a larger number on screen is louder.
    static uint8_t displayLevel(uint8_t setting);

    static uint8_t louder(uint8_t setting);
    static uint8_t quieter(uint8_t setting);

private:
    static uint8_t inRange(uint8_t setting);
};

}  // namespace Audio

#endif  // AUDIO_LINEVOLUME_H_
