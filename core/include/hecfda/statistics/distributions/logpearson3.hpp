// ported from: HEC.FDA.Statistics/Distributions/LogPearson3.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_LOGPEARSON3_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_LOGPEARSON3_HPP
#include <cmath>
#include <memory>
#include <vector>
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/distributions/pearson3.hpp"
#include "hecfda/statistics/sample_statistics.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: LogPearson3.cs. The flood-frequency workhorse: Mean/StandardDeviation/Skewness are
// moments of the LOG BASE 10 of the underlying random variable (flow). PDF/CDF delegate to the
// already-ported PearsonIII helper evaluated at log10(x) (with the log10 Jacobian 1/(x*ln(10)) for
// PDF); InverseCDF does NOT delegate to PearsonIII -- it applies the Wilson-Hilferty
// cube-root-normal approximation directly against Normal::standard_normal_inverse_cdf, exactly as
// upstream (the commented-out `PearsonIII(...).InverseCDF` delegation is dead code in C#, not
// ported). `_skewDividedBySix`/`_twoDividedBySkew` are precomputed once per Skewness value
// (upstream: a property setter side effect on the `Skewness` property) rather than recomputed
// every InverseCDF call; C++ recomputes them once in each ctor body instead of via a
// property-setter equivalent, giving identical values.
class LogPearson3 : public ContinuousDistribution {
   public:
    // ported from: LogPearson3.cs ctor(mean, standardDeviation, skew, sampleSize=1). Default
    // sample size is 1, matching the C# default parameter.
    LogPearson3(double mean, double standard_deviation, double skew, long sample_size = 1)
        : mean_(mean), sd_(standard_deviation), skew_(skew) {
        this->sample_size_ = sample_size;
        recompute_skew_terms();
        add_rules();
    }

    DistributionType type() const override { return DistributionType::LogPearsonIII; }
    double mean() const { return mean_; }
    double standard_deviation() const { return sd_; }
    double skewness() const { return skew_; }

    // ported from: LogPearson3.cs PDF(double x): PearsonIII(Mean,SD,Skew,SampleSize).PDF(log10(x))
    // / x / Math.Log(10) -- the log10-change-of-variables Jacobian (Math.Log(10) is the NATURAL
    // log of 10, not log10 of anything).
    double pdf(double x) const override {
        PearsonIII d(mean_, sd_, skew_, sample_size_);
        return d.pdf(std::log10(x)) / x / std::log(10.0);
    }

    // ported from: LogPearson3.cs CDF(double x): x>0 ? PearsonIII(...).CDF(log10(x)) : 0.
    double cdf(double x) const override {
        if (x > 0) {
            PearsonIII d(mean_, sd_, skew_, sample_size_);
            return d.cdf(std::log10(x));
        }
        return 0.0;
    }

    // ported from: LogPearson3.cs InverseCDF(double p). Clamps p to [1e-12, 1-1e-12], then applies
    // the Wilson-Hilferty cube-root-normal approximation directly (NOT via PearsonIII -- the
    // commented-out PearsonIII delegation in the C# source is unreachable dead code, not
    // transcribed). The `Skewness == 0` branch is an EXACT equality check here (unlike PearsonIII's
    // fabs(skew) < 0.00001 threshold) -- transcribed verbatim per upstream. The operation order in
    // the skewed branch is preserved exactly, per the upstream comment "pemdas says you cant
    // substitute for the divide in that other instance... so dont do it!" -- `_twoDividedBySkew` is
    // multiplied against the (whfactor^3 - 1) term, never re-derived as `2 * (...) / Skewness`.
    double inverse_cdf(double p) const override {
        if (p <= 0) {
            p = 0.000000000001;
        }
        if (p >= 1) {
            p = 0.999999999999;
        }
        if (skew_ == 0) {
            double logflow = (Normal::standard_normal_inverse_cdf(p) * sd_) + mean_;
            return std::pow(10.0, logflow);
        } else {
            double z = Normal::standard_normal_inverse_cdf(p);
            double whfactor = (z - skew_divided_by_six_) * skew_ / 6.0 + 1;
            // pemdas says you cant substitute for the divide in that other instance... so dont do
            // it! (upstream comment, preserved: _twoDividedBySkew multiplies, never re-divides)
            double k = (two_divided_by_skew_) * ((whfactor * whfactor * whfactor) - 1);
            double logflow = mean_ + (k * sd_);
            return std::pow(10.0, logflow);
        }
    }

    // ported from: LogPearson3.cs Equals(IDistribution distribution). Checked dynamic_cast, per
    // the equals-safety convention established across this port (see e.g. lognormal.hpp,
    // truncated_normal.hpp).
    bool equals(const IDistribution& distribution) const override {
        const auto* other = dynamic_cast<const LogPearson3*>(&distribution);
        if (other == nullptr) return false;
        return mean_ == other->mean_ && sd_ == other->sd_ && skew_ == other->skew_ &&
               sample_size() == other->sample_size();
    }

    // ported from: LogPearson3.cs Fit(double[] sample): logs each sample value with Math.Log10 in
    // place; if ANY value is <= 0 (log10 undefined), returns the error-state ctor
    // (successfullyLoggedData: false) immediately -- the remaining, not-yet-logged values are never
    // processed, matching the C# early `return` inside the for-loop. Otherwise builds
    // SampleStatistics over the logged values and returns `new LogPearson3(stats.Mean,
    // stats.StandardDeviation, stats.Skewness, stats.SampleSize)`.
    std::unique_ptr<IDistribution> fit(const std::vector<double>& sample) const override {
        std::vector<double> logged(sample);
        for (std::size_t i = 0; i < logged.size(); ++i) {
            if (logged[i] <= 0) {
                return std::unique_ptr<LogPearson3>(new LogPearson3(false));
            }
            logged[i] = std::log10(logged[i]);
        }
        hecfda::statistics::SampleStatistics stats(logged);
        return std::make_unique<LogPearson3>(stats.mean(), stats.standard_deviation(), stats.skewness(),
                                              stats.sample_size());
    }

   private:
    // ported from: LogPearson3.cs private LogPearson3(bool successfullyLoggedData) -- the
    // error-state ctor returned by Fit() when any sample value is <= 0. Reflection defaults
    // (Mean=0.1, StandardDeviation=.01, Skewness=.01, SampleSize=1) are reproduced verbatim even
    // though this ctor is reached only via Fit(); IsNull is not ported (reflection/UI-serialization
    // concern with no consumer in the ported IDistribution surface, matching the DONE_WITH_CONCERNS
    // scoping already applied to ToXML/FromXML elsewhere in this port).
    explicit LogPearson3(bool successfully_logged_data)
        : mean_(0.1), sd_(0.01), skew_(0.01), successfully_logged_data_(successfully_logged_data) {
        this->sample_size_ = 1;
        recompute_skew_terms();
        add_rules();
    }

    // ported from: LogPearson3.cs Skewness property setter side effect: recomputes
    // _skewDividedBySix/_twoDividedBySkew whenever Skewness is assigned (both ctors assign
    // Skewness once, so this runs exactly once per instance, matching observable C# behavior).
    void recompute_skew_terms() {
        if (skew_ != 0) {
            skew_divided_by_six_ = skew_ / 6.0;
            two_divided_by_skew_ = 2.0 / skew_;
        } else {
            skew_divided_by_six_ = 0.0;
            two_divided_by_skew_ = 0.0;
        }
    }

    // ported from: LogPearson3.cs addRules().
    void add_rules() {
        add_single_property_rule(
            "_successfullyLoggedData", [this]() { return successfully_logged_data_ == true; },
            "Input flow values cannot be negative", ErrorLevel::Severe);
        add_single_property_rule(
            "StandardDeviation", [this]() { return sd_ > 0; }, "Standard Deviation must be greater than 0.",
            ErrorLevel::Fatal);
        add_single_property_rule(
            "StandardDeviation", [this]() { return sd_ < 3; }, "Standard Deviation must be less than 3.",
            ErrorLevel::Fatal);
        add_single_property_rule(
            "Mean", [this]() { return mean_ > 0; }, "Mean must be greater than 0.", ErrorLevel::Fatal);
        add_single_property_rule(
            "Mean", [this]() { return mean_ < 7; }, "Mean must be less than 7.", ErrorLevel::Fatal);
        add_single_property_rule(
            "Skewness", [this]() { return skew_ > -3.0; }, "Skewness must be greater than -3.0.",
            ErrorLevel::Fatal);
        add_single_property_rule(
            "Skewness", [this]() { return skew_ < 3.0; }, "Skewness must be less than 3.0.", ErrorLevel::Fatal);
        add_single_property_rule(
            "SampleSize", [this]() { return sample_size_ > 0; }, "SampleSize must be greater than 0.",
            ErrorLevel::Fatal);
    }

    double mean_;
    double sd_;
    double skew_;
    double skew_divided_by_six_ = 0.0;
    double two_divided_by_skew_ = 0.0;
    bool successfully_logged_data_ = true;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_LOGPEARSON3_HPP
