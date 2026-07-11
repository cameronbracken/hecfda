// ported from: HEC.FDA.Statistics/Distributions/TruncatedLogPearson3.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_TRUNCATED_LOGPEARSON3_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_TRUNCATED_LOGPEARSON3_HPP
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/distributions/pearson3.hpp"
#include "hecfda/statistics/sample_statistics.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: TruncatedLogPearson3.cs. Same type()-aliasing convention already established by
// TruncatedLogNormal (see truncated_lognormal.hpp): `Type => IDistributionEnum.LogPearsonIII`
// reuses the REAL LogPearsonIII=4 enum value (confirmed by grep -- no distinct
// TruncatedLogPearson3 member exists in C#'s IDistributionEnum), so type() below is faithful. The
// FACTORY still needs a distinct key to construct this class, so this port adds a PORT-INTERNAL
// enum value, DistributionType::TruncatedLogPearson3 = 1006 (see i_distribution_enum.hpp) -- used
// only by distribution_type_from_name()/IDistributionFactory::create(), never returned by type().
//
// Unlike TruncatedLogNormal (which hardcodes _ProbabilityMin/_ProbabilityMax = 0.0/1.0
// unconditionally), TruncatedLogPearson3's ctor runs a real bounds-search
// (SetProbabilityRangeAndMinAndMax, ported below as set_probability_range_and_min_max): it calls
// this object's OWN cdf()/inverse_cdf() -- which internally branch on a `_Constructed`/
// `constructed_` flag that is still false while this search runs, so the "if (constructed_)"
// equality-shortcut/clamp branches in cdf()/inverse_cdf() are inert during the ctor and only
// become live for calls made after construction completes. `constructed_` is set true
// unconditionally at the very end of build_from_properties(), matching C#'s trailing
// `_Constructed = true;` (which runs whether or not Validate() reported errors -- but the bounds
// search itself is skipped entirely when HasErrors, leaving probability_min_/probability_max_ at
// their C# default-field values of 0.0/0.0, NOT 0.0/1.0 -- reproduced verbatim below).
//
// `Truncated` is set true unconditionally by the only ported ctor (the parameterless reflection
// ctor is not exposed via the factory and is not ported, matching every other Truncated* class in
// this port), so `Truncated && _Constructed` collapses to just `constructed_` in inverse_cdf()
// below -- not re-derived as a separate always-true field, same simplification already applied to
// TruncatedNormal/TruncatedLogNormal.
class TruncatedLogPearson3 : public ContinuousDistribution {
   public:
    // ported from: TruncatedLogPearson3.cs ctor(mean, standardDeviation, skew, min, max,
    // sampleSize=1) + BuildFromProperties(). addRules() runs before Validate() (matching C#'s
    // ctor ordering), then set_probability_range_and_min_max(min_, max_) runs only if validation
    // reported no errors, and constructed_ is set true unconditionally afterward.
    TruncatedLogPearson3(double mean, double standard_deviation, double skew, double min, double max,
                          long sample_size = 1)
        : mean_(mean), sd_(standard_deviation), skew_(skew), min_(min), max_(max) {
        this->sample_size_ = sample_size;
        add_rules();
        validate();
        if (!has_errors()) {
            set_probability_range_and_min_max(min_, max_);
        }
        constructed_ = true;
    }

    // ported from: TruncatedLogPearson3.cs `Type => IDistributionEnum.LogPearsonIII`. See class
    // comment -- this is the faithful C# value, the same real enum member LogPearson3 itself
    // returns, DELIBERATELY different from the port-internal factory key
    // DistributionType::TruncatedLogPearson3 used to construct this class.
    DistributionType type() const override { return DistributionType::LogPearsonIII; }
    double mean() const { return mean_; }
    double standard_deviation() const { return sd_; }
    double skewness() const { return skew_; }
    double min() const { return min_; }
    double max() const { return max_; }

    // ported from: TruncatedLogPearson3.cs PDF(double x): `double.Epsilon` outside [Min, Max]
    // (the smallest positive subnormal double, NOT machine epsilon -- std::numeric_limits<double>
    // ::denorm_min() is the exact C++ equivalent), otherwise PearsonIII(Mean,SD,Skew,SampleSize)
    // .PDF(log10(x)) / x / Math.Log(10), same log10-change-of-variables Jacobian as LogPearson3.
    double pdf(double x) const override {
        if (x < min_ || x > max_) return std::numeric_limits<double>::denorm_min();
        PearsonIII d(mean_, sd_, skew_, sample_size_);
        return d.pdf(std::log10(x)) / x / std::log(10.0);
    }

    // ported from: TruncatedLogPearson3.cs CDF(double x). When constructed_, x exactly at Min/Max
    // returns the pinned probability_min_/probability_max_ directly (avoids recomputing through
    // PearsonIII at the boundary). Otherwise clamps outside [Min, Max] to 0/1, then delegates to
    // PearsonIII.CDF(log10(x)) for x>0 (0 otherwise) -- same structure as LogPearson3::cdf.
    double cdf(double x) const override {
        if (constructed_) {
            if (x == min_) return probability_min_;
            if (x == max_) return probability_max_;
        }
        if (x < min_) return 0.0;
        if (x > max_) return 1.0;
        if (x > 0) {
            PearsonIII d(mean_, sd_, skew_, sample_size_);
            return d.cdf(std::log10(x));
        }
        return 0.0;
    }

    // ported from: TruncatedLogPearson3.cs InverseCDF(double p). `Truncated && _Constructed`
    // collapses to `constructed_` (see class comment). Once constructed_, p is remapped into
    // [probability_min_, probability_max_] and clamped to Min/Max at the remapped boundaries;
    // otherwise (i.e. while still inside the ctor's bounds search) p passes through unmodified.
    // The finite check throws exactly as C# (an out-of-[0,1] p can map to a non-finite remapped
    // value before this point). Builds a PearsonIII(Mean,SD,Skew,SampleSize) and returns
    // 10^d.InverseCDF(p) -- unlike LogPearson3 (which bypasses PearsonIII entirely via the
    // Wilson-Hilferty approximation), this class DOES delegate to PearsonIII::inverse_cdf.
    double inverse_cdf(double p) const override {
        if (constructed_) {
            p = probability_min_ + p * (probability_max_ - probability_min_);
        }
        if (!std::isfinite(p)) {
            throw std::invalid_argument(
                "The value of specified probability parameter: " + std::to_string(p) +
                " is invalid because it is not on the valid probability range: [0, 1].");
        }
        if (constructed_) {
            if (p <= probability_min_) return min_;
            if (p >= probability_max_) return max_;
        }
        PearsonIII d(mean_, sd_, skew_, sample_size_);
        return std::pow(10.0, d.inverse_cdf(p));
    }

    // ported from: TruncatedLogPearson3.cs Equals(IDistribution distribution). Checked
    // dynamic_cast, matching the `is TruncatedLogPearson3` real-runtime-type check (Type is
    // aliased to LogPearsonIII, so a type()-based gate would be wrong here -- same reasoning as
    // truncated_lognormal.hpp's equals()). Min/Max are NOT compared, matching C# verbatim.
    bool equals(const IDistribution& distribution) const override {
        const auto* other = dynamic_cast<const TruncatedLogPearson3*>(&distribution);
        if (other == nullptr) return false;
        return mean_ == other->mean_ && sd_ == other->sd_ && skew_ == other->skew_ &&
               sample_size() == other->sample_size();
    }

    // ported from: TruncatedLogPearson3.cs Fit(double[] sample): logs each sample value with
    // Math.Log10 (NO <=0 guard, unlike LogPearson3::fit -- transcribed verbatim, log10 of a
    // non-positive value simply produces NaN/-Infinity here and flows into SampleStatistics),
    // then returns `new TruncatedLogPearson3(stats.Mean, stats.StandardDeviation, stats.Skewness,
    // this.Min, this.Max, stats.SampleSize)`. Truncation bounds are preserved from the original
    // distribution, not refit from the sample (same as truncated_normal.hpp/
    // truncated_lognormal.hpp).
    std::unique_ptr<IDistribution> fit(const std::vector<double>& sample) const override {
        std::vector<double> logged(sample);
        for (double& v : logged) {
            v = std::log10(v);
        }
        hecfda::statistics::SampleStatistics stats(logged);
        return std::make_unique<TruncatedLogPearson3>(stats.mean(), stats.standard_deviation(),
                                                        stats.skewness(), min_, max_, stats.sample_size());
    }

   private:
    // ported from: TruncatedLogPearson3.cs SetProbabilityRangeAndMinAndMax(double min, double
    // max). Local `min`/`max` parameters shadow the min_/max_ members intentionally, matching the
    // C# local-parameter shadowing of the Min/Max properties. Calls this->cdf()/this->inverse_cdf()
    // (not PearsonIII directly) -- both are still pre-constructed_ at this point, so their
    // constructed_-gated branches are inert (see class comment).
    void set_probability_range_and_min_max(double min, double max) {
        double pmin = 0.0;
        const double epsilon = 1.0 / 1000000000.0;
        double pmax = 1.0 - pmin;
        if (std::isfinite(min) || std::isfinite(max)) {
            pmin = cdf(min);
            pmax = cdf(max);
        } else {
            pmin = 0.0000001;
            pmax = 1.0 - pmin;
            min = inverse_cdf(pmin);
            max = inverse_cdf(pmax);
        }
        while (!(std::isfinite(min) && std::isfinite(max))) {
            pmin += epsilon;
            pmax -= epsilon;
            if (!std::isfinite(min)) min = inverse_cdf(pmin);
            if (!std::isfinite(max)) max = inverse_cdf(pmax);
            if (pmin > 0.25) {
                throw std::runtime_error(
                    "The log Pearson III object is not constructable because 50% or more of its "
                    "distribution returns -Infinity and Infinity.");
            }
        }
        max_ = max;
        min_ = min;
        probability_min_ = pmin;
        probability_max_ = pmax;
    }

    // ported from: TruncatedLogPearson3.cs addRules().
    void add_rules() {
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

    double mean_, sd_, skew_, min_, max_;
    // ported from: TruncatedLogPearson3.cs `_ProbabilityMin`/`_ProbabilityMax` fields -- default
    // C# field values (0.0/0.0, NOT 0.0/1.0) when the bounds search never runs because Validate()
    // reported errors. Reproduced verbatim (not defaulted to 0.0/1.0 like TruncatedNormal's
    // probability_min_/probability_max_, which ARE always assigned by finite_range()).
    double probability_min_ = 0.0, probability_max_ = 0.0;
    // ported from: TruncatedLogPearson3.cs `_Constructed` field, gates the equality-shortcut/clamp
    // branches in cdf()/inverse_cdf() during the ctor's own bounds search (see class comment).
    bool constructed_ = false;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_TRUNCATED_LOGPEARSON3_HPP
