#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "hecfda/sampling/dotnet_random.hpp"
#include "check.hpp"

TEST_CASE("DotNetRandom(1234) reproduces the .NET seeded stream") {
    hecfda::sampling::DotNetRandom r(1234);
    const double expected[10] = {
        0.39908097935797693, 0.8958994657247791,  0.3192029387313886,
        0.9467375338760845,  0.33943602458547617, 0.9487782409176129,
        0.8079918901473246,  0.5207309469211525,  0.643958064096029,
        0.31255894820790686};
    for (int i = 0; i < 10; ++i)
        CHECK(hecfda_test::close_abs(r.next_double(), expected[i], 0.0));  // exact
}
