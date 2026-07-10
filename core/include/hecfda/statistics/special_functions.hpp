// ported from: HEC.FDA.Statistics/Distributions/SpecialFunctions.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_SPECIAL_FUNCTIONS_HPP
#define HECFDA_STATISTICS_SPECIAL_FUNCTIONS_HPP
#include <cmath>
#include <limits>
#include "hecfda/constants.hpp"
namespace hecfda {
namespace statistics {
// Only the members needed by Normal.CDF are ported in this phase: logGamma and the
// regularized-gamma-P path (regIncompleteGamma -> regularizedGammaP -> regularizedGammaQ ->
// evaluateCFGammaQ). Other SpecialFunctions.cs members (digamma, beta family, trigamma,
// gammaDerivative, ...) are out of scope until a distribution that needs them is ported.
struct SpecialFunctions {
    // ported from: SpecialFunctions.cs logGamma(double t)
    static double log_gamma(double t) {
        if (t < 0.5) {
            // Reflection formula
            return std::log(hecfda::kPi) - std::log(std::sin(hecfda::kPi * t)) - log_gamma(1 - t);
        } else {
            // Coefficients used by the GNU Scientific Library
            double sum = 0.9999999999998099 + 676.5203681218851 / (t) -
                         1259.1392167224028 / (t + 1.0) + 771.3234287776531 / (t + 2.0) -
                         176.6150291621406 / (t + 3.0) + 12.507343278686905 / (t + 4.0) -
                         0.13857109526572012 / (t + 5.0) + 9.984369578019572e-6 / (t + 6.0) +
                         1.5056327351493116e-7 / (t + 7.0);
            double base_number = t + 7 - 0.5;
            return ((0.5 * std::log(2.0 * hecfda::kPi) + std::log(sum)) - base_number) +
                   (t - 0.5) * std::log(base_number);
        }
    }

    // ported from: SpecialFunctions.cs regIncompleteGamma(double t, double x)
    static double reg_incomplete_gamma(double t, double x) {
        return regularized_gamma_p(t, x, 1.0e-14, std::numeric_limits<int>::max());
    }

   private:
    // ported from: SpecialFunctions.cs regularizedGammaP(double a, double x, double epsilon, int maxIterations)
    static double regularized_gamma_p(double a, double x, double epsilon, int max_iterations) {
        double ret = 0.0;
        if ((a <= 0.0) || (x < 0.0)) {
            ret = std::numeric_limits<double>::quiet_NaN();
        } else if (x == 0.0) {
            ret = 0.0;
        } else if (x >= a + 1) {
            // use regularizedGammaQ because it should converge faster in this case.
            ret = 1.0 - regularized_gamma_q(a, x, epsilon, max_iterations);
        } else {
            // calculate series
            double n = 0.0;                // current element index
            double an = 1.0 / a;           // n-th element in the series
            double sum = an;               // partial sum
            while (std::fabs(an / sum) > epsilon && n < max_iterations &&
                   sum < std::numeric_limits<double>::infinity()) {
                n += 1.0;
                an *= x / (a + n);
                sum += an;
            }
            if (n >= max_iterations) {
                // matches C#: falls through leaving ret at its initial 0.0
            } else if (sum > std::numeric_limits<double>::max()) {
                ret = 1.0;
            } else {
                ret = std::exp(-x + (a * std::log(x)) - log_gamma(a)) * sum;
            }
        }
        return ret;
    }

    // ported from: SpecialFunctions.cs regularizedGammaQ(double a, double x, double epsilon, int maxIterations)
    static double regularized_gamma_q(double a, double x, double epsilon, int max_iterations) {
        double ret;
        if ((a <= 0.0) || (x < 0.0)) {
            ret = std::numeric_limits<double>::quiet_NaN();
        } else if (x == 0.0) {
            ret = 1.0;
        } else if (x < a + 1.0) {
            // use regularizedGammaP because it should converge faster in this case.
            ret = 1.0 - regularized_gamma_p(a, x, epsilon, max_iterations);
        } else {
            // create continued fraction
            ret = 1.0 / evaluate_cf_gamma_q(a, x, epsilon, max_iterations);
            ret = std::exp(-x + (a * std::log(x)) - log_gamma(a)) * ret;
        }
        return ret;
    }

    // ported from: SpecialFunctions.cs evaluateCFGammaQ(double a, double x, double epsilon, int maxIterations)
    // modified Lentz algorithm continued-fraction evaluation.
    static double evaluate_cf_gamma_q(double a, double x, double epsilon, int max_iterations) {
        const double small = 1.0e-50;
        double h_prev = ((2.0 * 0.0) + 1.0) - a + x;
        // use the value of small as epsilon criteria for zero checks
        if (std::fabs(h_prev - 0.0) < small) {
            h_prev = small;
        }
        int n = 1;
        double d_prev = 0.0;
        double c_prev = h_prev;
        double h_n = h_prev;
        while (n < max_iterations) {
            double aa = ((2.0 * n) + 1.0) - a + x;
            double bb = n * (a - n);
            double d_n = aa + bb * d_prev;
            if (std::fabs(d_n - 0.0) < small) {
                d_n = small;
            }
            double c_n = aa + bb / c_prev;
            if (std::fabs(c_n - 0.0) < small) {
                c_n = small;
            }
            d_n = 1 / d_n;

            double delta_n = c_n * d_n;
            h_n = h_prev * delta_n;
            if (std::fabs(delta_n - 1.0) < epsilon) {
                break;
            }
            d_prev = d_n;
            c_prev = c_n;
            h_prev = h_n;
            n++;
        }
        return h_n;
    }
};
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_SPECIAL_FUNCTIONS_HPP
