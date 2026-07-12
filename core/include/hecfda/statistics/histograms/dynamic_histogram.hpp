// ported from: HEC.FDA.Statistics/Histograms/DynamicHistogram.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_HISTOGRAMS_DYNAMIC_HISTOGRAM_HPP
#define HECFDA_STATISTICS_HISTOGRAMS_DYNAMIC_HISTOGRAM_HPP
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/distributions/empirical.hpp"
#include "hecfda/statistics/distributions/i_distribution.hpp"
#include "hecfda/statistics/distributions/i_distribution_enum.hpp"
#include "hecfda/statistics/histograms/i_histogram.hpp"
namespace hecfda {
namespace statistics {
namespace histograms {

// ported from: DynamicHistogram.cs `public class DynamicHistogram : IHistogram`. The Monte Carlo
// result accumulator the compute engine bins simulation results into (the "heart" of the port).
// Two data tracks coexist and MUST NOT be conflated:
//   1. The RAW running moments -- SampleMean/SampleVariance/StandardDeviation/SampleMin/SampleMax
//      -- updated per observation via a Welford-style online scheme (see add_observation..).
//   2. The BINNED histogram -- bin_counts_ over [min_, max_) at width bin_width_ -- from which
//      HistogramMean()/HistogramVariance() and PDF/CDF/InverseCDF are computed.
//
// Base class note: the pinned upstream declares `class DynamicHistogram : IHistogram` (IHistogram
// itself extends IDistribution). Per the C2 task brief this port derives from ContinuousDistribution
// (for the IDistribution + Validation surface, and to reuse its Sample(packet)/sample_size_) plus
// the histogram-only IHistogram interface. ContinuousDistribution::Sample(packet) is byte-identical
// to DynamicHistogram.Sample (build samples[i]=InverseCDF(packet[i]) for i<SampleSize, return
// Fit(samples)), so it is inherited rather than re-implemented. SampleSize is stored in the
// inherited protected sample_size_ (initialized to 0 here, not the base default of 1).
//
// convert_to_int64 / convert_to_int32: C# Convert.ToInt64/ToInt32(double) round HALF-TO-EVEN
// (banker's rounding), NOT truncation. This is load-bearing in ResetToZeroMin, where
// `Convert.ToInt64(lowerFraction * BinCounts[i])` splits a bin's count across two new bins by a
// non-integer fraction. std::nearbyint reproduces round-half-to-even under the default FP rounding
// mode. (The Convert.ToInt32(Math.Ceiling(..))/ToInt64(Math.Floor(..)) call sites already pass
// integer-valued doubles, so rounding mode is immaterial there; they use plain static_cast.)
//
// DONE_WITH_CONCERNS (scoped out, not ported):
//  - ToXML() / ReadFromXML(XElement) and the private (min,max,binWidth,sampleSize,binCounts,cc)
//    ctor it feeds: XML (de)serialization, no equivalent surface in this port (matches the
//    repo-wide XML severance in convergence_criteria.hpp / i_distribution.hpp). Callers
//    reconstruct a histogram via the public numeric ctors instead.
//  - HistogramDebugVisualizer: there is no such member in the pinned source; the debug/GUI
//    visualization surface referenced in the task lives elsewhere (WPF), nothing to sever here.
//  - The parameterless `DynamicHistogram()` "ARBITRARY histogram" ctor (adds ten 0-observations):
//    a serialization/placeholder helper, not a data-collection surface. Not ported.
class DynamicHistogram : public distributions::ContinuousDistribution, public IHistogram {
   public:
    // ported from: DynamicHistogram.cs field constants.
    static constexpr int MAX_BIN_COUNT = 500;
    static constexpr int OVERFLOW_PREVENTION_THRESHOLD = 2000;
    static constexpr double DEFAULT_BIN_WIDTH = .0001;

    // ported from: DynamicHistogram(double min, double binWidth, ConvergenceCriteria).
    DynamicHistogram(double min, double bin_width, ConvergenceCriteria convergence_criteria)
        : convergence_criteria_(std::move(convergence_criteria)) {
        bin_width_ = bin_width;
        min_ = min;
        max_ = min_ + bin_width_;
        bin_counts_.assign(1, 0);
        sample_size_ = 0;
    }

    // ported from: DynamicHistogram(double binWidth, ConvergenceCriteria). Leaves bin_counts_ empty
    // and defers the first Min/Max until the first observation arrives (min_has_not_been_set_).
    DynamicHistogram(double bin_width, ConvergenceCriteria convergence_criteria)
        : convergence_criteria_(std::move(convergence_criteria)) {
        bin_width_ = bin_width;
        min_has_not_been_set_ = true;
        sample_size_ = 0;
    }

    // ported from: DynamicHistogram(List<double> dataList, ConvergenceCriteria). Sturges'-rule bin
    // count (ceil(1 + 3.322*log10(N))); a zero-range sample collapses to DEFAULT_BIN_WIDTH.
    DynamicHistogram(const std::vector<double>& data, ConvergenceCriteria convergence_criteria)
        : convergence_criteria_(std::move(convergence_criteria)) {
        min_ = *std::min_element(data.begin(), data.end());
        max_ = *std::max_element(data.begin(), data.end());
        int quantity_of_bins = static_cast<int>(
            std::ceil(1 + 3.322 * std::log10(static_cast<double>(data.size()))));
        double range = max_ - min_;
        if (range == 0) {
            bin_width_ = DEFAULT_BIN_WIDTH;
        } else {
            bin_width_ = range / quantity_of_bins;
        }
        bin_counts_.assign(static_cast<std::size_t>(quantity_of_bins), 0);
        sample_size_ = 0;
        add_observations_to_histogram(data);
    }

    // ported from: DynamicHistogram(double min, double binWidth, long[] binCounts, double
    // sampleMean, double sampleVariance, double sampleMin, double sampleMax, ConvergenceCriteria).
    DynamicHistogram(double min, double bin_width, std::vector<std::int64_t> bin_counts,
                     double sample_mean, double sample_variance, double sample_min,
                     double sample_max, ConvergenceCriteria convergence_criteria)
        : convergence_criteria_(std::move(convergence_criteria)) {
        min_ = min;
        bin_width_ = bin_width;
        bin_counts_ = std::move(bin_counts);
        max_ = min_ + static_cast<double>(bin_counts_.size()) * bin_width_;
        std::int64_t total = 0;
        for (std::int64_t count : bin_counts_) total += count;
        sample_size_ = total;
        sample_mean_ = sample_mean;
        sample_variance_ = sample_variance;
        sample_min_ = sample_min;
        sample_max_ = sample_max;
    }

    // ---- IDistribution surface ----------------------------------------------------------------

    distributions::DistributionType type() const override {
        return distributions::DistributionType::IHistogram;
    }

    // ported from: DynamicHistogram.cs PDF(double x).
    double pdf(double x) const override {
        if (sample_size_ == 0) return std::numeric_limits<double>::quiet_NaN();
        if (min_ == (max_ - bin_width_)) {
            if (x > min_) {
                if (x <= max_) return 1.0;
            }
            return 0.0;
        }
        double n_at_x = static_cast<double>(find_bin_count(x, false));
        double n = static_cast<double>(sample_size_);
        return n_at_x / n;
    }

    // ported from: DynamicHistogram.cs CDF(double x).
    double cdf(double x) const override {
        if (sample_size_ == 0) return std::numeric_limits<double>::quiet_NaN();
        if (min_ == (max_ - bin_width_)) {
            if (x > min_) {
                if (x <= max_) {
                    return (max_ - x) / (max_ - min_);
                } else {
                    return 1.0;
                }
            }
            return 0.0;
        }
        double n_at_x = static_cast<double>(find_bin_count(x));
        double n = static_cast<double>(sample_size_);
        return n_at_x / n;
    }

    // ported from: DynamicHistogram.cs InverseCDF(double p). C# `p.IsOnRange(0, 1)` is inclusive on
    // both ends (ExtensionMethods.cs IsOnRange defaults inclusiveMin=inclusiveMax=true), so only
    // p<0 or p>1 throws.
    double inverse_cdf(double p) const override {
        if (p < 0 || p > 1) {
            throw std::invalid_argument("The provided probability value: " + std::to_string(p) +
                                         " is not on the a valid range: [0, 1]");
        }
        if (histogram_is_zero_valued()) return 0.0;
        if (histogram_is_single_valued()) return sample_mean_;
        if (sample_size_ == 0) return std::numeric_limits<double>::quiet_NaN();
        if (min_ == (max_ - bin_width_)) return min_ + (bin_width_ * p);
        if (p == 0) return min_;
        if (p == 1) return max_;
        int numobs = convert_to_int32(static_cast<double>(sample_size_) * p);
        if (p <= 0.5) {
            int index = 0;
            double obs = static_cast<double>(bin_counts_[0]);
            double cobs = obs;
            while (cobs < numobs) {
                index++;
                obs = static_cast<double>(bin_counts_[static_cast<std::size_t>(index)]);
                cobs += obs;
            }
            double fraction;
            if (obs == 0) {
                fraction = .5;
            } else {
                fraction = (cobs - numobs) / obs;
            }
            double bin_offset = static_cast<double>(index + 1);
            return min_ + bin_width_ * bin_offset - bin_width_ * fraction;
        } else {
            int index = static_cast<int>(bin_counts_.size()) - 1;
            double obs = static_cast<double>(bin_counts_[static_cast<std::size_t>(index)]);
            double cobs = static_cast<double>(sample_size_) - obs;
            while (cobs > numobs) {
                index--;
                obs = static_cast<double>(bin_counts_[static_cast<std::size_t>(index)]);
                cobs -= obs;
            }
            double fraction;
            if (obs == 0) {
                fraction = .5;
            } else {
                fraction = (numobs - cobs) / obs;
            }
            double bin_offset = static_cast<double>(static_cast<int>(bin_counts_.size()) - index);
            return max_ - bin_width_ * bin_offset + bin_width_ * fraction;
        }
    }

    // ported from: DynamicHistogram.cs static ConvertToEmpiricalDistribution(IHistogram histogram) @
    // f63682a86a30dc306a105689714a92bfd95956c5. Deferred in Phase 4/5 (see the class-level
    // DONE_WITH_CONCERNS note this bullet used to live under -- now restored in Phase 6 as the
    // binned-histogram -> Empirical bridge the quantile/benefits chain needs). Upstream's static
    // method takes an `IHistogram histogram` parameter but every call site invokes it as
    // `ConvertToEmpiricalDistribution(someHistogram)` on the histogram itself, so it is ported as an
    // instance method on `this` (matching this port's existing pattern for other IHistogram members,
    // e.g. `is_histogram_converged`). Sweeps InverseCDF at the same 2500 evenly-spaced quantile
    // probabilities `(i + 0.5) / 2500` as Empirical::stack_empirical_distributions (a different
    // upstream method that happens to use the same probability-step formula and step count),
    // builds a new Empirical from (probabilities, values) via the two-array ctor (which derives
    // Min/Max from the first/last Quantiles entries -- InverseCDF is non-decreasing in p, so the
    // swept values are already ascending), then force-sets SampleMean from this histogram's own raw
    // running SampleMean (NOT recomputed by Empirical's ctor -- see Empirical.cs's own SampleMean
    // doc comment: "must be set from outside this class").
    distributions::Empirical convert_to_empirical_distribution() const {
        constexpr int probability_steps = 2500;
        std::vector<double> cumulative_probabilities(static_cast<std::size_t>(probability_steps));
        std::vector<double> inverse_cdfs(static_cast<std::size_t>(probability_steps));
        for (int i = 0; i < probability_steps; ++i) {
            double probability_step = (static_cast<double>(i) + 0.5) / probability_steps;
            cumulative_probabilities[static_cast<std::size_t>(i)] = probability_step;
            inverse_cdfs[static_cast<std::size_t>(i)] = inverse_cdf(probability_step);
        }
        distributions::Empirical new_empirical(cumulative_probabilities, inverse_cdfs);
        new_empirical.set_sample_mean(sample_mean_);
        return new_empirical;
    }

    // ported from: DynamicHistogram.cs Equals(IDistribution distribution). The C# `distribution.Type
    // != IHistogram` guard is expressed here as a dynamic_cast to DynamicHistogram (matching the
    // repo's Equals idiom, e.g. Normal::equals). Field comparisons transcribed in the same order,
    // early-return-on-mismatch. NOTE: like the C#, the bin_counts loop is bounded by THIS object's
    // bin count and indexes the other object without a length guard -- faithful to upstream.
    bool equals(const distributions::IDistribution& distribution) const override {
        const auto* other = dynamic_cast<const DynamicHistogram*>(&distribution);
        if (other == nullptr) return false;
        if (!convergence_criteria_.equals(other->convergence_criteria_)) return false;
        for (std::size_t i = 0; i < bin_counts_.size(); i++) {
            if (bin_counts_[i] != other->bin_counts_[i]) return false;
        }
        if (min_ != other->min_) return false;
        if (bin_width_ != other->bin_width_) return false;
        if (sample_size_ != other->sample_size_) return false;
        if (max_ != other->max_) return false;
        if (is_converged_ != other->is_converged_) return false;
        if (converged_iteration_ != other->converged_iteration_) return false;
        return true;
    }

    // ported from: DynamicHistogram.cs Fit(double[] data) -> new DynamicHistogram(data.ToList(),
    // this.ConvergenceCriteria).
    std::unique_ptr<distributions::IDistribution> fit(const std::vector<double>& data) const override {
        return std::make_unique<DynamicHistogram>(data, convergence_criteria_);
    }

    // Sample(packet) is inherited from ContinuousDistribution (identical to DynamicHistogram.Sample).

    // ported from: DynamicHistogram.cs Truncated getter (always false; truncation is not
    // implemented upstream). Kept as a plain method -- IDistribution's Truncated was severed
    // repo-wide (see i_distribution.hpp).
    bool truncated() const { return false; }

    // ---- IHistogram surface -------------------------------------------------------------------

    bool is_converged() const override { return is_converged_; }
    bool histogram_is_zero_valued() const override { return is_zero_valued(); }
    bool histogram_is_single_valued() const override { return is_single_valued(); }
    std::int64_t converged_iteration() const override { return converged_iteration_; }
    double bin_width() const override { return bin_width_; }
    const std::vector<std::int64_t>& bin_counts() const override { return bin_counts_; }
    double min() const override { return min_; }
    double max() const override { return max_; }
    double sample_mean() const override { return sample_mean_; }
    double sample_max() const { return sample_max_; }
    double sample_min() const { return sample_min_; }

    // ported from: DynamicHistogram.cs SampleVariance getter -- the raw running variance rescaled
    // by (SampleSize-1)/SampleSize (i.e. _SampleVariance holds the divide-by-(n-1) accumulator and
    // this getter converts it to the population/divide-by-n moment).
    double sample_variance() const override {
        return sample_variance_ *
               (static_cast<double>(sample_size_ - 1) / static_cast<double>(sample_size_));
    }

    // ported from: DynamicHistogram.cs StandardDeviation getter (Math.Pow(SampleVariance, 0.5)).
    double standard_deviation() const override { return std::pow(sample_variance(), 0.5); }

    const ConvergenceCriteria& convergence_criteria() const override { return convergence_criteria_; }

    // ported from: DynamicHistogram.cs ForceDeQueue() -- a no-op HACK to satisfy the interface.
    void force_de_queue() override {}

    // ported from: DynamicHistogram.cs AddObservationToHistogram(double observation, Int64 index=0).
    // The `index` param is a documented interface HACK (unused when collecting into a
    // DynamicHistogram) -- transcribed verbatim including its irrelevance to the logic below.
    void add_observation_to_histogram(double observation, std::int64_t index = 0) override {
        (void)index;
        if (histogram_shut_down_) {
            // do nothing because the histogram is shut down
            return;
        }
        // First observation: seed the raw moments and (if deferred) the bin frame.
        if (sample_size_ == 0) {
            sample_max_ = observation;
            sample_min_ = observation;
            sample_mean_ = observation;
            sample_variance_ = 0;
            if (min_has_not_been_set_) {
                min_ = observation;
                max_ = observation + bin_width_;
                bin_counts_.assign(1, 0);
            }
            sample_size_ = 1;
        } else {
            // Welford-style online update, transcribed EXACTLY (order + rescale factor matter):
            //   tmpMean         = mean + (x - mean)/n
            //   delta           = x - mean            (uses the OLD mean, before tmpMean)
            //   _SampleVariance = ((n-2)/(n-1))*_SampleVariance + (delta*delta)/n
            //   mean            = tmpMean             (assigned LAST)
            if (observation > sample_max_) sample_max_ = observation;
            if (observation < sample_min_) sample_min_ = observation;
            sample_size_ += 1;
            double tmp_mean =
                sample_mean_ + ((observation - sample_mean_) / static_cast<double>(sample_size_));
            double delta = observation - sample_mean_;
            sample_variance_ =
                (((static_cast<double>(sample_size_ - 2) / static_cast<double>(sample_size_ - 1)) *
                  sample_variance_) +
                 (delta * delta) / static_cast<double>(sample_size_));
            sample_mean_ = tmp_mean;
        }
        ensure_capacity_for_observation(observation);
        int quantity_additional_bins;
        if (observation < min_) {
            quantity_additional_bins =
                static_cast<int>(std::ceil((min_ - observation) / bin_width_));
            if (observation == 0) {
                reset_to_zero_min(quantity_additional_bins);
            } else {
                double new_min = min_ - (quantity_additional_bins * bin_width_);
                if (observation > 0 && new_min < 0) {
                    reset_to_zero_min(quantity_additional_bins);
                } else {
                    // Grow below Min: allocate q extra bins at the front, shift existing counts up
                    // by q, drop the new observation into bin 0, and lower Min by q*BinWidth.
                    std::size_t old_len = bin_counts_.size();
                    std::vector<std::int64_t> new_bin_counts(
                        static_cast<std::size_t>(quantity_additional_bins) + old_len, 0);
                    for (std::int64_t i = static_cast<std::int64_t>(old_len) + quantity_additional_bins - 1;
                         i > (quantity_additional_bins - 1); i--) {
                        new_bin_counts[static_cast<std::size_t>(i)] =
                            bin_counts_[static_cast<std::size_t>(i - quantity_additional_bins)];
                    }
                    bin_counts_ = std::move(new_bin_counts);
                    bin_counts_[0] += 1;
                    min_ = new_min;
                }
            }
        } else if (observation > max_) {
            // Grow above Max: allocate q extra bins at the end, keep existing counts in place, drop
            // the new observation into the last bin, and raise Max to Min + BinCounts.Length*Width.
            quantity_additional_bins =
                static_cast<int>(std::ceil((observation - max_ + bin_width_) / bin_width_));
            std::size_t old_len = bin_counts_.size();
            std::vector<std::int64_t> new_bin_counts(
                static_cast<std::size_t>(quantity_additional_bins) + old_len, 0);
            for (std::size_t i = 0; i < old_len; i++) {
                new_bin_counts[i] = bin_counts_[i];
            }
            new_bin_counts[old_len + static_cast<std::size_t>(quantity_additional_bins) - 1] += 1;
            bin_counts_ = std::move(new_bin_counts);
            max_ = min_ + (static_cast<double>(bin_counts_.size()) * bin_width_);
        } else {
            std::int64_t new_obs_index = 0;
            if (observation != min_) {
                new_obs_index = static_cast<std::int64_t>(std::floor((observation - min_) / bin_width_));
            }
            // Landing exactly on Max needs one more bin (the [Max, Max+Width) bin) before indexing.
            if (observation == max_) {
                std::vector<std::int64_t> new_bin_counts(bin_counts_.size() + 1, 0);
                for (std::size_t i = 0; i < bin_counts_.size(); i++) {
                    new_bin_counts[i] = bin_counts_[i];
                }
                bin_counts_ = std::move(new_bin_counts);
                max_ = min_ + (static_cast<double>(bin_counts_.size()) * bin_width_);
            }
            bin_counts_[static_cast<std::size_t>(new_obs_index)] += 1;
        }
    }

    // ported from: DynamicHistogram.cs AddObservationsToHistogram(double[] data). Early
    // shut-down when a >1000-sample histogram is still identically zero, else add each observation
    // and, if the bin array grew past MAX_BIN_COUNT*4, coarsen it back toward MAX_BIN_COUNT bins.
    void add_observations_to_histogram(const std::vector<double>& data) override {
        bool sample_size_is_big_enough = sample_size_ > 1000;
        if (sample_size_is_big_enough && histogram_is_zero_valued()) {
            shut_histogram_down();
        } else {
            for (double x : data) {
                add_observation_to_histogram(x);
            }
            if (bin_counts_.size() > static_cast<std::size_t>(MAX_BIN_COUNT * 4)) {
                double divisor = static_cast<double>(bin_counts_.size()) / MAX_BIN_COUNT;
                resize_histogram(divisor);
            }
        }
    }

    // ported from: DynamicHistogram.cs FindBinCount(double x, bool cumulative=true).
    std::int64_t find_bin_count(double x, bool cumulative = true) const override {
        if (x > max_) {
            return cumulative ? sample_size_ : 0;
        }
        if (x < min_) {
            return 0;
        }
        int obs_index = static_cast<int>(std::floor((x - min_) / bin_width_));
        if (obs_index == static_cast<int>(bin_counts_.size())) {
            obs_index -= 1;
        }
        if (cumulative) {
            std::int64_t sum = 0;
            for (int i = 0; i < obs_index + 1; i++) {
                sum += bin_counts_[static_cast<std::size_t>(i)];
            }
            return sum;
        } else {
            return bin_counts_[static_cast<std::size_t>(obs_index)];
        }
    }

    // ported from: DynamicHistogram.cs HistogramMean(). Mean computed FROM the binned histogram
    // (bin-midpoint weighted), distinct from the raw running SampleMean.
    double histogram_mean() const {
        if (sample_size_ == 0) return std::numeric_limits<double>::quiet_NaN();
        if (min_ == (max_ - bin_width_)) return max_ + (.5 * bin_width_);
        double sum = 0;
        for (std::size_t i = 0; i < bin_counts_.size(); i++) {
            sum += (min_ + (static_cast<double>(i) * bin_width_) + (0.5 * bin_width_)) *
                   static_cast<double>(bin_counts_[i]);
        }
        return sum / static_cast<double>(sample_size_);
    }

    // ported from: DynamicHistogram.cs HistogramVariance(). NOTE (transcribed verbatim): the
    // deviation sum is NOT weighted by BinCounts[i] -- it sums (midpoint - SampleMean)^2 once per
    // bin, then divides by (SampleSize-1). Faithful to upstream even though it reads oddly.
    double histogram_variance() const {
        if (sample_size_ == 0) return std::numeric_limits<double>::quiet_NaN();
        if (sample_size_ == 1) return 0.0;
        if (min_ == (max_ - bin_width_)) return 0.0;
        double deviation2 = 0;
        for (std::size_t i = 0; i < bin_counts_.size(); i++) {
            double midpoint = min_ + (static_cast<double>(i) * bin_width_) + (0.5 * bin_width_);
            double deviation = midpoint - sample_mean_;
            deviation2 += deviation * deviation;
        }
        return deviation2 / static_cast<double>(sample_size_ - 1);
    }

    // ported from: DynamicHistogram.cs HistogramStandardDeviation().
    double histogram_standard_deviation() const { return std::sqrt(histogram_variance()); }

    // ported from: DynamicHistogram.cs IsHistogramConverged(double upperq, double lowerq). Consumes
    // ConvergenceCriteria (min/max iterations, ZAlpha, Tolerance) plus the histogram's own
    // InverseCDF/PDF to test the quantile half-width against Tolerance*0.5 at both tails.
    bool is_histogram_converged(double upperq, double lowerq) override {
        if (is_converged_) return true;
        if (sample_size_ < convergence_criteria_.min_iterations()) return false;
        if (sample_size_ >= convergence_criteria_.max_iterations()) {
            is_converged_ = true;
            converged_iteration_ = sample_size_;
            converged_on_max_ = true;
            return true;
        }
        if (sample_size_ >= convergence_criteria_.min_iterations() && min_ == max_) {
            return true;
        }
        double qval = inverse_cdf(lowerq);
        double qslope = pdf(qval);
        double variance =
            (lowerq * (1 - lowerq)) / (static_cast<double>(sample_size_) * qslope * qslope);
        bool lower = false;
        double lower_comparison = std::abs(convergence_criteria_.z_alpha() * std::sqrt(variance) / qval);
        if (lower_comparison <= (convergence_criteria_.tolerance() * .5)) lower = true;
        qval = inverse_cdf(upperq);
        qslope = pdf(qval);
        variance = (upperq * (1 - upperq)) / (static_cast<double>(sample_size_) * qslope * qslope);
        bool upper = false;
        double upper_comparison = std::abs(convergence_criteria_.z_alpha() * std::sqrt(variance) / qval);
        if (upper_comparison <= (convergence_criteria_.tolerance() * .5)) upper = true;
        if (lower) {
            is_converged_ = true;
            converged_iteration_ = sample_size_;
        }
        if (upper) {
            is_converged_ = true;
            converged_iteration_ = sample_size_;
        }
        return is_converged_;
    }

    // ported from: DynamicHistogram.cs EstimateIterationsRemaining(double upperq, double lowerq).
    std::int64_t estimate_iterations_remaining(double upperq, double lowerq) override {
        if (is_converged_) return 0;
        double up = upperq;
        double val = up * (1 - up);
        double uz2 = 2 * convergence_criteria_.z_alpha();
        double uxp = inverse_cdf(up);
        double ufxp = pdf(uxp);
        std::int64_t upperestimate = convergence_criteria_.max_iterations();
        if (ufxp > 0.0 && uxp != 0) {
            double ratio = uz2 / (uxp * convergence_criteria_.tolerance() * ufxp);
            double estimate = std::ceil(val * (ratio * ratio));
            if (estimate > static_cast<double>(std::numeric_limits<int>::max() - 1)) {
                upperestimate = std::numeric_limits<int>::max() - 1;
            } else {
                std::int64_t e = static_cast<std::int64_t>(estimate);
                upperestimate = e < 0 ? -e : e;
            }
        }
        double lp = lowerq;
        double lval = lp * (1 - lp);
        double lz2 = 2 * convergence_criteria_.z_alpha();
        double lxp = inverse_cdf(lp);
        double lfxp = pdf(lxp);
        std::int64_t lowerestimate = convergence_criteria_.max_iterations();
        // NOTE: the upstream guard reads `lfxp > 0.0 & uxp != 0` -- it tests uxp (the UPPER
        // quantile), almost certainly a copy-paste of the upper branch. Transcribed verbatim.
        if (lfxp > 0.0 && uxp != 0) {
            double lower_ratio = lz2 / (lxp * convergence_criteria_.tolerance() * lfxp);
            double estimate = std::ceil(lval * (lower_ratio * lower_ratio));
            if (estimate > static_cast<double>(std::numeric_limits<int>::max() - 1)) {
                lowerestimate = std::numeric_limits<int>::max() - 1;
            } else {
                std::int64_t e = static_cast<std::int64_t>(estimate);
                lowerestimate = e < 0 ? -e : e;
            }
        }
        std::int64_t biggest_guess = std::max(upperestimate, lowerestimate);
        std::int64_t remaining_iters =
            static_cast<std::int64_t>(convergence_criteria_.max_iterations()) - sample_size_;
        return std::min(remaining_iters, biggest_guess);
    }

   private:
    // C# Convert.ToInt64/ToInt32(double): round HALF-TO-EVEN then cast. std::nearbyint honors the
    // default round-to-nearest-ties-to-even FP mode.
    static std::int64_t convert_to_int64(double value) {
        return static_cast<std::int64_t>(std::nearbyint(value));
    }
    static int convert_to_int32(double value) {
        return static_cast<int>(std::nearbyint(value));
    }

    // ported from: DynamicHistogram.cs ResizeHistogram(double divisor). Widens BinWidth by
    // `divisor` and re-bins existing counts into ceil(len/divisor) coarser bins.
    void resize_histogram(double divisor) {
        bin_width_ = bin_width_ * divisor;
        int new_bin_count =
            static_cast<int>(std::ceil(static_cast<double>(bin_counts_.size()) / divisor));
        std::vector<std::int64_t> new_bins(static_cast<std::size_t>(new_bin_count), 0);
        for (std::size_t i = 0; i < bin_counts_.size(); i++) {
            int new_bin = static_cast<int>(std::floor(static_cast<double>(i) / divisor));
            new_bins[static_cast<std::size_t>(new_bin)] += bin_counts_[i];
        }
        max_ = min_ + static_cast<double>(new_bin_count) * bin_width_;
        bin_counts_ = std::move(new_bins);
    }

    // ported from: DynamicHistogram.cs EnsureCapacityForObservation(double observation). If the
    // projected range would exceed OVERFLOW_PREVENTION_THRESHOLD bins, coarsen to MAX_BIN_COUNT
    // bins BEFORE the add proceeds (prevents Int32 overflow / runaway memory).
    void ensure_capacity_for_observation(double observation) {
        if (bin_counts_.empty()) return;
        double projected_min = std::min(min_, observation);
        double projected_max = std::max(max_, observation);
        double projected_range = projected_max - projected_min;
        double projected_bin_count = projected_range / bin_width_;
        if (projected_bin_count > OVERFLOW_PREVENTION_THRESHOLD) {
            double new_bin_width = projected_range / MAX_BIN_COUNT;
            double divisor = new_bin_width / bin_width_;
            resize_histogram(divisor);
        }
    }

    // ported from: DynamicHistogram.cs ResetToZeroMin(int quantityAdditionalBins). Special-case
    // growth when an observation of exactly 0 (or a positive obs that would push Min below 0) lands
    // below Min: snap Min to 0, add q bins, put the 0-observation in bin 0, and redistribute each
    // existing bin's count across two adjacent new bins by lowerFraction/upperFraction (this is the
    // banker's-rounding split described in the class comment).
    void reset_to_zero_min(int quantity_additional_bins) {
        double new_min = 0;
        std::size_t original_bins_length = bin_counts_.size();
        std::vector<std::int64_t> new_bins(
            static_cast<std::size_t>(quantity_additional_bins) + original_bins_length, 0);
        new_bins[0] += 1;
        double lower_fraction =
            (new_min + quantity_additional_bins * bin_width_ - min_) / bin_width_;
        double upper_fraction = 1 - lower_fraction;
        for (std::size_t i = 0; i < original_bins_length; i++) {
            new_bins[i + static_cast<std::size_t>(quantity_additional_bins) - 1] +=
                convert_to_int64(lower_fraction * static_cast<double>(bin_counts_[i]));
            new_bins[i + static_cast<std::size_t>(quantity_additional_bins)] +=
                convert_to_int64(upper_fraction * static_cast<double>(bin_counts_[i]));
        }
        min_ = new_min;
        bin_counts_ = std::move(new_bins);
        max_ = min_ + static_cast<double>(bin_counts_.size()) * bin_width_;
    }

    // ported from: DynamicHistogram.cs ShutHistogramDown(). Collapses a persistently-zero histogram
    // to a single unit bin and marks it converged so no further observations are binned.
    void shut_histogram_down() {
        histogram_shut_down_ = true;
        bin_counts_.assign(1, 1);
        min_ = 0;
        bin_width_ = 1;
        sample_max_ = 0;
        sample_mean_ = 0;
        sample_variance_ = 0;
        sample_min_ = 0;
        sample_size_ = 1;
        is_converged_ = true;
        converged_on_max_ = false;
    }

    // ported from: DynamicHistogram.cs IsZeroValued() (mean == 0 AND StandardDeviation == 0).
    bool is_zero_valued() const {
        bool mean_is_zero = sample_mean_ == 0;
        bool standard_deviation_is_zero = standard_deviation() == 0;
        return mean_is_zero && standard_deviation_is_zero;
    }

    // ported from: DynamicHistogram.cs IsSingleValued() (BinCounts[0] == SampleSize). As in C#,
    // assumes bin_counts_ is non-empty (upstream would throw IndexOutOfRange otherwise).
    bool is_single_valued() const { return bin_counts_[0] == sample_size_; }

    double bin_width_ = DEFAULT_BIN_WIDTH;
    double min_ = 0.0;
    double max_ = 0.0;
    double sample_mean_ = 0.0;
    double sample_variance_ = 0.0;  // the C# `_SampleVariance` accumulator (divide-by-(n-1) form)
    double sample_min_ = 0.0;
    double sample_max_ = 0.0;
    std::vector<std::int64_t> bin_counts_;
    bool min_has_not_been_set_ = false;
    bool histogram_shut_down_ = false;
    bool converged_on_max_ = false;
    bool is_converged_ = false;
    std::int64_t converged_iteration_ = static_cast<std::int64_t>(std::numeric_limits<int>::min());
    ConvergenceCriteria convergence_criteria_;
};

}  // namespace histograms
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_HISTOGRAMS_DYNAMIC_HISTOGRAM_HPP
