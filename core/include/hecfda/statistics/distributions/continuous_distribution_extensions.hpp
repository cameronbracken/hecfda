// ported from: HEC.FDA.Model/extensions/ContinuousDistributionExtensions.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_CONTINUOUS_DISTRIBUTION_EXTENSIONS_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_CONTINUOUS_DISTRIBUTION_EXTENSIONS_HPP
#include <cstddef>
#include <memory>
#include <vector>
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/utilities/double_global_statics.hpp"
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/distributions/i_distribution.hpp"
namespace hecfda {
namespace statistics {
namespace distributions {

// required_exceedance_probabilities() (DoubleGlobalStatics.RequiredExceedanceProbabilities) is
// the analytical-frequency curve's fixed 173-point exceedance-probability grid, already ported in
// hecfda::model::utilities (double_global_statics.hpp, shared with
// GraphicalFrequencyUncertaintyCalculators). Re-exposed here via using-declaration -- NOT
// redefined -- so callers of bootstrap_to_paired_data can write
// hecfda::statistics::distributions::required_exceedance_probabilities() without reaching into an
// unrelated namespace; both names resolve to the exact same static vector (DRY, single source of
// truth for the 173 literal values).
using hecfda::model::utilities::required_exceedance_probabilities;

// ported from: ContinuousDistributionExtensions.cs BootstrapToPairedData(this ContinuousDistribution
// continuousDistribution, long iterationNumber, double[] ExceedanceProbabilities, bool
// computeIsDeterministic = false), lines 48-71. This is the analytical-frequency realization the
// EAD compute uses (Phase 5 Task 8): given a fitted flow-frequency distribution (e.g. LogPearson3),
// realize a PairedData flow-frequency curve either from the distribution's own central-tendency
// fit (computeIsDeterministic == true, no RNG) or from a parametric-uncertainty bootstrap resample
// of it (ContinuousDistribution::sample(iterationNumber) -- requires
// generate_random_samples_of_numbers(...) to have been called first; throws otherwise, matching
// the C# exception). `prob = 1 - ExceedanceProbabilities[i]` is the corresponding NON-exceedance
// (cumulative) probability handed to InverseCDF; x values are these non-exceedance probabilities
// (increasing, since ExceedanceProbabilities is descending), y values the resulting increasing
// discharge/flow realizations -- upstream comment: "same exceedance probs as graphical and as
// 1.4.3" / "y values in increasing order". The two-overload C# design (this one takes an
// iterationNumber + bool; the sibling BootstrapToPairedData(IProvideRandomNumbers, ...) always
// samples via NextRandomSequence) is intentionally NOT both ported -- only the iterationNumber
// overload is needed by the EAD compute path (Task 8's WHERE THIS FITS), per this task's scope.
inline hecfda::model::paired_data::PairedData bootstrap_to_paired_data(
    const ContinuousDistribution& continuous_distribution, long iteration_number,
    const std::vector<double>& exceedance_probabilities, bool compute_is_deterministic = false) {
    std::unique_ptr<IDistribution> owned_bootstrap;
    const IDistribution* bootstrap = &continuous_distribution;
    if (!compute_is_deterministic) {
        owned_bootstrap = continuous_distribution.sample(iteration_number);
        bootstrap = owned_bootstrap.get();
    }
    std::size_t count = exceedance_probabilities.size();
    std::vector<double> x(count);
    std::vector<double> y(count);
    for (std::size_t i = 0; i < count; ++i) {
        double prob = 1.0 - exceedance_probabilities[i];
        x[i] = prob;
        y[i] = bootstrap->inverse_cdf(prob);
    }
    return hecfda::model::paired_data::PairedData(std::move(x), std::move(y));
}

}  // namespace distributions
}  // namespace statistics
}  // namespace hecfda
#endif  // HECFDA_STATISTICS_DISTRIBUTIONS_CONTINUOUS_DISTRIBUTION_EXTENSIONS_HPP
