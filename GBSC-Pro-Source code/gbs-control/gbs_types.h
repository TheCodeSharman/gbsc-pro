#ifndef GBS_TYPES_H_
#define GBS_TYPES_H_

// The register-map instantiation, in ONE place, so any file that touches a
// register can be a .cpp.

#include "tv5725.h"

typedef TV5725<GBS_ADDR> GBS;

#endif  // GBS_TYPES_H_
