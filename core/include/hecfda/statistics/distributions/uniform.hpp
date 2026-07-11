// ported from: HEC.FDA.Statistics/Distributions/Uniform.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_UNIFORM_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_UNIFORM_HPP
#include <memory>
#include <vector>
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/sample_statistics.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: Uniform.cs. The Task A4 exemplar distribution -- every later distribution follows
// this shape (fields + ctor + add_rules() + pdf/cdf/inverse_cdf + equals + fit), constructed via
// IDistributionFactory::create.
class Uniform : public ContinuousDistribution {
   public:
    Uniform(double min, double max, long sample_size = 1) : min_(min), max_(max) {
        this->sample_size_ = sample_size;
        add_rules();
    }

    DistributionType type() const override { return DistributionType::Uniform; }
    double min() const { return min_; }
    double max() const { return max_; }

    // ported from: Uniform.cs PDF(double x)
    double pdf(double x) const override {
        if (max_ == min_) {
            return x == min_ ? 1.0 : 0.0;
        }
        if (x < min_) {
            return 0.0;
        }
        if (x <= max_) {
            return 1.0 / (max_ - min_);
        }
        return 0.0;
    }

    // ported from: Uniform.cs CDF(double x)
    double cdf(double x) const override {
        if (max_ == min_) {
            return x >= min_ ? 1.0 : 0.0;
        }
        if (x < min_) {
            return 0.0;
        }
        if (x <= max_) {
            return (x - min_) / (max_ - min_);
        }
        return 0.0;
    }

    // ported from: Uniform.cs InverseCDF(double p)
    double inverse_cdf(double p) const override { return min_ + ((max_ - min_) * p); }

    // ported from: Uniform.cs Equals(IDistribution distribution)
    bool equals(const IDistribution& distribution) const override {
        const auto* other = dynamic_cast<const Uniform*>(&distribution);
        if (other == nullptr) return false;
        return min_ == other->min_ && max_ == other->max_ && sample_size() == other->sample_size();
    }

    // ported from: Uniform.cs Fit(double[] sample) -> `new Uniform(stats.Min, stats.Max,
    // stats.SampleSize)`.
    std::unique_ptr<IDistribution> fit(const std::vector<double>& sample) const override {
        hecfda::statistics::SampleStatistics stats(sample);
        return std::make_unique<Uniform>(stats.min(), stats.max(), stats.sample_size());
    }

   private:
    // ported from: Uniform.cs addRules(). Registered but not auto-evaluated: a caller must
    // invoke validate() (Validation::validate) before has_errors()/error_level() are meaningful,
    // matching C# (neither ctor calls Validate() itself; UniformTests.cs calls it explicitly).
    void add_rules() {
        add_single_property_rule(
            "Min", [this]() { return min_ <= max_; }, "Min must be smaller or equal to Max.",
            ErrorLevel::Fatal);
        add_single_property_rule(
            "Min", [this]() { return min_ < max_; }, "Min shouldnt equal Max.", ErrorLevel::Minor);
        add_single_property_rule(
            "SampleSize", [this]() { return sample_size_ > 0; }, "SampleSize must be greater than 0.",
            ErrorLevel::Fatal);
    }

    double min_, max_;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_UNIFORM_HPP
