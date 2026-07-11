// ported from: HEC.FDA.Model/paireddata/PairedData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_PAIRED_DATA_HPP
#define HECFDA_MODEL_PAIRED_DATA_PAIRED_DATA_HPP
#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <vector>
#include "hecfda/model/paired_data/curve_meta_data.hpp"
#include "hecfda/model/paired_data/dotnet_binary_search.hpp"
#include "hecfda/model/paired_data/i_paired_data.hpp"
#include "hecfda/statistics/mathematics.hpp"
namespace hecfda {
namespace model {
namespace paired_data {
// f, f_inverse, Integrate, CurveMetaData, compose, SumYsForGivenX, multiply, and the
// monotonicity/sort helpers are all ported here. x/y storage mirrors the C# double[] fields; the
// ctor copies by value like the C# ToArray() call. `metadata` defaults to a fresh CurveMetaData()
// (IsNull() == true), standing in for the C# ctor's `metadata = null` default -- C++'s
// std::vector-backed PairedData has no natural "null curve" representation, so the "null" analog
// used throughout this port is CurveMetaData's own IsNull() flag rather than a nullable metadata
// member (see sum_ys_for_given_x()'s x_vals_.empty()/y_vals_.empty() guard for the corresponding
// "null Xvals/Yvals" analog).
class PairedData : public IPairedData {
   public:
    PairedData(std::vector<double> xs, std::vector<double> ys, CurveMetaData metadata = CurveMetaData())
        : x_vals_(std::move(xs)), y_vals_(std::move(ys)), metadata_(std::move(metadata)) {}

    const CurveMetaData& metadata() const { return metadata_; }
    const std::vector<double>& xvals() const override { return x_vals_; }
    const std::vector<double>& yvals() const override { return y_vals_; }

    // ported from: PairedData.cs f(double x)
    // Array.BinarySearch(_xVals, x) semantics, faithfully via dotnet_binary_search: an exact
    // match returns SOME matching index (midpoint-driven, not necessarily the first, when x_vals_
    // has duplicates -- e.g. flat frequency segments); otherwise it returns the bitwise complement
    // `~index` of the insertion point (the index of the first element strictly greater than x, or
    // x_vals_.size() if x exceeds every element). This must NOT be std::lower_bound, which always
    // returns the FIRST equal element and so can silently diverge from the real C# on duplicates.
    double f(double x) const override {
        if (x_vals_.front() > x_vals_.back()) {
            throw std::invalid_argument("X values must be in increasing order.");
        }
        std::size_t len = x_vals_.size();
        long search_index = dotnet_binary_search(x_vals_, x);
        if (search_index >= 0) {
            // Matches a value exactly
            return y_vals_[static_cast<std::size_t>(search_index)];
        }
        // This is the next LARGER value.
        std::size_t index = static_cast<std::size_t>(~search_index);
        if (index == len) return y_vals_[len - 1];
        if (index == 0) return y_vals_[0];

        // Ok. Interpolate Y=mx+b
        double y_index_minus1 = y_vals_[index - 1];
        double x_index_minus1 = x_vals_[index - 1];
        double m = (y_vals_[index] - y_index_minus1) / (x_vals_[index] - x_index_minus1);
        double b = y_index_minus1;
        double dx = x - x_index_minus1;
        return m * dx + b;
    }

    // ported from: PairedData.cs f(double x, ref int indexOfPreviousTopOfSegment)
    // "Created to provide a method for searching paired data without using binary search." Unlike
    // f(x), this special-cases out-of-range x directly: above the curve returns the LAST y (same
    // as f(x)'s off-the-end behavior), but below the curve returns 0 -- NOT the first y, which is
    // what plain f(x) returns for x below the first x value. That asymmetry is real upstream
    // behavior, kept verbatim. `index_of_previous_top_of_segment` is caller-owned sequential-scan
    // state, mutated in place exactly like the C# `ref int`.
    double f(double x, int& index_of_previous_top_of_segment) const override {
        require_non_empty();
        if (x_vals_.front() > x_vals_.back()) {
            throw std::invalid_argument("X values must be in increasing order.");
        }
        // We're above the curve
        if (x > x_vals_.back()) {
            return y_vals_.back();
        }
        // We're below the curve
        if (x < x_vals_.front()) {
            return 0.0;
        }
        // If we've got it right on
        if (x_vals_[static_cast<std::size_t>(index_of_previous_top_of_segment)] == x) {
            return y_vals_[static_cast<std::size_t>(index_of_previous_top_of_segment)];
        }
        // We're on the same segment
        if (x < x_vals_[static_cast<std::size_t>(index_of_previous_top_of_segment)]) {
            return interpolate_ys(x, index_of_previous_top_of_segment);
        }
        // x is greater than the last top of segment
        while (x > x_vals_[static_cast<std::size_t>(index_of_previous_top_of_segment)]) {
            ++index_of_previous_top_of_segment;
        }
        return interpolate_ys(x, index_of_previous_top_of_segment);
    }

    // ported from: PairedData.cs f_inverse(double y)
    // Symmetric to f(), binary-searching y_vals_ instead of x_vals_ (assumes y is increasing) via
    // dotnet_binary_search -- see f()'s comment for why this must not be std::lower_bound.
    double f_inverse(double y) const override {
        if (x_vals_.front() > x_vals_.back()) {
            throw std::invalid_argument("X values must be in increasing order.");
        }
        std::size_t len = y_vals_.size();
        long search_index = dotnet_binary_search(y_vals_, y);
        if (search_index >= 0) {
            // Matches a value exactly
            return x_vals_[static_cast<std::size_t>(search_index)];
        }
        // This is the next LARGER value.
        std::size_t index = static_cast<std::size_t>(~search_index);
        if (index == len) return x_vals_[len - 1];
        if (index == 0) return x_vals_[0];

        // Ok. Interpolate Y=mx+b
        double m = (y_vals_[index] - y_vals_[index - 1]) / (x_vals_[index] - x_vals_[index - 1]);
        double b = x_vals_[index - 1];
        double dy = y - y_vals_[index - 1];
        return dy / m + b;
    }

    // ported from: PairedData.cs compose(IPairedData inputPairedData)
    // Takes the input's Y values as the X sample point (looking up the commensurate Y from this
    // curve via f()); the composed curve's X values are the input's X values verbatim. Typical
    // compose patterns: Stage Damage compose Frequency Stage = Damage Frequency; System Response
    // compose Frequency Stage = Failure Frequency; FlowStageSample compose Frequency Flow =
    // Frequency Stage.
    PairedData compose(const IPairedData& input) const override {
        std::size_t count = input.xvals().size();
        std::vector<double> x(count);
        std::vector<double> y(count);
        for (std::size_t i = 0; i < count; ++i) {
            y[i] = f(input.yvals()[i]);
            x[i] = input.xvals()[i];
        }
        return PairedData(std::move(x), std::move(y));
    }

    // ported from: PairedData.cs SumYsForGivenX(IPairedData inputPairedData)
    // Builds the sorted union of both curves' X grids (SortedSet<double> -> std::set<double>,
    // both ascending-unique by construction) and sums f(x) from each curve at every union X.
    //
    // The C# guard `if (Xvals == null || Yvals == null) return new PairedData(input...)` has NO
    // direct analog in this port: PairedData's ctor always takes concrete std::vector<double>
    // (never a null pointer), so a "null Xvals" PairedData cannot be constructed here in the
    // first place. Verified against the real C# (via the oracle_emitter): constructing PairedData
    // with EMPTY-but-non-null arrays does NOT take that early-return branch either -- it falls
    // through to `_xVals[0]`, which throws System.IndexOutOfRangeException. This port mirrors
    // that "an empty curve is invalid input to this method" outcome with a defined C++ exception
    // (std::out_of_range) instead of the undefined behavior that x_vals_.front() on an empty
    // vector would otherwise be.
    PairedData sum_ys_for_given_x(const IPairedData& input) const {
        require_non_empty();
        if (x_vals_.front() > x_vals_.back()) {
            throw std::invalid_argument("X values must be in increasing order.");
        }

        // Build a sorted union of both x-grids
        std::set<double> union_x_set;
        for (double x : x_vals_) union_x_set.insert(x);
        for (double x : input.xvals()) union_x_set.insert(x);

        std::vector<double> union_x(union_x_set.begin(), union_x_set.end());
        std::vector<double> union_y(union_x.size());
        for (std::size_t idx = 0; idx < union_x.size(); ++idx) {
            union_y[idx] = f(union_x[idx]) + input.f(union_x[idx]);
        }
        return PairedData(std::move(union_x), std::move(union_y));
    }

    // ported from: PairedData.cs Integrate(bool withPadding = true)
    double integrate(bool with_padding = true) const override {
        if (x_vals_.front() > x_vals_.back()) {
            throw std::invalid_argument("X values must be in increasing order.");
        }
        if (with_padding) {
            return hecfda::statistics::Mathematics::integrate_cdf(x_vals_, y_vals_);
        }
        return hecfda::statistics::Mathematics::real_integrate_trapezoidal(x_vals_, y_vals_);
    }

    // ported from: PairedData.cs multiply(IPairedData systemResponseFunction)
    // Appropriate when subject is a stage damage curve, and the input is a fragility curve.
    // Assumes subject has stages as x values, and damage as y values. Assumes input has
    // probability of failure as y values, and stages as x values. multiply multiplies a stage
    // damage curve by a fragility curve. All damages below the curve are considered 0. Returns a
    // paired data where x values are stages from the subject, and y vals are damage*prob.
    PairedData multiply(const IPairedData& system_response_function) const override {
        std::vector<double> new_xvals;  // xvals are stages in the stage-damage function
        std::vector<double> new_yvals;  // yvals are damage*prob(failure)

        // calculate damages for the range of the stage-damage function
        for (std::size_t i = 0; i < x_vals_.size(); ++i) {
            double stage_from_stage_damage = x_vals_[i];
            double probability_of_failure = system_response_function.f(stage_from_stage_damage);
            double probability_weighted_damage = probability_of_failure * y_vals_[i];

            new_xvals.push_back(stage_from_stage_damage);
            new_yvals.push_back(probability_weighted_damage);
        }
        const std::vector<double>& fragility_xvals = system_response_function.xvals();
        const std::vector<double>& fragility_yvals = system_response_function.yvals();
        for (std::size_t i = 0; i < fragility_xvals.size(); ++i) {
            double fragility_stage = fragility_xvals[i];
            bool fragility_stage_is_in_stages =
                std::find(new_xvals.begin(), new_xvals.end(), fragility_stage) != new_xvals.end();
            if (!fragility_stage_is_in_stages) {
                double probability_of_failure = fragility_yvals[i];
                double unweighted_damage = f(fragility_stage);
                double probability_weighted_damage = probability_of_failure * unweighted_damage;
                new_xvals.push_back(fragility_stage);
                new_yvals.push_back(probability_weighted_damage);
            }
        }
        // This sorts the stages and sorts the damage based on the sorting of the stages
        // (Array.Sort(stages, damages)).
        sort_parallel(new_xvals, new_yvals);
        return PairedData(std::move(new_xvals), std::move(new_yvals));
    }

    // ported from: PairedData.cs ForceWeakMonotonicityBottomUp()
    // Weak monotonicity demands the function be either flat or increasing, enforced by walking
    // the function from the bottom up -- effectively capping the minimum value to the minimum
    // provided. MUTATES this curve's y values.
    void force_weak_monotonicity_bottom_up() {
        require_non_empty();
        double previous_y_val = y_vals_[0];
        for (std::size_t i = 1; i < y_vals_.size(); ++i) {
            double current_y = y_vals_[i];
            if (previous_y_val >= current_y) {
                y_vals_[i] = previous_y_val;
            } else {
                previous_y_val = current_y;
            }
        }
    }

    // ported from: PairedData.cs ForceStrictMonotonicityTopDown()
    // Strict monotonicity demands the function be increasing (flat sections not permitted),
    // enforced by walking the function from the top down -- effectively capping the maximum
    // value to the max provided. `double.Epsilon` (the smallest positive subnormal double) ->
    // std::numeric_limits<double>::denorm_min(); note that subtracting/adding this from/to any
    // value with a normal-range magnitude (e.g. 2.0) is a silent no-op in IEEE 754 double
    // arithmetic (below the representable ULP), so in practice this "nudge" only has any effect
    // very close to zero -- a real upstream quirk, reproduced verbatim rather than "fixed".
    // MUTATES this curve's y values.
    void force_strict_monotonicity_top_down() {
        require_non_empty();
        double upper_value = y_vals_.back();

        for (long i = static_cast<long>(y_vals_.size()) - 2; i >= 0; --i) {
            std::size_t idx = static_cast<std::size_t>(i);
            if (y_vals_[idx] >= upper_value) {
                upper_value -= std::numeric_limits<double>::denorm_min();
                y_vals_[idx] = upper_value;
            } else {
                upper_value = y_vals_[idx];
            }
        }
    }

    // ported from: PairedData.cs ForceStrictMonotonicityBottomUp()
    // Strict monotonicity demands the function be increasing (flat sections not permitted),
    // enforced by walking the function from the bottom up -- effectively capping the minimum
    // value to the minimum provided. See force_strict_monotonicity_top_down() for the
    // double.Epsilon -> denorm_min() no-op-at-normal-magnitude quirk, which applies here too.
    // MUTATES this curve's y values.
    void force_strict_monotonicity_bottom_up() {
        require_non_empty();
        double previous_y_val = y_vals_[0];
        for (std::size_t index = 1; index < y_vals_.size(); ++index) {
            double current_y = y_vals_[index];
            if (previous_y_val >= current_y) {
                previous_y_val += std::numeric_limits<double>::denorm_min();
                y_vals_[index] = previous_y_val;
            } else {
                previous_y_val = current_y;
            }
        }
    }

    // ported from: PairedData.cs SortToIncreasingXVals() -- Array.Sort(_xVals, _yVals).
    // MUTATES both this curve's x and y values.
    void sort_to_increasing_x_vals() { sort_parallel(x_vals_, y_vals_); }

   private:
    // Not present in the C# source: C#'s `_xVals[0]`/`Yvals[^1]` on an empty (but non-null) array
    // throws a defined, catchable System.IndexOutOfRangeException on its own -- no explicit guard
    // needed there. The equivalent C++ operations (std::vector::front()/back()/operator[](0)) are
    // undefined behavior on an empty vector, not a thrown exception, so this port adds an explicit
    // guard at the entry of every NEW (Task P2T2) method that would otherwise index into an empty
    // x_vals_/y_vals_ before any bounds check -- f(x, ref index) and the three
    // force_*_monotonicity_* mutators below. (compose()/multiply()/sort_to_increasing_x_vals()
    // don't need it: compose()/multiply() only ever index via f()/f_inverse(), and
    // sort_to_increasing_x_vals()'s sort_parallel() is already a no-op on empty vectors.)
    // The three PRE-EXISTING (Phase 0) methods f(x)/f_inverse(y)/integrate() were NOT touched here
    // -- they share this same empty-vector characteristic but are out of scope for this task; see
    // the Task P2T2 report for this as a documented, deliberately deferred follow-up.
    void require_non_empty() const {
        if (x_vals_.empty() || y_vals_.empty()) {
            throw std::out_of_range("PairedData: this curve has no points.");
        }
    }

    // ported from: PairedData.cs's private InterpolateYs(double x, int index), used only by the
    // f(x, ref index) overload above.
    double interpolate_ys(double x, int index) const {
        if (x_vals_.front() > x_vals_.back()) {
            throw std::invalid_argument("X values must be in increasing order.");
        }
        std::size_t i = static_cast<std::size_t>(index);
        double y_at_index_minus1 = y_vals_[i - 1];
        double x_at_index_minus1 = x_vals_[i - 1];
        double m = (y_vals_[i] - y_at_index_minus1) / (x_vals_[i] - x_at_index_minus1);
        double b = y_at_index_minus1;
        double dx = x - x_at_index_minus1;
        return m * dx + b;
    }

    // Shared by multiply() (Array.Sort(stages, damages)) and sort_to_increasing_x_vals()
    // (Array.Sort(_xVals, _yVals)) -- both are literally "stable-sort by keys, permute values in
    // lockstep" in the C# source. Every real call site's `keys` end up with no duplicate values
    // by construction (multiply()'s dedup-by-Contains loop; sort_to_increasing_x_vals() on curves
    // whose x values are already a valid, distinct grid), so std::stable_sort vs. the real
    // (unstable) Array.Sort cannot disagree in practice.
    static void sort_parallel(std::vector<double>& keys, std::vector<double>& values) {
        std::vector<std::size_t> order(keys.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(),
                          [&keys](std::size_t a, std::size_t b) { return keys[a] < keys[b]; });
        std::vector<double> sorted_keys(keys.size());
        std::vector<double> sorted_values(values.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            sorted_keys[i] = keys[order[i]];
            sorted_values[i] = values[order[i]];
        }
        keys = std::move(sorted_keys);
        values = std::move(sorted_values);
    }

    std::vector<double> x_vals_;
    std::vector<double> y_vals_;
    CurveMetaData metadata_;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_PAIRED_DATA_HPP
