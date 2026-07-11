// ported from: HEC.FDA.Statistics/Distributions/TruncatedLogNormal.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_TRUNCATED_LOGNORMAL_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_TRUNCATED_LOGNORMAL_HPP
#include <cmath>
#include <memory>
#include <vector>
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/sample_statistics.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: TruncatedLogNormal.cs. Two real upstream quirks, reproduced verbatim:
//
// (1) `Type => IDistributionEnum.Normal` -- there is NO TruncatedLogNormal value in the C#
//     IDistributionEnum (confirmed by grep across every Distributions/*.cs Type getter). type()
//     below faithfully returns DistributionType::Normal. The FACTORY still needs a distinct key
//     to construct a TruncatedLogNormal, so this port adds a PORT-INTERNAL enum value,
//     DistributionType::TruncatedLogNormal = 1005 (see i_distribution_enum.hpp) -- used only by
//     distribution_type_from_name()/IDistributionFactory::create(), never returned by type().
//     The factory key (1005) and the instance type() (Normal) are intentionally different.
//
// (2) PDF/CDF ignore Mean/StandardDeviation entirely: `new Normal()` is the STANDARD normal
//     (mean=0, sd=1 -- see Normal.cs's parameterless ctor), so PDF(x)/CDF(x) always evaluate
//     `sn.PDF(Math.Log(x))` / `sn.CDF(Math.Log(x))` against ln(x) directly, regardless of this
//     distribution's own Mean/StandardDeviation. Only InverseCDF uses Mean/StandardDeviation.
//     Reproduced exactly (not "fixed" to use Mean/StandardDeviation in pdf/cdf).
//
// Additionally, unlike TruncatedNormal (which computes _ProbabilityMin/_ProbabilityMax via
// FiniteRange(min,max) -> this.cdf(min)/this.cdf(max)), TruncatedLogNormal's ctor hardcodes
// _ProbabilityMin = 0.0 and _ProbabilityMax = 1.0 UNCONDITIONALLY and never recomputes them.
// InverseCDF's `if (Truncated) p = _ProbabilityMin + p*(_ProbabilityMax - _ProbabilityMin);`
// remap is therefore always the identity `p' = p` (dead code, kept only as the surrounding
// clamp-to-[Min,Max] structure below -- not re-derived as an "identity is a no-op, so skip it"
// simplification, to keep the shape parallel to truncated_normal.hpp's inverse_cdf). Because
// ProbabilityMin/Max are always exactly 0.0/1.0, only p<=0 (-> Min) and p>=1 (-> Max) ever clamp;
// EVERY interior p in (0,1) returns the raw, UNCLAMPED `exp(Mean + StandardNormalInverseCDF(p) *
// StandardDeviation)` -- which can legitimately fall outside [Min, Max] for interior p. This is
// real upstream behavior (not a bug ported by accident) and is exercised by a fixture case below.
// The `Normal(0.0, 1.0)` local stands in for both `new Normal()` (pdf/cdf) and
// `Normal.StandardNormalInverseCDF` (inverse_cdf), matching the lognormal.hpp/truncated_normal.hpp
// precedent of reusing Normal rather than re-transcribing the standard-normal formulas.
class TruncatedLogNormal : public ContinuousDistribution {
   public:
    // ported from: TruncatedLogNormal.cs ctor(mean, sd, minValue, maxValue, sampleSize=1). Sets
    // Truncated=true and _ProbabilityMin/_ProbabilityMax = 0.0/1.0 unconditionally (see class
    // comment). The parameterless reflection ctor (which self-seeds Min/Max via InverseCDF at
    // p=1e-13/1-1e-13) is not exposed via the factory and is not ported, matching the
    // Normal/LogNormal/TruncatedNormal precedent of skipping unused reflection ctors.
    TruncatedLogNormal(double mean, double sd, double min_value, double max_value, long sample_size = 1)
        : mean_(mean), sd_(sd), min_(min_value), max_(max_value) {
        this->sample_size_ = sample_size;
        add_rules();
    }

    // ported from: TruncatedLogNormal.cs `Type => IDistributionEnum.Normal`. See class comment
    // (1) -- this is the faithful C# value, DELIBERATELY different from the port-internal factory
    // key DistributionType::TruncatedLogNormal used to construct this class.
    DistributionType type() const override { return DistributionType::Normal; }
    double mean() const { return mean_; }
    double standard_deviation() const { return sd_; }
    double min() const { return min_; }
    double max() const { return max_; }

    // ported from: TruncatedLogNormal.cs PDF(double x): `new Normal().PDF(Math.Log(x))`. Natural
    // log; standard normal (mean=0, sd=1) -- see class comment (2). Ignores mean_/sd_/min_/max_.
    double pdf(double x) const override {
        Normal standard_normal(0.0, 1.0);
        return standard_normal.pdf(std::log(x));
    }

    // ported from: TruncatedLogNormal.cs CDF(double x): `new Normal().CDF(Math.Log(x))`. Natural
    // log; standard normal -- see class comment (2). Ignores mean_/sd_/min_/max_.
    double cdf(double x) const override {
        Normal standard_normal(0.0, 1.0);
        return standard_normal.cdf(std::log(x));
    }

    // ported from: TruncatedLogNormal.cs InverseCDF(double p). See class comment for why the
    // `_ProbabilityMin + p*(_ProbabilityMax - _ProbabilityMin)` remap is always the identity here
    // (ProbabilityMin/Max are hardcoded 0.0/1.0, never computed from min_/max_ via cdf()). Only
    // p<=0 / p>=1 clamp to Min/Max; every interior p returns the raw, unclamped raw value.
    double inverse_cdf(double p) const override {
        double remapped = 0.0 + p * (1.0 - 0.0);
        if (remapped <= 0.0) return min_;
        if (remapped >= 1.0) return max_;
        Normal standard_normal(0.0, 1.0);
        return std::exp(mean_ + standard_normal.inverse_cdf(remapped) * sd_);
    }

    // ported from: TruncatedLogNormal.cs Equals(IDistribution distribution). Unlike
    // Normal/LogNormal/TruncatedNormal (which gate on `type() != distribution.type()`), the C#
    // here gates on the real runtime type via `!(distribution is TruncatedLogNormal)` -- using
    // the Type-property-based check would be wrong, since Type is aliased to Normal (a real
    // Normal instance would satisfy a `type()`-based equality gate). dynamic_cast reproduces C#'s
    // `is` operator faithfully.
    bool equals(const IDistribution& distribution) const override {
        const auto* other = dynamic_cast<const TruncatedLogNormal*>(&distribution);
        if (other == nullptr) return false;
        return mean_ == other->mean_ && sd_ == other->sd_ && sample_size() == other->sample_size();
    }

    // ported from: TruncatedLogNormal.cs Fit(double[] sample): logs the sample with Math.Log10
    // (LOG BASE 10 -- inconsistent with pdf/cdf/inverse_cdf's natural log/exp, kept as-is, same
    // quirk as lognormal.hpp) before computing SampleStatistics, then returns `new
    // TruncatedLogNormal(stats.Mean, stats.StandardDeviation, this.Min, this.Max,
    // stats.SampleSize)`. Truncation bounds are preserved from the original distribution, not
    // refit from the sample (same as truncated_normal.hpp). C# mutates the caller's `sample`
    // array in place; the C++ signature takes `sample` by const ref, so a local mutable copy is
    // log-transformed instead -- no behavioral difference for the returned distribution.
    std::unique_ptr<IDistribution> fit(const std::vector<double>& sample) const override {
        std::vector<double> logged(sample);
        for (double& v : logged) {
            v = std::log10(v);
        }
        hecfda::statistics::SampleStatistics stats(logged);
        return std::make_unique<TruncatedLogNormal>(stats.mean(), stats.standard_deviation(), min_, max_,
                                                      stats.sample_size());
    }

   private:
    // ported from: TruncatedLogNormal.cs addRules().
    void add_rules() {
        add_single_property_rule(
            "StandardDeviation", [this]() { return sd_ > 0; }, "Standard Deviation must be greater than 0.",
            ErrorLevel::Fatal);
        add_single_property_rule(
            "Mean", [this]() { return mean_ > 0; }, "Mean must be greater than 0.", ErrorLevel::Fatal);
        add_single_property_rule(
            "SampleSize", [this]() { return sample_size_ > 0; }, "SampleSize must be greater than 0.",
            ErrorLevel::Fatal);
    }

    double mean_, sd_, min_, max_;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_TRUNCATED_LOGNORMAL_HPP
