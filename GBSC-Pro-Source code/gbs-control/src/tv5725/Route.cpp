#include "Route.h"

namespace Tv5725 {

Route::Route() : state_(Unstored) {}

Route::Route(State state) : state_((uint8_t)state) {}

Route Route::scaled() { return Route(Scaled); }

Route Route::passedThrough() { return Route(PassedThrough); }

Route Route::chosen(bool passThrough)
{
    return passThrough ? passedThrough() : scaled();
}

bool Route::stored() const { return state_ != Unstored; }

bool Route::passesThrough() const { return state_ == PassedThrough; }

bool Route::operator==(const Route &other) const
{
    return state_ == other.state_;
}

bool Route::operator!=(const Route &other) const { return !(*this == other); }

}  // namespace Tv5725
