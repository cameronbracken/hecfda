// ported from: HEC.FDA.Statistics/Distributions/ContinuousDistribution.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_CONTINUOUS_DISTRIBUTION_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_CONTINUOUS_DISTRIBUTION_HPP
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>
#include "hecfda/statistics/distributions/i_distribution.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {
class ContinuousDistribution : public IDistribution {
   public:
    long sample_size() const { return sample_size_; }

    // ported from: ContinuousDistribution.cs Sample(double[] packetOfRandomNumbers), lines 55-62.
    // C# builds samples[i] = InverseCDF(packetOfRandomNumbers[i]) for i < SampleSize, then
    // returns Fit(samples) (an IDistribution). Fit's concrete return type varies per distribution
    // (e.g. Normal fit(...) returns Normal), so this phase-0 base only produces the raw samples
    // vector; callers reproduce the full C# behavior by composing `derived.fit(base.sample(packet))`.
    std::vector<double> sample(const std::vector<double>& random_packet) const {
        if (static_cast<long>(random_packet.size()) < sample_size_) {
            throw std::invalid_argument(
                "The parametric bootstrap sample cannot be constructed using the distribution. It "
                "requires at least " +
                std::to_string(sample_size_) + " random value(s) but only " +
                std::to_string(random_packet.size()) + " were provided.");
        }
        std::vector<double> samples(static_cast<std::size_t>(sample_size_));
        for (long i = 0; i < sample_size_; ++i) {
            samples[static_cast<std::size_t>(i)] = inverse_cdf(random_packet[static_cast<std::size_t>(i)]);
        }
        return samples;
    }

   protected:
    long sample_size_ = 1;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_CONTINUOUS_DISTRIBUTION_HPP
