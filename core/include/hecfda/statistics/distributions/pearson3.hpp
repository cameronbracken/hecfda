// ported from: HEC.FDA.Statistics/Distributions/PearsonIII.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_PEARSON3_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_PEARSON3_HPP
#include <cmath>
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/distributions/shifted_gamma.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: PearsonIII.cs. PUBLIC plain helper class (upstream: `public class PearsonIII`, no
// base type) -- NOT a ContinuousDistribution/IDistribution and NOT registered in the factory.
// Delegates to the already-ported Normal (no-skew case) or ShiftedGamma (skewed case), used as a
// building block behind LogPearson3. Validated directly via PearsonIIITests.cs /
// fixtures/distributions/pearson3.json; only positive-skew cases are covered by the upstream
// test, but both skew branches are transcribed verbatim below.
class PearsonIII {
   public:
    // ported from: PearsonIII.cs PearsonIII(double mean, double sd, double skew, Int64 n = 1)
    PearsonIII(double mean, double sd, double skew, long n = 1)
        : mean_(mean), sd_(sd), skew_(skew), sample_size_(n) {}

    double mean() const { return mean_; }
    double standard_deviation() const { return sd_; }
    double skewness() const { return skew_; }
    long sample_size() const { return sample_size_; }

    // ported from: PearsonIII.cs CDF(double x)
    double cdf(double x) const {
        if (std::fabs(skew_) < kNoSkewness) {
            // a PearsonIII distribution with no skew is normally distributed.
            Normal norm(mean_, sd_);
            return norm.cdf(x);
        } else {
            // a skewed PearsonIII distribution is a shifted gamma distribution
            double shift;
            double alpha = 4.0 / (skew_ * skew_);
            double beta = 0.5 * sd_ * skew_;
            // positively skewed distribution
            if (skew_ > 0) {
                shift = mean_ - 2.0 * sd_ / skew_;
                ShiftedGamma gamma(alpha, beta, shift);
                return gamma.cdf(x);
            }
            // negatively skewed distribution
            else {
                beta = -beta;
                shift = -mean_ + 2.0 * sd_ / skew_;
                ShiftedGamma gamma(alpha, beta, shift);
                return 1 - gamma.cdf(-x);
            }
        }
    }

    // ported from: PearsonIII.cs PDF(double x)
    double pdf(double x) const {
        if (std::fabs(skew_) < kNoSkewness) {
            // a PearsonIII distribution with no skew is normally distributed.
            Normal norm(mean_, sd_);
            return norm.pdf(x);
        } else {
            // a skewed PearsonIII distribution is a shifted gamma distribution
            double shift, alpha = 4.0 / (skew_ * skew_), beta = 0.5 * sd_ * skew_;
            // positively skewed distribution
            if (skew_ > 0) {
                shift = mean_ - 2.0 * sd_ / skew_;
                ShiftedGamma gamma(alpha, beta, shift);
                return gamma.pdf(x);
            }
            // negatively skewed distribution
            else {
                beta = -beta;
                shift = -mean_ + 2.0 * sd_ / skew_;
                ShiftedGamma gamma(alpha, beta, shift);
                return -gamma.pdf(x);
            }
        }
    }

    // ported from: PearsonIII.cs InverseCDF(double p)
    double inverse_cdf(double p) const {
        if (std::fabs(skew_) < kNoSkewness) {
            // a PearsonIII distribution with no skew is normally distributed.
            Normal norm(mean_, sd_);
            return norm.inverse_cdf(p);
        } else {
            // a skewed PearsonIII distribution is a shifted gamma distribution
            double shift = 0;
            double alpha = 4.0 / (skew_ * skew_);
            double beta = 0.5 * sd_ * skew_;
            // positively skewed distribution
            if (skew_ > 0) {
                shift = mean_ - 2.0 * sd_ / skew_;
                ShiftedGamma gamma(alpha, beta, shift);
                return gamma.inverse_cdf(p);
            }
            // negatively skewed distribution
            else {
                beta = -beta;
                shift = -mean_ + 2.0 * sd_ / skew_;
                ShiftedGamma gamma(alpha, beta, shift);
                return -gamma.inverse_cdf(1 - p);
            }
        }
    }

   private:
    // ported from: PearsonIII.cs private readonly double _NoSkewness = 0.00001
    static constexpr double kNoSkewness = 0.00001;

    double mean_;
    double sd_;
    double skew_;
    long sample_size_;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_PEARSON3_HPP
