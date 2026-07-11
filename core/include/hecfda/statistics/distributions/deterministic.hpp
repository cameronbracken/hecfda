// ported from: HEC.FDA.Statistics/Distributions/Deterministic.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_DETERMINISTIC_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_DETERMINISTIC_HPP
#include <memory>
#include <vector>
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/sample_statistics.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: Deterministic.cs. Follows the Task A4 exemplar shape established by Uniform (fields
// + ctor + add_rules() + pdf/cdf/inverse_cdf + equals + fit), constructed via
// IDistributionFactory::create.
class Deterministic : public ContinuousDistribution {
   public:
    Deterministic(double value) : value_(value) { add_rules(); }

    DistributionType type() const override { return DistributionType::Deterministic; }
    double value() const { return value_; }

    // ported from: Deterministic.cs PDF(double x)
    double pdf(double x) const override {
        if (x == value_) {
            return 1.0;
        } else {
            return 0.0;
        }
    }

    // ported from: Deterministic.cs CDF(double x)
    double cdf(double x) const override {
        if (x >= value_) {
            return 1.0;
        } else {
            return 0.0;
        }
    }

    // ported from: Deterministic.cs InverseCDF(double p)
    double inverse_cdf([[maybe_unused]] double p) const override { return value_; }

    // ported from: Deterministic.cs Equals(IDistribution distribution)
    bool equals(const IDistribution& distribution) const override {
        if (type() != distribution.type()) return false;
        const auto& other = static_cast<const Deterministic&>(distribution);
        return value_ == other.value_;
    }

    // ported from: Deterministic.cs Fit(double[] sample) -> `new Deterministic(stats.Mean)`.
    std::unique_ptr<IDistribution> fit(const std::vector<double>& sample) const override {
        hecfda::statistics::SampleStatistics stats(sample);
        return std::make_unique<Deterministic>(stats.mean());
    }

   private:
    // ported from: Deterministic.cs addRules(). No validation rules registered.
    void add_rules() {
        // C# Deterministic has no validation rules (addRules is empty).
    }

    double value_;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_DETERMINISTIC_HPP
