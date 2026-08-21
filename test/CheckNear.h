#ifndef TEST_CHECK_NEAR_H_
#define TEST_CHECK_NEAR_H_

#include <cmath>

#include <doctest/doctest.h>

// An absolute tolerance, because every tolerance in the geometry suites is a
// count of pixels rather than a proportion.
#define CHECK_NEAR(got, want, tol)                                             \
    CHECK_MESSAGE(std::fabs((double)(got) - (double)(want)) <= (double)(tol),  \
                  #got " = " << (double)(got) << ", wanted " << (double)(want) \
                             << " +-" << (double)(tol))

#endif  // TEST_CHECK_NEAR_H_
