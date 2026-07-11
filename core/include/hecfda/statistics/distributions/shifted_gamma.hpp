// ported from: HEC.FDA.Statistics/Distributions/ShiftedGamma.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_SHIFTED_GAMMA_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_SHIFTED_GAMMA_HPP
#include <cmath>
#include "hecfda/statistics/distributions/gamma.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: ShiftedGamma.cs. PUBLIC helper class wrapping the internal Gamma -- like Gamma,
// this is NOT a ContinuousDistribution/IDistribution and is NOT registered in the factory.
// Validated directly via ShiftedGammaTests.cs / fixtures/distributions/shifted_gamma.json (which
// transitively pins Gamma's CDF/PDF/InverseCDF, including the Newton/bisection root-finder).
class ShiftedGamma {
   public:
    // ported from: ShiftedGamma.cs ShiftedGamma(double alpha, double beta, double shift)
    // alpha is the shape parameter, beta is the scale parameter.
    ShiftedGamma(double alpha, double beta, double shift) : gamma_(alpha, beta), shift_(shift) {}

    double shift() const { return shift_; }

    // ported from: ShiftedGamma.cs CDF(double x)
    double cdf(double x) const {
        double val = x - shift_;
        if (std::fabs(val) < .001) {
            val = 0;
        }
        return gamma_.cdf(val);
    }

    // ported from: ShiftedGamma.cs PDF(double x)
    double pdf(double x) const { return gamma_.pdf(x - shift_); }

    // ported from: ShiftedGamma.cs InverseCDF(double p)
    double inverse_cdf(double p) const { return gamma_.inverse_cdf(p) + shift_; }

   private:
    Gamma gamma_;
    double shift_;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_SHIFTED_GAMMA_HPP
