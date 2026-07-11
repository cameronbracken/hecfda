// ported from: HEC.FDA.Statistics/Distributions/Triangular.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_TRIANGULAR_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_TRIANGULAR_HPP
#include <cmath>
#include <memory>
#include <vector>
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/sample_statistics.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: Triangular.cs. Follows the Task A4 exemplar shape established by Uniform (fields
// + ctor + add_rules() + pdf/cdf/inverse_cdf + equals + fit), constructed via
// IDistributionFactory::create.
class Triangular : public ContinuousDistribution {
   public:
    Triangular(double min, double most_likely, double max, long sample_size = 1)
        : min_(min), max_(max), most_likely_(most_likely) {
        this->sample_size_ = sample_size;
        add_rules();
    }

    DistributionType type() const override { return DistributionType::Triangular; }
    double min() const { return min_; }
    double max() const { return max_; }
    double most_likely() const { return most_likely_; }

    // ported from: Triangular.cs PDF(double x)
    double pdf(double x) const override {
        if (max_ == min_) {
            if (x == most_likely_) {  // this is actually kinda odd.
                return 1.0;
            }
            return 0.0;
        }
        if (x < min_) {
            return 0.0;
        } else if (x <= most_likely_) {
            return 2 * (x - min_) / ((max_ - min_) * (most_likely_ - min_));
        } else if (x <= max_) {
            return 2 * (max_ - x) / ((max_ - min_) * (max_ - most_likely_));
        } else {
            return 0.0;
        }
    }

    // ported from: Triangular.cs CDF(double x)
    double cdf(double x) const override {
        if (max_ == min_) {
            if (x >= most_likely_) {  // this is actually kinda odd.
                return 1.0;
            }
            return 0.0;
        }
        if (x < min_) {
            return 0.0;
        } else if (x <= most_likely_) {
            double diff = x - min_;
            return (diff * diff) / ((max_ - min_) * (most_likely_ - min_));
        } else if (x <= max_) {
            double diff = max_ - x;
            return 1 - (diff * diff) / ((max_ - min_) * (max_ - most_likely_));
        } else {
            return 1.0;
        }
    }

    // ported from: Triangular.cs InverseCDF(double p). Transcribed verbatim, including the
    // asymmetric branch split at a/(Max-Min) (below the mode) vs the open-ended `p < 1` branch
    // (above the mode) -- do not "simplify" to a symmetric split, that is not what C# does.
    double inverse_cdf(double p) const override {
        if (max_ == min_) {
            return most_likely_;  // this is actually kinda odd.
        }
        double a = most_likely_ - min_;
        double b = max_ - most_likely_;
        if (p <= 0) {
            return min_;
        } else if (p < a / (max_ - min_)) {
            return min_ + std::sqrt(p * (max_ - min_) * a);
        } else if (p < 1) {
            return max_ - std::sqrt((1 - p) * (max_ - min_) * b);
        } else {
            return max_;
        }
    }

    // ported from: Triangular.cs Equals(IDistribution distribution)
    bool equals(const IDistribution& distribution) const override {
        const auto* other = dynamic_cast<const Triangular*>(&distribution);
        if (other == nullptr) return false;
        return min_ == other->min_ && max_ == other->max_ && sample_size() == other->sample_size() &&
               most_likely_ == other->most_likely_;
    }

    // ported from: Triangular.cs Fit(double[] sample) -> `new Triangular(stats.Min, stats.Mean,
    // stats.Max, stats.SampleSize)`. Note the fitted mode is stats.Mean, not a computed mode --
    // transcribed exactly as the (arguably odd) C# does it.
    std::unique_ptr<IDistribution> fit(const std::vector<double>& sample) const override {
        hecfda::statistics::SampleStatistics stats(sample);
        return std::make_unique<Triangular>(stats.min(), stats.mean(), stats.max(), stats.sample_size());
    }

   private:
    // ported from: Triangular.cs addRules().
    void add_rules() {
        add_single_property_rule(
            "Min", [this]() { return min_ <= max_; }, "Min must not exceed Max.", ErrorLevel::Fatal);
        add_single_property_rule(
            "Min", [this]() { return min_ < max_; }, "Min shouldnt equal Max.", ErrorLevel::Minor);
        add_single_property_rule(
            "Min", [this]() { return min_ <= most_likely_; },
            "Min must be smaller than or equal to MostLikely.", ErrorLevel::Fatal);
        add_single_property_rule(
            "Min", [this]() { return min_ < most_likely_; }, "Min shouldnt equal MostLikely.",
            ErrorLevel::Minor);
        add_single_property_rule(
            "Max", [this]() { return most_likely_ <= max_; },
            "MostLikely must be smaller than or equal to Max.", ErrorLevel::Fatal);
        add_single_property_rule(
            "Max", [this]() { return most_likely_ < max_; }, "MostLikely shouldnt equal to Max.",
            ErrorLevel::Minor);
        add_single_property_rule(
            "SampleSize", [this]() { return sample_size_ > 0; }, "SampleSize must be greater than 0.",
            ErrorLevel::Fatal);
    }

    double min_, max_, most_likely_;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_TRIANGULAR_HPP
