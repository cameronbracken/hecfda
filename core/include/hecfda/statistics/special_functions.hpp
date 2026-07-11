// ported from: HEC.FDA.Statistics/Distributions/SpecialFunctions.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_SPECIAL_FUNCTIONS_HPP
#define HECFDA_STATISTICS_SPECIAL_FUNCTIONS_HPP
#include <cmath>
#include <limits>
#include "hecfda/constants.hpp"
namespace hecfda {
namespace statistics {
// Full gamma/beta special-function family ported verbatim from SpecialFunctions.cs.
// Directly exercised by distributions: logGamma and regIncompleteGamma (-> regularizedGammaP ->
// regularizedGammaQ -> evaluateCFGammaQ) back the Normal/TruncatedNormal/Gamma CDFs and Gamma.PDF.
// The remaining foundational members (logFactorial, gamma, factorial, incompleteGamma x2,
// logIncompleteGamma, digamma, logBeta, beta, incompleteBeta, regIncompleteBeta, regularizedBeta ->
// evaluateCFBeta, trigamma, singleParGammaPDF, gammaDerivative) have no caller among the Phase-1
// distributions but are ported for a complete, gate-verified special-function surface.
// Deliberately skipped as unused (zero callers repo-wide, not part of the gamma/beta family):
// the GEV generalization transforms t()/tInv() and the combinatorics helper binomialCoefficient().
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

    // ported from: SpecialFunctions.cs logFactorial(int n)
    static double log_factorial(int n) {
        return log_gamma(n + 1.0);
    }

    // ported from: SpecialFunctions.cs gamma(double t)
    static double gamma(double t) {
        return std::exp(log_gamma(t));
    }

    // ported from: SpecialFunctions.cs factorial(int n)
    static double factorial(int n) {
        return std::round(gamma(n + 1.0));
    }

    // ported from: SpecialFunctions.cs incompleteGamma(double t, double x)
    static double incomplete_gamma(double t, double x) {
        return gamma(t) * reg_incomplete_gamma(t, x);
    }

    // ported from: SpecialFunctions.cs incompleteGamma(double t, double xl, double xu)
    static double incomplete_gamma(double t, double xl, double xu) {
        return incomplete_gamma(t, xu) - incomplete_gamma(t, xl);
    }

    // ported from: SpecialFunctions.cs logIncompleteGamma(double t, double x)
    static double log_incomplete_gamma(double t, double x) {
        return log_gamma(t) + std::log(reg_incomplete_gamma(t, x));
    }

    // ported from: SpecialFunctions.cs digamma(double x)
    static double digamma(double x) {
        if ((x < 0.0 && std::ceil(x) == x) || x == 0) {
            return std::numeric_limits<double>::infinity();
        }
        double value = 0;
        while (true) {
            if (x >= 0 && x < kGammaMinX) {
                x = kGammaMinX;
            }
            if (x < kDigammaMinNegX) {
                x = kDigammaMinNegX + kGammaMinX;
                continue;
            }
            if (x > 0 && x <= kSLimit) {
                return value + -kGamma - 1 / x;
            }
            if (x >= kCLimit) {
                double inv = 1 / (x * x);
                return value + std::log(x) - 0.5 / x - inv * ((1.0 / 12) + inv * (1.0 / 120 - inv / 252));
            }
            value -= 1 / x;
            x = x + 1;
        }
    }

    // ported from: SpecialFunctions.cs logBeta(double s, double t)
    static double log_beta(double s, double t) {
        if (std::isnan(s) || std::isnan(t) || s <= 0.0 || t <= 0.0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return log_gamma(s) + (log_gamma(t) - log_gamma(s + t));
    }

    // ported from: SpecialFunctions.cs beta(double s, double t)
    static double beta(double s, double t) {
        return std::exp(log_beta(s, t));
    }

    // ported from: SpecialFunctions.cs incompleteBeta(double s, double t, double x)
    static double incomplete_beta(double s, double t, double x) {
        return beta(s, t) * reg_incomplete_beta(s, t, x);
    }

    // ported from: SpecialFunctions.cs regIncompleteBeta(double s, double t, double x)
    static double reg_incomplete_beta(double s, double t, double x) {
        return regularized_beta(s, t, x, 1.0e-14, std::numeric_limits<int>::max());
    }

    // ported from: SpecialFunctions.cs regularizedBeta(double a, double b, double x, double epsilon, int maxIterations)
    static double regularized_beta(double a, double b, double x, double epsilon, int max_iterations) {
        double ret;
        if (std::isnan(x) || std::isnan(a) || std::isnan(b) || x < 0 || x > 1 || a <= 0.0 || b <= 0.0) {
            ret = std::numeric_limits<double>::quiet_NaN();
        } else if (x > (a + 1) / (2 + b + a) && 1 - x <= (b + 1) / (2 + b + a)) {
            ret = 1 - regularized_beta(b, a, 1 - x, epsilon, max_iterations);
        } else {
            ret = std::exp((a * std::log(x)) + (b * std::log(-x + 1)) - std::log(a) - log_beta(a, b)) * 1.0 /
                  evaluate_cf_beta(a, b, x, epsilon, max_iterations);
        }
        return ret;
    }

    // ported from: SpecialFunctions.cs trigamma(double x)
    static double trigamma(double x) {
        if (std::isnan(x) || std::isinf(x) || x == 0.0 || (x < 0.0 && std::floor(x) == x)) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        double y = 0;
        if (x < 0.0) {
            double val = (hecfda::kPi * (1.0 / std::sin(-hecfda::kPi * x)));
            y = -trigamma(-x + 1) + val * val;
        }
        if (x > 0.0 && x <= kSmallTrigamma) {
            y = 1 / (x * x) + kTrigammaC + kTrigammaC1 * x;
        }
        while (x > kSmallTrigamma && x < kLargeTrigamma) {
            y += 1.0 / (x * x);
            x++;
        }
        if (x >= kLargeTrigamma) {
            double z = 1.0 / (x * x);
            y += 0.5 * z +
                 (1.0 + z * (kB2 + z * (kB4 + z * (kB6 + z * (kB8 + z * kB10))))) / x;
        }
        return y;
    }

    // ported from: SpecialFunctions.cs singleParGammaPDF(double alpha, double x)
    static double single_par_gamma_pdf(double alpha, double x) {
        return (std::pow(x, (alpha - 1)) * std::exp(-x)) / gamma(alpha);
    }

    // ported from: SpecialFunctions.cs gammaDerivative(double alpha, double x)
    static double gamma_derivative(double alpha, double x) {
        if (alpha < 0 || x < 0 || std::fabs(x - alpha) / std::sqrt(alpha + 1.0) > 7.0) {
            return 0;
        }
        double log_X = std::log(x);
        double tol = 1.0e-11;
        double t;
        double r;
        double sum;
        double del;
        if (x < 5) {
            t = std::pow(x, alpha);
            r = (alpha * log_X - 1.0) / (alpha * alpha);
            sum = t * r;
            for (int i = 1; i <= 1000; i++) {
                double ai = alpha + i;
                t = -t * x / i;
                r = (ai * log_X - 1.0) / (ai * ai);
                del = r * t;
                sum += del;
                if (i > 1 && std::fabs(del) < (1.0 + std::fabs(sum)) * tol) {
                    return sum / std::exp(log_gamma(alpha)) - digamma(alpha) * reg_incomplete_gamma(alpha, x);
                }
            }
            // TODO: shouldn't come here. return error
            return 0;
        } else if (x > alpha + 30.0) {
            t = std::exp(-x + (alpha - 1.0) * log_X - log_gamma(alpha));
            r = log_X - digamma(alpha);
            sum = r * t;
            for (int i = 1; i < (int)(alpha - 1.0); i++) {
                double ami = alpha - i;
                t = t * ami / x;
                r = log_X - digamma(ami);
                del = r * t;
                sum += del;
                if (i > 1 && std::fabs(del) < (1.0 + std::fabs(sum)) * tol) {
                    return -sum;
                }
            }
            // TODO: shouldn't come here. return error
            return 0;
        } else {
            t = std::exp(-x + alpha * log_X - log_gamma(alpha + 1.0));
            r = log_X - digamma(alpha + 1.0);
            sum = r * t;
            for (int i = 1; i < 10000; i++) {
                t = t * x / (alpha + i);
                r = log_X - digamma(alpha + i + 1.0);
                del = r * t;
                sum += del;
                if (i > 1 && std::fabs(del) < (1.0 + std::fabs(sum)) * tol) {
                    return sum;
                }
            }
            // TODO: shouldn't come here. return error
            return 0;
        }
    }

   private:
    // ported from: SpecialFunctions.cs private constants used by digamma
    static constexpr double kGamma = 0.5772156649015329;      // GAMMA / EulerConst
    static constexpr double kGammaMinX = 1.0e-12;             // GAMMA_MINX
    static constexpr double kDigammaMinNegX = -1250;         // DIGAMMA_MINNEGX
    static constexpr double kCLimit = 49;                    // C_LIMIT
    static constexpr double kSLimit = 1.0e-5;                // S_LIMIT
    // ported from: SpecialFunctions.cs private constants used by trigamma
    static constexpr double kB10 = 5.0 / 66.0;
    static constexpr double kB2 = 1.0 / 6.0;
    static constexpr double kB4 = -1.0 / 30.0;
    static constexpr double kB6 = 1.0 / 42.0;
    static constexpr double kB8 = -1.0 / 30.0;
    // NOTE: verbatim transcription of upstream's `c = Math.Pow(Math.PI, 2.0 / 6.0)`. This evaluates
    // Pi^(1/3), which is almost certainly an upstream typo for Pi^2/6; it only affects the
    // x <= SMALL_TRIGAMMA (1e-4) branch of trigamma, so it is faithfully reproduced rather than
    // "fixed" to keep bit-for-bit parity with C# (the gate compares against real C# output).
    static inline const double kTrigammaC = std::pow(hecfda::kPi, 2.0 / 6.0);
    static constexpr double kTrigammaC1 = -2.4041138063191885;
    static constexpr double kSmallTrigamma = 1.0e-4;
    static constexpr double kLargeTrigamma = 8.0;

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

    // ported from: SpecialFunctions.cs evaluateCFBeta(double a, double b, double x, double epsilon, int maxIterations)
    // modified Lentz algorithm continued-fraction evaluation for the regularized beta function.
    static double evaluate_cf_beta(double a, double b, double x, double epsilon, int max_iterations) {
        const double small = 1.0e-50;
        double h_prev = 1.0;
        // use the value of small as epsilon criteria for zero checks
        if (std::fabs(h_prev - 0.0) < small) {
            h_prev = small;
        }
        int n = 1;
        double d_prev = 0.0;
        double c_prev = h_prev;
        double h_n = h_prev;
        while (n < max_iterations) {
            double aa = 1.0;
            double bb;
            double m;
            if (n % 2 == 0) {
                // even
                m = n / 2.0;
                bb = (m * (b - m) * x) / ((a + (2 * m) - 1) * (a + (2 * m)));
            } else {
                m = (n - 1.0) / 2.0;
                bb = -((a + m) * (a + b + m) * x) / ((a + (2 * m)) * (a + (2 * m) + 1.0));
            }
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
