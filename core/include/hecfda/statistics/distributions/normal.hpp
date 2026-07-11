// ported from: HEC.FDA.Statistics/Distributions/Normal.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_NORMAL_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_NORMAL_HPP
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>
#include "hecfda/constants.hpp"
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/sample_statistics.hpp"
#include "hecfda/statistics/special_functions.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {
class Normal : public ContinuousDistribution {
   public:
    Normal(double mean, double sd, long sample_size = 1) : mean_(mean), sd_(sd) {
        this->sample_size_ = sample_size;
        add_rules();
    }

    DistributionType type() const override { return DistributionType::Normal; }
    double mean() const { return mean_; }
    double standard_deviation() const { return sd_; }

    // ported from: Normal.cs PDF(double x)
    double pdf(double x) const override {
        if (sd_ == 0) {
            return x == mean_ ? 1.0 : 0.0;
        }
        return std::exp(-(x - mean_) * (x - mean_) / (2.0 * sd_ * sd_)) /
               (std::sqrt(2.0 * hecfda::kPi) * sd_);
    }

    // ported from: Normal.cs CDF(double x)
    double cdf(double x) const override {
        if (sd_ == 0) {
            return x >= mean_ ? 1.0 : 0.0;
        }
        if (std::isinf(x)) {
            return x > 0 ? 1.0 : 0.0;
        }
        double g = SpecialFunctions::reg_incomplete_gamma(
            0.5, (x - mean_) * (x - mean_) / (2.0 * sd_ * sd_));
        return x >= mean_ ? 0.5 * (1.0 + g) : 0.5 * (1.0 - g);
    }

    // ported from: Normal.cs InverseCDF(double p); Abramowitz-Stegun rational approximation,
    // constants c0..d3 transcribed verbatim.
    double inverse_cdf(double p) const override {
        const double c0 = 2.515517, c1 = .802853, c2 = .010328;
        const double d1 = 1.432788, d2 = .189269, d3 = .001308;
        double q = p;
        if (q == .5) {
            return mean_;
        }
        if (q <= 0) {
            q = .000000000000001;
        }
        if (q >= 1) {
            q = .999999999999999;
        }
        int i;
        if (q < .5) {
            i = -1;
        } else {
            i = 1;
            q = 1 - q;
        }
        double t = std::sqrt(std::log(1 / (q * q)));
        double t2 = t * t, t3 = t2 * t;
        double x = t - (c0 + c1 * t + c2 * t2) / (1 + d1 * t + d2 * t2 + d3 * t3);
        x = i * x;
        return (x * sd_) + mean_;
    }

    // ported from: Normal.cs static double StandardNormalInverseCDF(double p). Byte-for-byte the
    // same Abramowitz-Stegun rational approximation as inverse_cdf() above, evaluated with mean=0,
    // sd=1 (the `x = i * x; return (x * 1.0);` tail collapses to just `x`). Added as its own static
    // per Task B7 (LogPearson3's InverseCDF calls this directly, not via a Normal(0,1) instance) --
    // earlier ports (lognormal.hpp, truncated_normal.hpp, truncated_lognormal.hpp) instead
    // constructed a local `Normal(0.0, 1.0)` and called inverse_cdf(p), which is mathematically
    // identical; those call sites are left as-is (not refactored) since both forms are correct.
    static double standard_normal_inverse_cdf(double p) {
        const double c0 = 2.515517, c1 = .802853, c2 = .010328;
        const double d1 = 1.432788, d2 = .189269, d3 = .001308;
        double q = p;
        if (q == .5) {
            return 0.0;
        }
        if (q <= 0) {
            q = .000000000000001;
        }
        if (q >= 1) {
            q = .999999999999999;
        }
        int i;
        if (q < .5) {
            i = -1;
        } else {
            i = 1;
            q = 1 - q;
        }
        double t = std::sqrt(std::log(1 / (q * q)));
        double t2 = t * t, t3 = t2 * t;
        double x = t - (c0 + c1 * t + c2 * t2) / (1 + d1 * t + d2 * t2 + d3 * t3);
        x = i * x;
        return x;
    }

    // ported from: Normal.cs Equals(IDistribution distribution)
    bool equals(const IDistribution& distribution) const override {
        const auto* other = dynamic_cast<const Normal*>(&distribution);
        if (other == nullptr) return false;
        return sample_size() == other->sample_size() && mean_ == other->mean_ && sd_ == other->sd_;
    }

    // ported from: Normal.cs Fit(double[] sample) -> `new Normal(stats.Mean, stats.StandardDeviation,
    // stats.SampleSize)`. Phase 1 builds the real SampleStatistics (Welford-style running variance,
    // rescaled to the population/divide-by-n moment on the Variance getter -- see
    // sample_statistics.hpp) rather than Phase 0's thin inline population mean/sd stub. Now returns
    // `unique_ptr<IDistribution>`, matching IDistribution::fit's polymorphic signature (Task A4).
    std::unique_ptr<IDistribution> fit(const std::vector<double>& sample) const override {
        hecfda::statistics::SampleStatistics stats(sample);
        return std::make_unique<Normal>(stats.mean(), stats.standard_deviation(), stats.sample_size());
    }

   private:
    // ported from: Normal.cs AddRules(). Registered but not auto-evaluated: matching C#, a
    // caller must invoke validate() (Validation::validate) before has_errors()/error_level() are
    // meaningful -- neither Normal's nor Uniform's C# ctor calls Validate() itself.
    void add_rules() {
        add_single_property_rule(
            "StandardDeviation", [this]() { return sd_ >= 0; },
            "Standard Deviation must be greater than or equal to 0.", ErrorLevel::Fatal);
        add_single_property_rule(
            "StandardDeviation", [this]() { return sd_ > 0; }, "Standard Deviation shouldnt be 0.",
            ErrorLevel::Minor);
        add_single_property_rule(
            "SampleSize", [this]() { return sample_size_ > 0; }, "SampleSize must be greater than 0.",
            ErrorLevel::Fatal);
    }

    double mean_, sd_;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_NORMAL_HPP
