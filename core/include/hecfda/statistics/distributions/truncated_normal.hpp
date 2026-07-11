// ported from: HEC.FDA.Statistics/Distributions/TruncatedNormal.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_TRUNCATED_NORMAL_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_TRUNCATED_NORMAL_HPP
#include <cmath>
#include <memory>
#include <vector>
#include "hecfda/constants.hpp"
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/sample_statistics.hpp"
#include "hecfda/statistics/special_functions.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: TruncatedNormal.cs. C# declares `TruncatedNormal : Normal`, but TruncatedNormal
// re-declares its OWN `Mean`/`StandardDeviation` properties with `new` (hiding, not overriding,
// the base Normal ones). Because the parameterized ctor never calls `: base(mean, sd, ...)`, C#
// implicitly runs the PARAMETERLESS `Normal()` base ctor first, which sets the base's own hidden
// Mean=0/StandardDeviation=1.0 and registers Normal.AddRules()'s 3 rules bound to those
// now-permanently-inert base fields (never touched again). Those base rules always evaluate true
// (1.0 >= 0, 1.0 > 0) and never affect HasErrors/ErrorLevel for any real construction -- they are
// dead weight. This C++ port therefore derives directly from ContinuousDistribution (not Normal)
// with its own mean_/sd_ fields, and registers only TruncatedNormal.addRules()'s own 2 rules --
// reproducing the OBSERVABLE C# behavior exactly (porting the base Normal rules against the real
// field would over-count validation errors upstream never reports). A local `Normal(0.0, 1.0)`
// standard-normal instance stands in for the C# `Normal.StandardNormalInverseCDF` static helper,
// matching the byte-for-byte reuse already established by lognormal.hpp.
class TruncatedNormal : public ContinuousDistribution {
   public:
    // ported from: TruncatedNormal.cs ctor(mean, sd, minValue, maxValue, sampleSize=1). Sets
    // Truncated=true unconditionally (matching the C# parameterized ctor -- the parameterless
    // reflection ctor, which leaves Truncated unset/false, is not exposed via the factory and is
    // not ported, matching the Normal/LogNormal precedent of skipping unused reflection ctors).
    // FiniteRange(min, max) computes _ProbabilityMin/_ProbabilityMax before addRules(), as in C#.
    TruncatedNormal(double mean, double sd, double min_value, double max_value, long sample_size = 1)
        : mean_(mean), sd_(sd), min_(min_value), max_(max_value) {
        this->sample_size_ = sample_size;
        finite_range(min_, max_);
        add_rules();
    }

    DistributionType type() const override { return DistributionType::TruncatedNormal; }
    double mean() const { return mean_; }
    double standard_deviation() const { return sd_; }
    double min() const { return min_; }
    double max() const { return max_; }

    // ported from: TruncatedNormal.cs PDF(double x). NOTE: verbatim upstream quirk -- this is the
    // PLAIN (untruncated) normal density formula. It does not renormalize by
    // (ProbabilityMax - ProbabilityMin), does not zero out density outside [Min, Max], and (unlike
    // Normal.pdf) has no sd_==0 guard. Reproduced exactly as the C# override, not "fixed."
    double pdf(double x) const override {
        return std::exp(-(x - mean_) * (x - mean_) / (2.0 * sd_ * sd_)) /
               (std::sqrt(2.0 * hecfda::kPi) * sd_);
    }

    // ported from: TruncatedNormal.cs CDF(double x). Also verbatim the plain (untruncated) normal
    // CDF -- only the +/-infinity guards, no sd_==0 guard (unlike Normal.cdf).
    double cdf(double x) const override {
        if (std::isinf(x)) {
            return x > 0 ? 1.0 : 0.0;
        }
        double g =
            SpecialFunctions::reg_incomplete_gamma(0.5, (x - mean_) * (x - mean_) / (2.0 * sd_ * sd_));
        return x >= mean_ ? 0.5 * (1.0 + g) : 0.5 * (1.0 - g);
    }

    // ported from: TruncatedNormal.cs InverseCDF(double p). The truncation renormalization
    // (https://en.wikipedia.org/wiki/Truncated_normal_distribution): remap p into
    // [_ProbabilityMin, _ProbabilityMax] via `p' = ProbabilityMin + p*(ProbabilityMax -
    // ProbabilityMin)`, clamp to [Min, Max] at the remapped boundaries, otherwise evaluate the
    // untruncated Normal.StandardNormalInverseCDF at p' and rescale by (mean_, sd_). `Truncated`
    // is always true for this (the only ported) ctor, so the remap always applies -- matching
    // `if (Truncated) { p = _ProbabilityMin + (p) * (_ProbabilityMax - _ProbabilityMin); }`.
    double inverse_cdf(double p) const override {
        double remapped = probability_min_ + p * (probability_max_ - probability_min_);
        if (remapped <= probability_min_) return min_;
        if (remapped >= probability_max_) return max_;
        Normal standard_normal(0.0, 1.0);
        return mean_ + standard_normal.inverse_cdf(remapped) * sd_;
    }

    // ported from: TruncatedNormal.cs Equals(IDistribution distribution). NOTE: verbatim
    // transcription of a real upstream dead-code quirk (mirroring the "TODO" already flagged on
    // Normal.cs's own Equals) -- once Type/SampleSize/Mean/StandardDeviation all match, the C#
    // method falls through to an unconditional `return true`; the nested Truncated/Min/Max
    // comparisons can never change the result (every failure path inside that nesting just skips
    // to the same trailing `return true`). Reproduced by omitting the unreachable comparisons
    // rather than writing branches that can never evaluate false.
    bool equals(const IDistribution& distribution) const override {
        if (type() != distribution.type()) return false;
        const auto& other = static_cast<const TruncatedNormal&>(distribution);
        return sample_size() == other.sample_size() && mean_ == other.mean_ && sd_ == other.sd_;
    }

    // ported from: TruncatedNormal.cs Fit(double[] sample) -> `new TruncatedNormal(stats.Mean,
    // stats.StandardDeviation, this.Min, this.Max, stats.SampleSize)`. Truncation bounds are
    // preserved from the original distribution, not refit from the sample.
    std::unique_ptr<IDistribution> fit(const std::vector<double>& sample) const override {
        hecfda::statistics::SampleStatistics stats(sample);
        return std::make_unique<TruncatedNormal>(stats.mean(), stats.standard_deviation(), min_, max_,
                                                   stats.sample_size());
    }

   private:
    // ported from: TruncatedNormal.cs FiniteRange(double min, double max). pmin/pmax default to
    // 0/1 (the full-range identity) unless at least one bound is finite, in which case both are
    // recomputed via this object's own cdf(). Matches `min.IsFinite() || max.IsFinite()`.
    void finite_range(double min_value, double max_value) {
        double pmin = 0.0;
        double pmax = 1.0 - pmin;
        if (std::isfinite(min_value) || std::isfinite(max_value)) {
            pmin = cdf(min_value);
            pmax = cdf(max_value);
        }
        probability_min_ = pmin;
        probability_max_ = pmax;
    }

    // ported from: TruncatedNormal.cs addRules() -- the TruncatedNormal-local rules only. See the
    // class comment above for why the base Normal.AddRules() rules are not ported (they are inert
    // in C# due to field hiding and would over-count errors if bound to the real sd_ field here).
    void add_rules() {
        add_single_property_rule(
            "StandardDeviation", [this]() { return sd_ > 0; }, "Standard Deviation must be greater than 0.",
            ErrorLevel::Fatal);
        add_single_property_rule(
            "SampleSize", [this]() { return sample_size_ > 0; }, "SampleSize must be greater than 0.",
            ErrorLevel::Fatal);
    }

    double mean_, sd_, min_, max_;
    double probability_min_ = 0.0, probability_max_ = 1.0;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_TRUNCATED_NORMAL_HPP
