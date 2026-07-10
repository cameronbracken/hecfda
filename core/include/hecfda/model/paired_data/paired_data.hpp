// ported from: HEC.FDA.Model/paireddata/PairedData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_PAIRED_DATA_HPP
#define HECFDA_MODEL_PAIRED_DATA_PAIRED_DATA_HPP
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>
#include "hecfda/statistics/mathematics.hpp"
namespace hecfda {
namespace model {
namespace paired_data {
// Only f, f_inverse, and Integrate are ported in this phase (CurveMetaData, compose,
// SumYsForGivenX, multiply, and the monotonicity/sort helpers are out of scope until a caller
// needs them). x/y storage mirrors the C# double[] fields; the ctor copies by value like the C#
// ToArray() call.
class PairedData {
   public:
    PairedData(std::vector<double> xs, std::vector<double> ys)
        : x_vals_(std::move(xs)), y_vals_(std::move(ys)) {}

    // ported from: PairedData.cs f(double x)
    // Array.BinarySearch(_xVals, x) semantics: an exact match returns that index; otherwise it
    // returns the bitwise complement of the index of the first element strictly greater than x
    // (the insertion point). std::lower_bound gives the same insertion point directly, so the
    // "not found" branch below just uses `it - begin()` in place of `~index`.
    double f(double x) const {
        if (x_vals_.front() > x_vals_.back()) {
            throw std::invalid_argument("X values must be in increasing order.");
        }
        std::size_t len = x_vals_.size();
        auto it = std::lower_bound(x_vals_.begin(), x_vals_.end(), x);
        if (it != x_vals_.end() && *it == x) {
            // Matches a value exactly
            std::size_t index = static_cast<std::size_t>(it - x_vals_.begin());
            return y_vals_[index];
        }
        // This is the next LARGER value.
        std::size_t index = static_cast<std::size_t>(it - x_vals_.begin());
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

    // ported from: PairedData.cs f_inverse(double y)
    // Symmetric to f(), binary-searching y_vals_ instead of x_vals_ (assumes y is increasing).
    double f_inverse(double y) const {
        if (x_vals_.front() > x_vals_.back()) {
            throw std::invalid_argument("X values must be in increasing order.");
        }
        std::size_t len = y_vals_.size();
        auto it = std::lower_bound(y_vals_.begin(), y_vals_.end(), y);
        if (it != y_vals_.end() && *it == y) {
            // Matches a value exactly
            std::size_t index = static_cast<std::size_t>(it - y_vals_.begin());
            return x_vals_[index];
        }
        // This is the next LARGER value.
        std::size_t index = static_cast<std::size_t>(it - y_vals_.begin());
        if (index == len) return x_vals_[len - 1];
        if (index == 0) return x_vals_[0];

        // Ok. Interpolate Y=mx+b
        double m = (y_vals_[index] - y_vals_[index - 1]) / (x_vals_[index] - x_vals_[index - 1]);
        double b = x_vals_[index - 1];
        double dy = y - y_vals_[index - 1];
        return dy / m + b;
    }

    // ported from: PairedData.cs Integrate(bool withPadding = true)
    double integrate(bool with_padding = true) const {
        if (x_vals_.front() > x_vals_.back()) {
            throw std::invalid_argument("X values must be in increasing order.");
        }
        if (with_padding) {
            return hecfda::statistics::Mathematics::integrate_cdf(x_vals_, y_vals_);
        }
        return hecfda::statistics::Mathematics::real_integrate_trapezoidal(x_vals_, y_vals_);
    }

   private:
    std::vector<double> x_vals_;
    std::vector<double> y_vals_;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_PAIRED_DATA_HPP
