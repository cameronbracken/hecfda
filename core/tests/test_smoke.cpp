#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "hecfda/constants.hpp"
#include "check.hpp"

TEST_CASE("kPi is defined to double precision") {
    CHECK(hecfda_test::close_abs(hecfda::kPi, 3.141592653589793, 1e-15));
}
