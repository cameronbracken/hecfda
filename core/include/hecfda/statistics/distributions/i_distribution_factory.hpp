// ported from: HEC.FDA.Statistics/Distributions/IDistributionFactory.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_FACTORY_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_FACTORY_HPP
#include <memory>
#include <stdexcept>
#include <vector>
#include "hecfda/statistics/distributions/i_distribution.hpp"
#include "hecfda/statistics/distributions/i_distribution_enum.hpp"
#include "hecfda/statistics/distributions/deterministic.hpp"
#include "hecfda/statistics/distributions/logpearson3.hpp"
#include "hecfda/statistics/distributions/lognormal.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/distributions/triangular.hpp"
#include "hecfda/statistics/distributions/truncated_lognormal.hpp"
#include "hecfda/statistics/distributions/truncated_logpearson3.hpp"
#include "hecfda/statistics/distributions/truncated_normal.hpp"
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
//   DistributionType::Deterministic -> FactoryDeterministic(value)
//                                       params = [value]
//   DistributionType::LogNormal  -> FactoryLogNormal(mean, stDev, sampleSize)
//                                    params = [mean, stDev, sampleSize] (moments of the
//                                    NATURAL-log-scale data -- see lognormal.hpp)
//   DistributionType::TruncatedNormal -> FactoryTruncatedNormal(mean, stDev, min, max, sampleSize)
//                                    params = [mean, stDev, min, max, sampleSize]
//   DistributionType::TruncatedLogNormal (PORT-INTERNAL key, 1005 -- see i_distribution_enum.hpp;
//                                    instance type() still returns DistributionType::Normal, per
//                                    the real C# `TruncatedLogNormal.Type => IDistributionEnum.
//                                    Normal`) -> params = [mean, stDev, min, max, sampleSize]
//                                    (moments of the NATURAL-log-scale data, matching
//                                    TruncatedLogNormal.cs's ctor -- see truncated_lognormal.hpp)
//   DistributionType::LogPearsonIII -> FactoryLogPearsonIII(mean, stDev, skew, sampleSize)
//                                    params = [mean, stDev, skew, sampleSize] (moments of the
//                                    LOG BASE 10-scale data -- see logpearson3.hpp)
//   DistributionType::TruncatedLogPearson3 (PORT-INTERNAL key, 1006 -- see
//                                    i_distribution_enum.hpp; instance type() still returns
//                                    DistributionType::LogPearsonIII, per the real C#
//                                    `TruncatedLogPearson3.Type => IDistributionEnum.
//                                    LogPearsonIII`) -> params = [mean, stDev, skew, min, max,
//                                    sampleSize] (moments of the LOG BASE 10-scale data, matching
//                                    TruncatedLogPearson3.cs's ctor -- see
//                                    truncated_logpearson3.hpp)
//
// Later distribution tasks add one `case` each (IHistogram, Empirical), documenting their own
// params order here in the same style.
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
            case DistributionType::Deterministic:
                return std::make_unique<Deterministic>(params.at(0));
            case DistributionType::LogNormal:
                return std::make_unique<LogNormal>(params.at(0), params.at(1), static_cast<long>(params.at(2)));
            case DistributionType::TruncatedNormal:
                return std::make_unique<TruncatedNormal>(params.at(0), params.at(1), params.at(2), params.at(3),
                                                           static_cast<long>(params.at(4)));
            case DistributionType::TruncatedLogNormal:
                return std::make_unique<TruncatedLogNormal>(params.at(0), params.at(1), params.at(2),
                                                              params.at(3), static_cast<long>(params.at(4)));
            case DistributionType::LogPearsonIII:
                return std::make_unique<LogPearson3>(params.at(0), params.at(1), params.at(2),
                                                       static_cast<long>(params.at(3)));
            case DistributionType::TruncatedLogPearson3:
                return std::make_unique<TruncatedLogPearson3>(params.at(0), params.at(1), params.at(2),
                                                                params.at(3), params.at(4),
                                                                static_cast<long>(params.at(5)));
            case DistributionType::NotSupported:
            case DistributionType::IHistogram:
            case DistributionType::Empirical:
            default:
                throw std::invalid_argument("IDistributionFactory::create: unsupported DistributionType");
        }
    }
};

}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_FACTORY_HPP
