// ported from: HEC.FDA.Statistics/Distributions/LogNormal.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_LOGNORMAL_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_LOGNORMAL_HPP
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/sample_statistics.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: LogNormal.cs. Mean/StandardDeviation are moments of the LOGGED data, but -- per
// the C# docstring on the ctor -- "the log is the natural log NOT log base 10". PDF/CDF and
// InverseCDF/Fit are DELIBERATELY inconsistent about which log they use; this is a real upstream
// quirk, reproduced verbatim rather than harmonized:
//   - pdf()/cdf(): z = (ln(x) - Mean) / StandardDeviation      -- natural log (Math.Log)
//   - inverse_cdf(): exp(Mean + StandardNormalInverseCDF(p)*SD) -- natural exp (Math.Exp)
//   - fit(): sample[i] = log10(sample[i]) before computing SampleStatistics -- LOG BASE 10
// PDF/CDF delegate to a standard normal (mean=0, sd=1), matching C#'s `new Normal()` default
// ctor; InverseCDF delegates to `Normal.StandardNormalInverseCDF`, which is byte-for-byte the
// same rational-approximation formula as `Normal::inverse_cdf` evaluated with mean=0, sd=1 -- so
// a local `Normal(0.0, 1.0)` standard-normal instance is reused for both rather than
// re-transcribing the Abramowitz-Stegun constants a second time.
class LogNormal : public ContinuousDistribution {
   public:
    LogNormal(double mean, double sd, long sample_size = 1) : mean_(mean), sd_(sd) {
        this->sample_size_ = sample_size;
        add_rules();
    }

    DistributionType type() const override { return DistributionType::LogNormal; }
    double mean() const { return mean_; }
    double standard_deviation() const { return sd_; }

    // ported from: LogNormal.cs PDF(double x). z uses Math.Log == natural log.
    double pdf(double x) const override {
        Normal standard_normal(0.0, 1.0);
        double z = (std::log(x) - mean_) / sd_;
        return standard_normal.pdf(z);
    }

    // ported from: LogNormal.cs CDF(double x). z uses Math.Log == natural log.
    double cdf(double x) const override {
        Normal standard_normal(0.0, 1.0);
        double z = (std::log(x) - mean_) / sd_;
        return standard_normal.cdf(z);
    }

    // ported from: LogNormal.cs InverseCDF(double p). Returns the RAW (un-logged) value via
    // Math.Exp -- natural exp, not 10^. `Normal(0.0, 1.0).inverse_cdf(p)` reproduces
    // `Normal.StandardNormalInverseCDF(p)` exactly (identical formula, mean 0 / sd 1).
    double inverse_cdf(double p) const override {
        if (p <= 0) {
            return 0.0;
        }
        if (p >= 1) {
            return std::numeric_limits<double>::infinity();
        }
        Normal standard_normal(0.0, 1.0);
        return std::exp(mean_ + standard_normal.inverse_cdf(p) * sd_);
    }

    // ported from: LogNormal.cs Equals(IDistribution distribution)
    bool equals(const IDistribution& distribution) const override {
        if (type() != distribution.type()) return false;
        const auto& other = static_cast<const LogNormal&>(distribution);
        return mean_ == other.mean_ && sd_ == other.sd_ && sample_size() == other.sample_size();
    }

    // ported from: LogNormal.cs Fit(double[] sample): logs the sample with Math.Log10 (LOG BASE
    // 10 -- inconsistent with pdf/cdf/inverse_cdf's natural log/exp, kept as-is) before computing
    // SampleStatistics, then returns `new LogNormal(stats.Mean, stats.StandardDeviation,
    // stats.SampleSize)`. C# mutates the caller's `sample` array in place; the C++ signature takes
    // `sample` by const ref, so a local mutable copy is log-transformed instead -- no behavioral
    // difference for the returned distribution.
    std::unique_ptr<IDistribution> fit(const std::vector<double>& sample) const override {
        std::vector<double> logged(sample);
        for (double& v : logged) {
            v = std::log10(v);
        }
        hecfda::statistics::SampleStatistics stats(logged);
        return std::make_unique<LogNormal>(stats.mean(), stats.standard_deviation(), stats.sample_size());
    }

   private:
    // ported from: LogNormal.cs addRules().
    void add_rules() {
        add_single_property_rule(
            "StandardDeviation", [this]() { return sd_ >= 0; },
            "Standard Deviation must be greater than or equal to 0.", ErrorLevel::Fatal);
        add_single_property_rule(
            "StandardDeviation", [this]() { return sd_ > 0; }, "Standard Deviation shouldnt equal 0.",
            ErrorLevel::Minor);
        add_single_property_rule(
            "Mean", [this]() { return mean_ > 0; }, "Mean must be greater than 0.", ErrorLevel::Fatal);
        add_single_property_rule(
            "SampleSize", [this]() { return sample_size_ > 0; }, "SampleSize must be greater than 0.",
            ErrorLevel::Fatal);
    }

    double mean_, sd_;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_LOGNORMAL_HPP
