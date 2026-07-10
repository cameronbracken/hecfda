// ported from: HEC.FDA.Statistics/Mathematics.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_MATHEMATICS_HPP
#define HECFDA_STATISTICS_MATHEMATICS_HPP
#include <cstddef>
#include <stdexcept>
#include <vector>
namespace hecfda {
namespace statistics {
// Only IntegrateCDF and RealIntegrateTrapezoidal are ported; IntegrateTrapezoidal is marked
// [Obsolete] in the C# source and out of scope for this phase. The C# generic
// IBinaryFloatingPointIeee754<T> constraint is dropped in favor of double, and Span<T> becomes
// std::vector<double>.
struct Mathematics {
    // ported from: Mathematics.cs RealIntegrateTrapezoidal<T>(Span<T> xVals, Span<T> yVals)
    static double real_integrate_trapezoidal(const std::vector<double>& x_vals,
                                              const std::vector<double>& y_vals) {
        if (x_vals.size() != y_vals.size()) {
            throw std::invalid_argument("xVals and yVals must have the same length");
        }
        double area = 0.0;
        for (std::size_t i = 0; i + 1 < x_vals.size(); ++i) {
            double dx = x_vals[i + 1] - x_vals[i];
            if (dx < 0.0) {
                throw std::invalid_argument("X values must be in ascending order to integrate");
            }
            area += (y_vals[i] + y_vals[i + 1]) / 2.0 * dx;
        }
        return area;
    }

    // ported from: Mathematics.cs IntegrateCDF<T>(Span<T> xVals, Span<T> yVals)
    // Calculates the area under the curve across the range of x values using trapezoidal
    // integration. Assumes x and y vals are increasing from 0. Assumes an additional x ordinate
    // of 1, and y ordinate equal to the last one in the array. This works because the only thing
    // we're integrating is CDFs of Consequence Frequency, which are always between 0 and 1.
    static double integrate_cdf(const std::vector<double>& x_vals, const std::vector<double>& y_vals) {
        double padded_area = 0.0;
        // Between 0 and x1 (Triangle)
        if (x_vals.front() > 0.0) {
            padded_area += (x_vals.front() * y_vals.front()) / 2.0;
        }
        // Between xN and 1 (Rectangle)
        if (x_vals.back() < 1.0) {
            padded_area += (1.0 - x_vals.back()) * y_vals.back();
        }
        return padded_area + real_integrate_trapezoidal(x_vals, y_vals);
    }
};
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_MATHEMATICS_HPP
