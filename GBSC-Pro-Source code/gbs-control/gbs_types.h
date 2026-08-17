#ifndef GBS_TYPES_H_
#define GBS_TYPES_H_

// The register-map instantiation, in ONE place, so any file that touches a
// register can be a .cpp.

#include "src/tv5725/Tv5725.h"

typedef Tv5725::Tv5725 GBS;

#endif  // GBS_TYPES_H_
