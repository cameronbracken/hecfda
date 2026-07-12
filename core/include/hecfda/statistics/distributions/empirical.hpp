// ported from: HEC.FDA.Statistics/Distributions/Empirical.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_EMPIRICAL_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_EMPIRICAL_HPP
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "hecfda/model/paired_data/dotnet_binary_search.hpp"
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/mathematics.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: Empirical.cs. `Type => IDistributionEnum.Empirical` (enum value 8, already declared
// in i_distribution_enum.hpp). `Truncated => false` is a SEALED override in C# -- always false, no
// consumer of Empirical ever sets it true -- so it is not exposed as a member here (matching
// continuous_distribution.hpp's DONE_WITH_CONCERNS note that `Truncated` has no consumer in the
// ported IDistribution surface); its one behavioral consequence (InverseCDF's dead remap branch)
// is documented at that call site below instead of transcribed as executable code.
//
// SEVERED (out of scope for this task; not needed by CDF/PDF/InverseCDF/Fit/summary-stats for a
// single instance):
//  - `StackEmpiricalDistributionsWeighted`: a static Model-layer AGGREGATION helper (weighted
//    combination of several Empirical instances). No Phase 6 caller needs the weighted variant
//    (`stack_empirical_distributions`/`fit_to_sample` below cover the two callers that do); port
//    later when a weighted-stacking caller lands.
//  - `WriteToXML` / `ReadFromXML`: reflection/XML (de)serialization, matching the XML scope already
//    excluded across this port (see continuous_distribution.hpp).
//  - `ComputeCumulativeFrequenciesForPlotting`: builds a fixed 250-bin CDF curve for UI plotting --
//    a graphing concern, matching `ToCoordinates` already excluded on ContinuousDistribution.
//  - `IsMonotonicallyIncreasing`: a public static utility that is never actually called anywhere in
//    Empirical.cs itself (AddRules() has only a `//TODO: Add rule to test if not monotonically
//    increasing` comment, never wired up) -- dead/unused in the ported surface, so not transcribed.
//  - The default parameterless `Empirical()` ctor: exists in C# only for reflection/XML
//    round-tripping (`ReadFromXML` etc., itself out of scope); not needed by any consumer of the
//    two data-bearing ctors.
//
// PDF has a genuine upstream QUIRK, transcribed verbatim (not "fixed"): it uses
// `Quantiles.ToList().IndexOf(x)` (an EXACT-value linear scan, .NET semantics: returns -1 -- always
// exactly -1, never an encoded insertion point -- when no element equals x) instead of
// `Array.BinarySearch` (used by CDF/InverseCDF). Because IndexOf's not-found sentinel is always -1,
// the "not found" branch's `index = -(index + 1)` always evaluates to 0, which unconditionally
// takes the `index == 0` "first value" path and returns 0.0. So PDF(x) returns 0.0 for ANY x that
// is not an EXACT match to one of the Quantiles values -- the "in between, interpolate" and "last
// value" branches of that path are provably unreachable dead code in both the C# original and this
// transcription. Kept for field-for-field fidelity; see the `pdf()` comment below.
//
// Equals also has a genuine upstream QUIRK, transcribed verbatim: `double[]` has no overloaded
// `operator==` in C#, so `Quantiles == distCompared.Quantiles` is REFERENCE equality, and
// `CumulativeProbabilities == distCompared.Quantiles` is a copy-paste bug (compares
// CumulativeProbabilities against the COMPARED object's Quantiles, never against
// distCompared.CumulativeProbabilities). See the `equals()` comment below for the C++ analogue.
class Empirical : public ContinuousDistribution {
   public:
    // ported from: Empirical.cs Empirical(double[] probabilities, double[] observationValues).
    // "The probabilities and observation values must be in ascending order, and are assumed to be
    // linked as coordinates. Counts should be equal." (upstream doc comment; not re-validated
    // here, matching C#). Min/Max are taken from the first/last Quantiles entries.
    Empirical(std::vector<double> probabilities, std::vector<double> observation_values)
        : cumulative_probabilities_(std::move(probabilities)), quantiles_(std::move(observation_values)) {
        min_ = quantiles_.front();
        max_ = quantiles_.back();
        build_from_properties();
        add_rules();
    }

    // ported from: Empirical.cs Empirical(double[] probabilities, double[] observationValues,
    // double min, double max). "Min and max should be equal to the first and last values in the
    // observationValues array" (upstream doc comment) -- but, unlike the two-array ctor, these are
    // caller-supplied rather than re-derived, which is what Fit()/FitToSample() rely on.
    Empirical(std::vector<double> probabilities, std::vector<double> observation_values, double min_value,
               double max_value)
        : cumulative_probabilities_(std::move(probabilities)),
          quantiles_(std::move(observation_values)),
          min_(min_value),
          max_(max_value) {
        build_from_properties();
        add_rules();
    }

    DistributionType type() const override { return DistributionType::Empirical; }

    double mean() const { return mean_; }
    double median() const { return median_; }
    double standard_deviation() const { return standard_deviation_; }
    double variance() const { return variance_; }
    double min() const { return min_; }
    double max() const { return max_; }
    // ported from: Empirical.cs SampleMean { get; set; }. NOT computed from CumulativeProbabilities
    // /Quantiles by this class -- upstream sets it externally (e.g. from the severed stacking
    // helpers' own sample-mean bookkeeping, or from a raw sample's mean before histogram binning).
    // Defaults to 0, matching the C# auto-property default.
    double sample_mean() const { return sample_mean_; }
    void set_sample_mean(double value) { sample_mean_ = value; }
    const std::vector<double>& cumulative_probabilities() const { return cumulative_probabilities_; }
    const std::vector<double>& quantiles() const { return quantiles_; }

    // ported from: Empirical.cs CDF(double x). Array.BinarySearch(Quantiles, x): exact hit returns
    // that index's CumulativeProbabilities entry directly; otherwise linearly interpolate between
    // the bracketing (Quantiles[index-1], CumulativeProbabilities[index-1]) and (Quantiles[index],
    // CumulativeProbabilities[index]) pairs by POSITION IN VALUE SPACE (weight computed from x
    // relative to the two bracketing Quantiles). Below the first Quantile -> 0.0; above the last ->
    // 1.0.
    double cdf(double x) const override {
        long index = hecfda::model::paired_data::dotnet_binary_search(quantiles_, x);
        if (index >= 0) {
            return cumulative_probabilities_[static_cast<std::size_t>(index)];
        }
        long size = sample_size_;
        index = -(index + 1);
        if (index == 0) {  // first value
            return 0.0;
        } else if (index < size) {
            std::size_t idx = static_cast<std::size_t>(index);
            double weight = (x - quantiles_[idx - 1]) / (quantiles_[idx] - quantiles_[idx - 1]);
            return (1.0 - weight) * cumulative_probabilities_[idx - 1] + weight * cumulative_probabilities_[idx];
        } else {  // last value
            return 1.0;
        }
    }

    // ported from: Empirical.cs PDF(double x). See the class-level comment above: the "not found"
    // path's index is always 0 (List<T>.IndexOf's not-found sentinel -(-1+1) == 0), so this returns
    // 0.0 for any x that isn't an EXACT match to a Quantiles entry. Transcribed verbatim, including
    // the unreachable `index < SampleSize` / final-else branches of that path.
    double pdf(double x) const override {
        long index = index_of_exact(quantiles_, x);
        if (index >= 0) {
            std::size_t idx = static_cast<std::size_t>(index);
            double pdf_left;
            if (index == 0) {  // first value
                pdf_left = 0.0;
            } else {
                pdf_left = (cumulative_probabilities_[idx] - cumulative_probabilities_[idx - 1]) /
                           (quantiles_[idx] - quantiles_[idx - 1]);
            }
            double pdf_right;
            if (index < sample_size_ - 1) {
                pdf_right = (cumulative_probabilities_[idx + 1] - cumulative_probabilities_[idx]) /
                            (quantiles_[idx + 1] - quantiles_[idx]);
            } else {  // last value
                pdf_right = 0.0;
            }
            return 0.5 * (pdf_left + pdf_right);
        } else {
            index = -(index + 1);  // always 0 in practice -- see class comment.
            if (index == 0) {      // first value (also: the only reachable case)
                return 0.0;
            } else if (index < sample_size_) {  // unreachable dead code, transcribed verbatim
                std::size_t idx = static_cast<std::size_t>(index);
                return (cumulative_probabilities_[idx] - cumulative_probabilities_[idx - 1]) /
                       (quantiles_[idx] - quantiles_[idx - 1]);
            } else {  // unreachable dead code, transcribed verbatim
                return 0.0;
            }
        }
    }

    // ported from: Empirical.cs InverseCDF(double p). Clamp/boundary order transcribed exactly:
    // ascending-CumulativeProbabilities check first, then the (provably dead, see class comment)
    // Truncated remap, then the IsFinite guard, then p<=min/p>=max clamps to Min/Max, then
    // Array.BinarySearch(CumulativeProbabilities, p) with the same exact-hit-vs-interpolate shape
    // as CDF but inverted (interpolating Quantiles by position in PROBABILITY space).
    double inverse_cdf(double p) const override {
        double cumulative_probs_min = cumulative_probabilities_.front();
        double cumulative_probs_max = cumulative_probabilities_.back();
        if (cumulative_probs_max < cumulative_probs_min) {
            throw std::runtime_error(
                "Cumulative Probabilities should always be ascending for an Empirical Distribution "
                "and were not.");
        }
        // ported from: `if (Truncated && _Constructed) { p = cumulativeProbsMin + (p) *
        // (cumulativeProbsMax - cumulativeProbsMin); }`. `Truncated` is a sealed override hardcoded
        // to `false` on Empirical, so this remap is PROVABLY DEAD CODE -- never applied for any
        // Empirical instance. Documented, not transcribed as executable code (see class comment).
        if (!std::isfinite(p)) {
            throw std::invalid_argument("The value of specified probability parameter: " + std::to_string(p) +
                                         " is invalid because it is not on the valid probability range: "
                                         "[0, 1].");
        } else if (p <= cumulative_probs_min) {
            return min_;
        } else if (p >= cumulative_probs_max) {
            return max_;
        } else {
            long index = hecfda::model::paired_data::dotnet_binary_search(cumulative_probabilities_, p);
            if (index >= 0) {
                return quantiles_[static_cast<std::size_t>(index)];
            }
            index = -(index + 1);
            if (index == 0) {  // first value
                return quantiles_.front();
            } else if (index < sample_size_) {
                std::size_t idx = static_cast<std::size_t>(index);
                double weight = (p - cumulative_probabilities_[idx - 1]) /
                                (cumulative_probabilities_[idx] - cumulative_probabilities_[idx - 1]);
                return (1.0 - weight) * quantiles_[idx - 1] + weight * quantiles_[idx];
            } else {  // last value
                return quantiles_[static_cast<std::size_t>(sample_size_ - 1)];
            }
        }
    }

    // ported from: Empirical.cs Equals(IDistribution distribution). See the class-level comment:
    // C# `double[]` has no `operator==`, so both array comparisons are REFERENCE equality, and the
    // second one is a copy-paste bug comparing CumulativeProbabilities against the OTHER object's
    // Quantiles. The closest C++ analogue of C# array-reference equality is buffer-pointer identity
    // (`data()`) -- reproduced here verbatim, bug included: `other->quantiles_.data()` appears on
    // BOTH sides, never `other->cumulative_probabilities_.data()`.
    bool equals(const IDistribution& distribution) const override {
        const auto* other = dynamic_cast<const Empirical*>(&distribution);
        if (other == nullptr) return false;
        return quantiles_.data() == other->quantiles_.data() &&
               cumulative_probabilities_.data() == other->quantiles_.data();
    }

    // ported from: Empirical.cs Fit(double[] sample). Sorts a COPY ascending, derives Min/Max from
    // the sorted extremes, and assigns probs[i] = i / count (i.e. probs[0] == 0.0 exactly) --
    // transcribed exactly, including that off-by-one-looking (but verbatim) probability spacing.
    std::unique_ptr<IDistribution> fit(const std::vector<double>& sample) const override {
        std::vector<double> sorted(sample);
        std::sort(sorted.begin(), sorted.end());
        auto count = static_cast<double>(sorted.size());
        std::vector<double> probs(sorted.size());
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            probs[i] = static_cast<double>(i) / count;
        }
        double min_value = sorted.front();
        double max_value = sorted.back();
        return std::make_unique<Empirical>(std::move(probs), std::move(sorted), min_value, max_value);
    }

    // ported from: Empirical.cs static FitToSample(List<double> sample) @
    // f63682a86a30dc306a105689714a92bfd95956c5. Deferred in Phase 4/5 alongside
    // StackEmpiricalDistributions (see class-level severance comment); restored in Phase 6 as the
    // refit step `stack_empirical_distributions` below needs. Algorithmically identical to the
    // instance `fit()` above (sort a copy ascending, probs[i] = i/count, min/max from the sorted
    // extremes) -- upstream carries both a static FitToSample and an instance Fit that do the same
    // thing; transcribed as two separate methods here too, matching the two distinct C# call
    // shapes (`Empirical.FitToSample(list)` vs `IDistribution.Fit(data)`).
    static Empirical fit_to_sample(const std::vector<double>& sample) {
        std::vector<double> sorted(sample);
        std::sort(sorted.begin(), sorted.end());
        auto count = static_cast<double>(sorted.size());
        std::vector<double> probs(sorted.size());
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            probs[i] = static_cast<double>(i) / count;
        }
        double min_value = sorted.front();
        double max_value = sorted.back();
        return Empirical(std::move(probs), std::move(sorted), min_value, max_value);
    }

    // ported from: Empirical.cs `Sum`/`Subtract` static combine helpers @
    // f63682a86a30dc306a105689714a92bfd95956c5, which upstream passes as a
    // `Func<double,double,double>` into StackEmpiricalDistributions. This port has exactly the two
    // call sites Phase 6 needs (damage aggregation across impact areas = sum; with/without-project
    // benefits = subtract), so they are represented as an enum + a combine() switch rather than a
    // std::function parameter.
    enum class StackOp { sum, subtract };

    // ported from: Empirical.cs static StackEmpiricalDistributions(List<Empirical>
    // empiricalDistributionsForStacking, Func<double,double,double> addOrSubtract) @
    // f63682a86a30dc306a105689714a92bfd95956c5. Deferred in Phase 4/5 (see class-level severance
    // comment), restored in Phase 6. Upstream samples every input distribution's InverseCDF at 2500
    // evenly-spaced quantile probabilities `(0.5 + i) / 2500` (Parallel.For over `i` -> serial `for`
    // here; each step is independent of every other step, so serializing changes nothing about the
    // result), combines the per-step values across inputs left-to-right via `addOrSubtract`
    // (distributions[0] op distributions[1] op distributions[2] op ...), separately combines the
    // inputs' SampleMean the same left-to-right way, refits a new Empirical from the combined
    // InverseCDF values via `fit_to_sample`, and finally force-sets the combined SampleMean onto the
    // refit result (fit_to_sample/FitToSample never sets SampleMean itself -- it stays at the
    // ctor-default 0.0 unless the caller sets it). The upstream `cumulativeProbabilities` output
    // array is computed but never read after the loop (dead output); not reproduced here since
    // nothing consumes it.
    static Empirical stack_empirical_distributions(const std::vector<Empirical>& distributions, StackOp op) {
        constexpr int probability_steps = 2500;
        std::vector<double> stacked_inverse_cdfs(static_cast<std::size_t>(probability_steps));
        for (int i = 0; i < probability_steps; ++i) {
            double probability_step = (0.5 + i) / probability_steps;
            double stacked_value = distributions[0].inverse_cdf(probability_step);
            for (std::size_t j = 1; j < distributions.size(); ++j) {
                stacked_value = combine(op, stacked_value, distributions[j].inverse_cdf(probability_step));
            }
            stacked_inverse_cdfs[static_cast<std::size_t>(i)] = stacked_value;
        }
        double stacked_mean = distributions[0].sample_mean();
        for (std::size_t j = 1; j < distributions.size(); ++j) {
            stacked_mean = combine(op, stacked_mean, distributions[j].sample_mean());
        }
        Empirical result = fit_to_sample(stacked_inverse_cdfs);
        result.set_sample_mean(stacked_mean);
        return result;
    }

   private:
    // ported from: Empirical.cs `Sum(double x1, double x2)` / `Subtract(double x1, double x2)`.
    static double combine(StackOp op, double x1, double x2) {
        switch (op) {
            case StackOp::sum:
                return x1 + x2;
            case StackOp::subtract:
                return x1 - x2;
        }
        throw std::invalid_argument("unknown Empirical::StackOp");
    }


    // ported from: Empirical.cs Array.BinarySearch(array, value), via the shared
    // hecfda::model::paired_data::dotnet_binary_search (see that header for full .NET semantics /
    // NaN-handling rationale). .NET semantics: if `value` is found, returns an index of a matching
    // element; if not found, returns the insertion point `lo` encoded as `~lo` (dotnet_binary_search's
    // return value), where `lo` is the index of the first element greater than `value` (or the
    // array size, if `value` exceeds every element). Every call site here recovers `lo` via
    // `index = -(index + 1)` (== `~index`; both encodings are equivalent since `~x == -x - 1`),
    // matching the C# call sites exactly.

    // ported from: Empirical.cs PDF's `Quantiles.ToList().IndexOf(x)`. .NET List<T>.IndexOf
    // semantics: linear scan for the first EXACT match; returns -1 (always exactly -1, never an
    // encoded insertion point) if none is found.
    static long index_of_exact(const std::vector<double>& values, double value) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (values[i] == value) return static_cast<long>(i);
        }
        return -1;
    }

    // ported from: Empirical.cs BuildFromProperties(). Computes SampleSize/Mean/Median/Variance/
    // StandardDeviation once, eagerly, at construction -- matching the C# ctor's own call sequence.
    void build_from_properties() {
        sample_size_ = static_cast<long>(quantiles_.size());
        mean_ = compute_mean();
        median_ = compute_median();
        variance_ = compute_variance();
        standard_deviation_ = std::sqrt(variance_);
    }

    // ported from: Empirical.cs ComputeMean(). Single-observation distributions skip the
    // Mathematics.IntegrateCDF quantile-function integral (E[X] = integral_0^1 Q(p) dp,
    // trapezoidal, padded to [0,1]) and return that one value directly.
    double compute_mean() const {
        if (sample_size_ == 0) return 0.0;
        if (quantiles_.size() == 1) return quantiles_[0];
        return hecfda::statistics::Mathematics::integrate_cdf(cumulative_probabilities_, quantiles_);
    }

    // ported from: Empirical.cs ComputeMedian(). This is the median of the Quantiles ARRAY itself
    // (positional middle element(s)), NOT an InverseCDF(0.5) probability-weighted lookup.
    double compute_median() const {
        if (sample_size_ == 0) {
            throw std::invalid_argument("Sample cannot be null");
        } else if (sample_size_ == 1) {
            return quantiles_[static_cast<std::size_t>(sample_size_ - 1)];
        } else if ((sample_size_ % 2) == 0) {
            std::size_t hi = static_cast<std::size_t>(sample_size_ / 2);
            return (quantiles_[hi] + quantiles_[hi - 1]) / 2.0;
        } else {
            return quantiles_[static_cast<std::size_t>((sample_size_ - 1) / 2)];
        }
    }

    // ported from: Empirical.cs ComputeVariance() (private). Piecewise E[X^2] accumulation over the
    // (CumulativeProbabilities, Quantiles) step function -- a left singleton step from
    // (0, Quantiles[0]), interior trapezoid-in-probability steps `(l^2 + l*r + r^2)/3` weighted by
    // the probability-mass step, and a right singleton step out to probability 1.0 -- then
    // `Var = E[X^2] - Mean^2`. Transcribed with the same variable roles (valL/valR/cdfL/cdfR/
    // stepPDF/stepVal -> val_l/val_r/cdf_l/cdf_r/step_pdf/step_val).
    double compute_variance() const {
        if (sample_size_ == 0) return 0.0;
        if (sample_size_ == 1) return 0.0;
        double mean = mean_;
        double expect2 = 0.0;
        double step_pdf, step_val;
        double val_l, val_r, cdf_l, cdf_r;
        // add left singleton
        val_r = quantiles_[0];
        cdf_r = cumulative_probabilities_[0];
        step_pdf = cdf_r - 0.0;
        expect2 += val_r * val_r * step_pdf;
        val_l = val_r;
        cdf_l = cdf_r;
        // add interval values
        for (long i = 1; i < sample_size_; ++i) {
            std::size_t idx = static_cast<std::size_t>(i);
            val_r = quantiles_[idx];
            cdf_r = cumulative_probabilities_[idx];
            step_pdf = cdf_r - cdf_l;
            step_val = (val_l * val_l + val_l * val_r + val_r * val_r) / 3.0;
            expect2 += step_val * step_pdf;
            val_l = val_r;
            cdf_l = cdf_r;
        }
        // add last singleton
        val_r = quantiles_[static_cast<std::size_t>(sample_size_ - 1)];
        cdf_r = 1.0;
        step_pdf = cdf_r - cdf_l;
        expect2 += val_r * step_pdf;
        return expect2 - mean * mean;
    }

    // ported from: Empirical.cs AddRules(). The `//TODO: Add rule to test if not monotonically
    // increasing` comment is not wired to a real rule in C# either -- only the SampleSize rule
    // actually registers.
    void add_rules() {
        add_single_property_rule(
            "SampleSize", [this]() { return sample_size_ > 0; }, "SampleSize must be greater than 0.",
            ErrorLevel::Fatal);
    }

    std::vector<double> cumulative_probabilities_;
    std::vector<double> quantiles_;
    double min_ = 0.0;
    double max_ = 0.0;
    double mean_ = 0.0;
    double median_ = 0.0;
    double standard_deviation_ = 0.0;
    double variance_ = 0.0;
    double sample_mean_ = 0.0;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_EMPIRICAL_HPP
