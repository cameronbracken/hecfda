// ported from: HEC.FDA.Statistics/Distributions/IDistributionFactory.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_FACTORY_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_FACTORY_HPP
#include <memory>
#include <stdexcept>
#include <vector>
#include "hecfda/statistics/distributions/i_distribution.hpp"
#include "hecfda/statistics/distributions/i_distribution_enum.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/distributions/triangular.hpp"
#include "hecfda/statistics/distributions/uniform.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// ported from: IDistributionFactory.cs's Factory* static methods, collapsed into a single
// `create(type, params)` switch (per the Task A4 brief) rather than one C++ function per C#
// `Factory*` overload. `params` order matches each C# `Factory*` signature exactly:
//
//   DistributionType::Normal     -> FactoryNormal(mean, stDev, sampleSize)
//                                    params = [mean, stDev, sampleSize]
//   DistributionType::Uniform    -> FactoryUniform(min, max, sampleSize)
//                                    params = [min, max, sampleSize]
//   DistributionType::Triangular -> FactoryTriangular(min, mostLikely, max, sampleSize)
//                                    params = [min, mostLikely, max, sampleSize]
//
// Later distribution tasks add one `case` each (LogPearsonIII, LogNormal, Deterministic,
// IHistogram, Empirical, TruncatedNormal), documenting their own params order here in the same
// style.
class IDistributionFactory {
   public:
    static std::unique_ptr<IDistribution> create(DistributionType type, const std::vector<double>& params) {
        switch (type) {
            case DistributionType::Normal:
                return std::make_unique<Normal>(params.at(0), params.at(1), static_cast<long>(params.at(2)));
            case DistributionType::Uniform:
                return std::make_unique<Uniform>(params.at(0), params.at(1), static_cast<long>(params.at(2)));
            case DistributionType::Triangular:
                return std::make_unique<Triangular>(params.at(0), params.at(1), params.at(2),
                                                      static_cast<long>(params.at(3)));
            case DistributionType::NotSupported:
            case DistributionType::LogPearsonIII:
            case DistributionType::LogNormal:
            case DistributionType::Deterministic:
            case DistributionType::IHistogram:
            case DistributionType::Empirical:
            case DistributionType::TruncatedNormal:
            default:
                throw std::invalid_argument("IDistributionFactory::create: unsupported DistributionType");
        }
    }
};

}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_FACTORY_HPP
