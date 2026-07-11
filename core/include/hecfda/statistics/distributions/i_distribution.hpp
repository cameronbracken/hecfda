// ported from: HEC.FDA.Statistics/Distributions/IDistribution.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_HPP
#include <memory>
#include <vector>
#include "hecfda/statistics/distributions/i_distribution_enum.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: IDistribution.cs. Scoped to the numeric/statistical surface the C++ port needs
// (Task A4's brief: type(), sample_size(), pdf/cdf/inverse_cdf, fit(data), sample(packet),
// equals). DONE_WITH_CONCERNS (scoped out, not ported -- no equivalent consumer in Phase 1):
//  - `Truncated` (bool property): unused by any distribution ported so far.
//  - `ToXML()` / `ContinuousDistribution.FromXML(...)`: reflection-driven XML
//    (de)serialization; no native C++ analogue needed by the statistics core.
class IDistribution {
   public:
    virtual ~IDistribution() = default;

    // ported from: IDistribution.Type
    virtual DistributionType type() const = 0;
    // ported from: IDistribution.SampleSize
    virtual long sample_size() const = 0;

    // ported from: IDistribution.PDF(double x)
    virtual double pdf(double x) const = 0;
    // ported from: IDistribution.CDF(double x)
    virtual double cdf(double x) const = 0;
    // ported from: IDistribution.InverseCDF(double p)
    virtual double inverse_cdf(double p) const = 0;

    // ported from: IDistribution.Equals(IDistribution distribution)
    virtual bool equals(const IDistribution& distribution) const = 0;

    // ported from: IDistribution.Fit(double[] data)
    virtual std::unique_ptr<IDistribution> fit(const std::vector<double>& data) const = 0;

    // ported from: IDistribution.Sample(double[] randomPacket)
    virtual std::unique_ptr<IDistribution> sample(const std::vector<double>& random_packet) const = 0;
};

}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_HPP
