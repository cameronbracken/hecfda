// ported from: HEC.FDA.Model/paireddata/InterpolateQuantiles.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_INTERPOLATE_QUANTILES_HPP
#define HECFDA_MODEL_PAIRED_DATA_INTERPOLATE_QUANTILES_HPP
#include <cstddef>
#include <vector>
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
namespace hecfda {
namespace model {
namespace paired_data {

// Static-method helper class, VERBATIM port of InterpolateQuantiles.cs's single public method. No
// RNG usage anywhere in this file -- InterpolateOnX is a purely deterministic normal-probability
// quantile interpolation (z-score transform + PairedData::compose(), already ported in Task P2T2).
class InterpolateQuantiles {
   public:
    // ported from: InterpolateQuantiles.cs InterpolateOnX(IReadOnlyList<double>
    // inputExceedanceProbabilities, IReadOnlyList<double>
    // exceedanceProbabilitiesForWhichQuantilesAreRequired, IReadOnlyList<double>
    // inputDataForInterpolation). Interpolates on stage/flow for an expanded set of exceedance
    // probabilities: converts both the input and required exceedance probabilities to standard
    // normal z-scores (via Normal(0,1).InverseCDF(1-p) -- Normal(0.0, 1.0) here stands in for C#'s
    // `new Normal()` default ctor, matching the same substitution already used in lognormal.hpp),
    // then reuses PairedData::compose() to interpolate the input curve (z-score -> stage/flow) at
    // each required z-score. The composed curve's Yvals are the interpolated stage/flow values;
    // its Xvals (the requested exceedance probabilities, echoed back verbatim by compose()) are
    // discarded here, matching the C# method's `double[]`-only return type.
    static std::vector<double> interpolate_on_x(
        const std::vector<double>& input_exceedance_probabilities,
        const std::vector<double>& exceedance_probabilities_for_which_quantiles_are_required,
        const std::vector<double>& input_data_for_interpolation) {
        hecfda::statistics::distributions::Normal standard_normal_distribution(0.0, 1.0);

        std::vector<double> input_zs(input_exceedance_probabilities.size());
        for (std::size_t i = 0; i < input_exceedance_probabilities.size(); ++i) {
            input_zs[i] = standard_normal_distribution.inverse_cdf(1.0 - input_exceedance_probabilities[i]);
        }

        std::vector<double> needed_zs(exceedance_probabilities_for_which_quantiles_are_required.size());
        for (std::size_t i = 0; i < exceedance_probabilities_for_which_quantiles_are_required.size(); ++i) {
            needed_zs[i] = standard_normal_distribution.inverse_cdf(
                1.0 - exceedance_probabilities_for_which_quantiles_are_required[i]);
        }

        PairedData nonexceedance_zscore(exceedance_probabilities_for_which_quantiles_are_required, needed_zs);
        PairedData zscore_stage_flow(input_zs, input_data_for_interpolation);
        PairedData interpolated_frequency_curve = zscore_stage_flow.compose(nonexceedance_zscore);

        return interpolated_frequency_curve.yvals();
    }
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_INTERPOLATE_QUANTILES_HPP
