// ported from: .NET seeded System.Random (Knuth subtractive, CompatPrng),
// as used by HEC.FDA.Model/compute/RandomProvider.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_SAMPLING_DOTNET_RANDOM_HPP
#define HECFDA_SAMPLING_DOTNET_RANDOM_HPP
#include <cstdint>
#include <cstdlib>
namespace hecfda {
namespace sampling {
class DotNetRandom {
   public:
    explicit DotNetRandom(std::int32_t seed) {
        std::int32_t subtraction = (seed == INT32_MIN) ? INT32_MAX : std::abs(seed);
        std::int32_t mj = kMSeed - subtraction;
        seed_array_[55] = mj;
        std::int32_t mk = 1;
        for (int i = 1; i < 55; ++i) {
            int ii = (21 * i) % 55;
            seed_array_[ii] = mk;
            mk = mj - mk;
            if (mk < 0) mk += kMBig;
            mj = seed_array_[ii];
        }
        for (int k = 1; k < 5; ++k) {
            for (int i = 1; i < 56; ++i) {
                seed_array_[i] -= seed_array_[1 + (i + 30) % 55];
                if (seed_array_[i] < 0) seed_array_[i] += kMBig;
            }
        }
        inext_ = 0;
        inextp_ = 21;
    }
    std::int32_t internal_sample() {
        int loc_inext = inext_;
        int loc_inextp = inextp_;
        if (++loc_inext >= 56) loc_inext = 1;
        if (++loc_inextp >= 56) loc_inextp = 1;
        std::int32_t ret = seed_array_[loc_inext] - seed_array_[loc_inextp];
        if (ret == kMBig) --ret;
        if (ret < 0) ret += kMBig;
        seed_array_[loc_inext] = ret;
        inext_ = loc_inext;
        inextp_ = loc_inextp;
        return ret;
    }
    double next_double() { return internal_sample() * (1.0 / kMBig); }

   private:
    static constexpr std::int32_t kMBig = 2147483647;
    static constexpr std::int32_t kMSeed = 161803398;
    std::int32_t seed_array_[56] = {};
    int inext_ = 0;
    int inextp_ = 0;
};
}  // namespace sampling
}  // namespace hecfda
#endif
