// ported from: HEC.FDA.Statistics/Histograms/IHistogram.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_HISTOGRAMS_I_HISTOGRAM_HPP
#define HECFDA_STATISTICS_HISTOGRAMS_I_HISTOGRAM_HPP
#include <cstdint>
#include <vector>
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
namespace hecfda {
namespace statistics {
namespace histograms {

// ported from: IHistogram.cs `public interface IHistogram : IDistribution`. In C# IHistogram
// extends IDistribution; here it is declared as a standalone abstract interface holding ONLY the
// histogram-specific surface (the properties/methods IHistogram adds on top of IDistribution).
// DynamicHistogram composes the IDistribution surface via ContinuousDistribution and this
// histogram surface via IHistogram (two unrelated bases), which avoids a virtual-inheritance
// diamond on IDistribution while giving DynamicHistogram exactly the same combined surface as the
// C# `class DynamicHistogram : ContinuousDistribution, IHistogram`.
class IHistogram {
   public:
    virtual ~IHistogram() = default;

    // ported from: IHistogram.cs properties.
    virtual bool is_converged() const = 0;
    virtual bool histogram_is_zero_valued() const = 0;
    virtual bool histogram_is_single_valued() const = 0;
    virtual std::int64_t converged_iteration() const = 0;
    virtual double bin_width() const = 0;
    virtual const std::vector<std::int64_t>& bin_counts() const = 0;
    virtual double min() const = 0;
    virtual double max() const = 0;
    virtual double sample_mean() const = 0;
    virtual double sample_variance() const = 0;
    virtual double standard_deviation() const = 0;
    virtual const ConvergenceCriteria& convergence_criteria() const = 0;

    // ported from: IHistogram.cs methods.
    virtual void add_observation_to_histogram(double observation, std::int64_t iteration_index) = 0;
    virtual void add_observations_to_histogram(const std::vector<double>& observations) = 0;
    virtual void force_de_queue() = 0;
    virtual bool is_histogram_converged(double upperq, double lowerq) = 0;
    virtual std::int64_t estimate_iterations_remaining(double upperq, double lowerq) = 0;
    virtual std::int64_t find_bin_count(double x, bool cumulative = true) const = 0;
};

}  // namespace histograms
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_HISTOGRAMS_I_HISTOGRAM_HPP
