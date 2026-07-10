// ported from: HEC.FDA.Model/compute/RandomProvider.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_COMPUTE_RANDOM_PROVIDER_HPP
#define HECFDA_MODEL_COMPUTE_RANDOM_PROVIDER_HPP
#include <vector>
#include "hecfda/sampling/dotnet_random.hpp"
namespace hecfda {
namespace model {
namespace compute {
// mirrors IProvideRandomNumbers (NextRandom / NextRandomSequence / Seed)
class RandomProvider {
   public:
    explicit RandomProvider(int seed) : seed_(seed), rng_(seed) {}
    double next_random() { return rng_.next_double(); }
    std::vector<double> next_random_sequence(long size) {
        std::vector<double> out(static_cast<std::size_t>(size));
        for (long i = 0; i < size; ++i) out[static_cast<std::size_t>(i)] = rng_.next_double();
        return out;
    }
    int seed() const { return seed_; }

   private:
    int seed_;
    hecfda::sampling::DotNetRandom rng_;
};
}  // namespace compute
}  // namespace model
}  // namespace hecfda
#endif
