// ported from: HEC.FDA.Statistics/Distributions/ContinuousDistribution.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_CONTINUOUS_DISTRIBUTION_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_CONTINUOUS_DISTRIBUTION_HPP
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/statistics/distributions/i_distribution.hpp"
#include "hecfda/statistics/validation.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: ContinuousDistribution.cs. Derives from both IDistribution (the C# base
// interface) and hecfda::statistics::Validation (Task A1's port of
// HEC.MVVMFramework.Base.Implementations.Validation, reached in C# via
// `ContinuousDistribution : ValidationErrorLogger, IDistribution` --
// ValidationErrorLogger : Validation is a thin WPF-messaging layer with nothing this port
// needs, so Validation is inherited directly).
//
// DONE_WITH_CONCERNS (scoped out, not ported -- see IDistribution.hpp for the matching note):
//  - ToXML()/FromXML(): reflection-driven XML (de)serialization.
//  - ToCoordinates(bool): builds a fixed 161-point exceedance-probability curve for plotting;
//    a UI/graphing concern, not part of the statistics core surface ported so far.
class ContinuousDistribution : public IDistribution, public Validation {
   public:
    long sample_size() const override { return sample_size_; }

    // ported from: ContinuousDistribution.cs GenerateRandomSamplesofNumbers(int seed, int
    // quantityOfSamples). Uses hecfda::model::compute::RandomProvider, which reproduces .NET's
    // `System.Random` sequence exactly (verified by Phase 0's dotnet_random fixture), matching
    // `new Random(seed)` followed by one `NextDouble()` per sample.
    void generate_random_samples_of_numbers(int seed, int quantity_of_samples) {
        hecfda::model::compute::RandomProvider rng(seed);
        random_samples_of_numbers_.clear();
        random_samples_of_numbers_.reserve(static_cast<std::size_t>(quantity_of_samples));
        for (int i = 0; i < quantity_of_samples; ++i) {
            random_samples_of_numbers_.push_back(rng.next_random_sequence(sample_size_));
        }
    }

    // ported from: ContinuousDistribution.cs Sample(double[] packetOfRandomNumbers), lines
    // 55-62. Builds samples[i] = InverseCDF(packetOfRandomNumbers[i]) for i < SampleSize, then
    // returns Fit(samples). Now that `fit` is a full IDistribution virtual returning
    // unique_ptr<IDistribution> (Task A4), this reproduces the C# behavior exactly, replacing
    // Phase 0's raw-samples-vector stub.
    std::unique_ptr<IDistribution> sample(const std::vector<double>& random_packet) const override {
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
        return fit(samples);
    }

    // ported from: ContinuousDistribution.cs Sample(long iterationNumber). Requires
    // generate_random_samples_of_numbers(...) to have been called first -- matches the C#
    // exception thrown when RandomSamplesofNumbers is empty. Not part of the IDistribution
    // interface (the C# overload lives only on ContinuousDistribution).
    std::unique_ptr<IDistribution> sample(long iteration_number) const {
        if (random_samples_of_numbers_.empty()) {
            throw std::runtime_error(
                "This distribution cannot be sampled by iteration number without first "
                "generating random samples of numbers for each iteration");
        }
        const std::vector<double>& random_of_numbers =
            random_samples_of_numbers_.at(static_cast<std::size_t>(iteration_number));
        std::vector<double> samples(static_cast<std::size_t>(sample_size_));
        for (long i = 0; i < sample_size_; ++i) {
            samples[static_cast<std::size_t>(i)] = inverse_cdf(random_of_numbers[static_cast<std::size_t>(i)]);
        }
        return fit(samples);
    }

   protected:
    long sample_size_ = 1;

   private:
    std::vector<std::vector<double>> random_samples_of_numbers_;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_CONTINUOUS_DISTRIBUTION_HPP
