// ported from: HEC.FDA.Statistics/Distributions/Gamma.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_GAMMA_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_GAMMA_HPP
#include <cmath>
#include <limits>
#include "hecfda/statistics/special_functions.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: Gamma.cs. INTERNAL helper class (upstream: `class Gamma`, no access modifier ->
// C# `internal`) -- NOT a ContinuousDistribution/IDistribution and NOT registered in the factory.
// Used as the workhorse behind ShiftedGamma, which in turn backs PearsonIII/LogPearson3's
// flood-frequency quantile math. Validated only indirectly, via ShiftedGammaTests.cs /
// fixtures/distributions/shifted_gamma.json.
class Gamma {
   public:
    // ported from: Gamma.cs Gamma(double shape, double scale)
    Gamma(double shape, double scale) : shape_(shape), scale_(scale) {}

    // ported from: Gamma.cs CDF(double x)
    double cdf(double x) const {
        if (x <= 0) {
            return 0;
        } else if (x >= std::numeric_limits<double>::max()) {
            return 1;
        } else {
            return SpecialFunctions::reg_incomplete_gamma(shape_, x / scale_);
        }
    }

    // ported from: Gamma.cs InverseCDF(double p)
    double inverse_cdf(double p) const {
        if (p <= 0.0) {
            p = 0.0000000000001;
        } else if (p >= 1.0) {
            p = .99999999999999;
        }

        double x_min = 0.0;
        double x_max = 1.0;
        for (int j = 0; j < 100; j++) {
            double p_max = cdf(x_max);
            if (p_max > p) {
                return inv_cdf_newton_bi_search(p, x_min, x_max, 1E-12, 100);
            }
            x_max *= 2.0;
        }
        return std::numeric_limits<double>::quiet_NaN();
    }

    // ported from: Gamma.cs PDF(double x)
    double pdf(double x) const {
        if (x <= 0.0) return 0.0;
        return std::exp(-x / scale_ + (shape_ - 1.0) * std::log(x) - shape_ * std::log(scale_) -
                         SpecialFunctions::log_gamma(shape_));
    }

   private:
    // ported from: Gamma.cs private const DX = 1E-10 (unused by the transcribed root-finder path
    // -- newtonBiSearch's tolX is threaded in as an explicit parameter from InverseCDF/
    // invCDFNewtonBiSearch, not read from this field -- but retained to mirror the C# field
    // layout exactly).
    static constexpr double kDx = 1E-10;

    // ported from: Gamma.cs invCDFNewtonBiSearch(double p, double xMin, double xMax, double tolX,
    // int maxIter)
    double inv_cdf_newton_bi_search(double p, double x_min, double x_max, double tol_x, int max_iter) const {
        return newton_bi_search(p, x_min, x_max, tol_x, max_iter);
    }

    // ported from: Gamma.cs private double f(double x)
    double f(double x) const { return cdf(x); }

    // ported from: Gamma.cs private double dfdx(double x)
    double dfdx(double x) const { return pdf(x); }

    // ported from: Gamma.cs newtonBiSearch(double y, double xMin, double xMax, double tolX, int
    // maxIter) -- GENERAL COMBINATION NEWTON / BISECTION SEARCH FORMULA. Transcribed EXACTLY
    // (branch structure, tolerances, iteration cap): this is the root-finder PearsonIII /
    // LogPearson3 quantile computations depend on transitively via ShiftedGamma.
    double newton_bi_search(double y, double x_min, double x_max, double tol_x, int max_iter) const {
        int j;
        double dfrts, dx, dxold, frts, fh, fl;
        double temp, xh, xl, rts, rts_old;
        fl = f(x_min) - y;
        fh = f(x_max) - y;
        if (fl == 0.0) {
            return x_min;
        }
        if (fh == 0.0) {
            return x_max;
        }
        if (fl < 0.0) {  // Orient the search so that f(xl) < 0.
            xl = x_min;
            xh = x_max;
        } else {
            xh = x_min;
            xl = x_max;
        }
        rts_old = xl;
        rts = 0.5 * (x_min + x_max);       // Initialize the guess for root,
        dxold = std::fabs(x_max - x_min);  // the stepsize before last,
        dx = dxold;                        // and the last step.
        frts = f(rts) - y;
        dfrts = dfdx(rts);
        for (j = 1; j <= max_iter; j++) {  // Loop over allowed iterations.
            if ((((rts - xh) * dfrts - frts) * ((rts - xl) * dfrts - frts) > 0.0)  // Bisect if Newton out of range,
                || (std::fabs(2.0 * frts) > std::fabs(dxold * dfrts))) {           // or not decreasing fast enough.
                dxold = dx;
                dx = 0.5 * (xh - xl);
                rts = xl + dx;
                if (xl == rts || rts == rts_old) {
                    return rts;  // Change in root is negligible.
                }
            } else {  // Newton step acceptable. Take it.
                dxold = dx;
                dx = frts / dfrts;
                temp = rts;
                rts -= dx;
                if (temp == rts) {
                    return rts;
                }
            }

            rts_old = rts;

            if (std::fabs(dx) < tol_x) {
                return rts;  // Convergence criterion.
            }
            frts = f(rts) - y;
            dfrts = dfdx(rts);
            // The one new function evaluation per iteration.
            if (frts < 0.0) {  // Maintain the bracket on the root.
                xl = rts;
            } else {
                xh = rts;
            }
        }
        return std::numeric_limits<double>::quiet_NaN();
    }

    double shape_;
    double scale_;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_GAMMA_HPP
