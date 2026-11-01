#ifndef TV5725_ROUTE_H_
#define TV5725_ROUTE_H_

// Whether a source is scaled or passed through, as a value a solve can carry.
//
// THREE STATES, because a source nobody has chosen for is not the same as one
// chosen to be scaled: the first takes the unit's default and the second
// overrides it. A pair of bools expresses the same thing and lets "not stored
// but passing through" be written down. docs/framing-presets.md

#include <stdint.h>

namespace Tv5725 {

class Route {
public:
    Route();

    static Route scaled();
    static Route passedThrough();
    static Route chosen(bool passThrough);

    bool stored() const;

    // False where nothing is stored, so a caller that ignores stored() gets
    // the safe route rather than an uninitialised one.
    bool passesThrough() const;

    bool operator==(const Route &other) const;
    bool operator!=(const Route &other) const;

private:
    enum State { Unstored = 0, Scaled = 1, PassedThrough = 2 };

    explicit Route(State state);

    uint8_t state_;
};

}  // namespace Tv5725

#endif  // TV5725_ROUTE_H_
