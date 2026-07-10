// ported from: HEC.FDA.Statistics/Distributions/IDistribution.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_HPP
#include <vector>
namespace hecfda {
namespace statistics {
namespace distributions {
enum class DistributionType { Normal, LogNormal, Triangular, Uniform, Deterministic /* extended later */ };
class IDistribution {
   public:
    virtual ~IDistribution() = default;
    virtual DistributionType type() const = 0;
    virtual double pdf(double x) const = 0;
    virtual double cdf(double x) const = 0;
    virtual double inverse_cdf(double p) const = 0;
};
}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_HPP
