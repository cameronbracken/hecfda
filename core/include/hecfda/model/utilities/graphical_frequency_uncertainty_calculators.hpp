// ported from: HEC.FDA.Model/utilities/GraphicalFrequencyUncertaintyCalculators.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_UTILITIES_GRAPHICAL_FREQUENCY_UNCERTAINTY_CALCULATORS_HPP
#define HECFDA_MODEL_UTILITIES_GRAPHICAL_FREQUENCY_UNCERTAINTY_CALCULATORS_HPP
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include "hecfda/model/paired_data/curve_meta_data.hpp"
#include "hecfda/model/paired_data/interpolate_quantiles.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/utilities/double_global_statics.hpp"
#include "hecfda/statistics/distributions/i_distribution.hpp"
#include "hecfda/statistics/distributions/lognormal.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
namespace hecfda {
namespace model {
namespace utilities {

// required_exceedance_probabilities() (DoubleGlobalStatics.RequiredExceedanceProbabilities) now
// lives in double_global_statics.hpp -- moved there in Task 5 once a second consumer
// (bootstrap_to_paired_data) needed the same constant; see that header's comment.

// ported from: GraphicalFrequencyUncertaintyCalculators.cs. Static utility class implementing
// Beth Faber's "Less Simple Method" (HEC-FDA Technical Reference Manual, CPD-72a) for quantifying
// uncertainty about a graphical frequency relationship. NO RNG usage anywhere in this file --
// LessSimpleMethod is a purely deterministic transform from (exceedance probabilities, stage/flow
// values) to (expanded exceedance probabilities, per-point Normal/LogNormal distributions); any
// later sampling from those returned distributions happens elsewhere (IDistribution::sample /
// UncertainPairedData, already ported in Phase 1 / Task P2T3).
class GraphicalFrequencyUncertaintyCalculators {
   public:
    using IDistribution = hecfda::statistics::distributions::IDistribution;

    // Default lower exceedance probability threshold (e.g., 0.01 = 100-year event). Below this
    // threshold, standard errors are held constant.
    static constexpr double kLessSimpleFrequentEventThreshold = 0.01;
    // Default higher exceedance probability threshold (e.g., 0.99 = annual event). Above this
    // threshold, standard errors are held constant.
    static constexpr double kLessSimpleRareEventThreshold = 0.99;
    static constexpr double kLessSimpleMaxExceedProb = 0.9999;
    static constexpr double kLessSimpleMinExceedProb = 0.0001;
    static constexpr double kMaxIntervalForExtrapolation = 0.0001;

    // ported from: GraphicalFrequencyUncertaintyCalculators.cs LessSimpleMethod(double[]
    // exceedanceProbabilities, double[] stagesOrFlows, bool usingStagesNotFlows, int
    // equivalentRecordLength = 10, CurveMetaData curveMetaData = null). Returns (expanded
    // exceedance probabilities, per-point distributions) -- Normal for stages, LogNormal for flows
    // -- NOT an UncertainPairedData; per the real C# return type `(double[],
    // ContinuousDistribution[])`, the caller (GraphicalUncertainPairedData, Task P2T4b) is expected
    // to combine these two arrays with a CurveMetaData to build one.
    //
    // SEVERANCE: the C# ArgumentNullException guards on `exceedanceProbabilities`/`stagesOrFlows`
    // being null have no analog here -- `const std::vector<double>&` cannot be null (same rationale
    // as PairedData's ctor not having a "null Xvals/Yvals" representation; see paired_data.hpp's
    // class comment). The three ArgumentException guards that ARE reachable through non-null
    // vectors -- mismatched lengths, insufficient points, invalid equivalentRecordLength -- are
    // ported verbatim below, throwing std::invalid_argument with the same message text.
    //
    // `curve_meta_data` is transcribed verbatim (defaulting to a fresh CurveMetaData(), matching
    // C#'s `curveMetaData ?? new CurveMetaData()`) even though -- per the real C# source -- it is a
    // genuine upstream dead parameter: `metadata` is assigned on the very next line and never read
    // again anywhere in the method body. Kept here for signature fidelity (and because a Task
    // P2T4b caller may still want to pass one through), but it has zero effect on the two returned
    // values; `(void)metadata` below silences the resulting "unused variable" warning the same way
    // the C# source's own dead assignment would if C# warned on it.
    static std::pair<std::vector<double>, std::vector<std::unique_ptr<IDistribution>>> less_simple_method(
        const std::vector<double>& exceedance_probabilities, const std::vector<double>& stages_or_flows,
        bool using_stages_not_flows, int equivalent_record_length = 10,
        const hecfda::model::paired_data::CurveMetaData& curve_meta_data =
            hecfda::model::paired_data::CurveMetaData()) {
        double frequent_event_threshold = kLessSimpleFrequentEventThreshold;
        double rare_event_threshold = kLessSimpleRareEventThreshold;

        if (exceedance_probabilities.size() != stages_or_flows.size()) {
            throw std::invalid_argument("Exceedance probabilities and stages/flows must have the same length.");
        }
        if (exceedance_probabilities.size() < 2) {
            throw std::invalid_argument("At least 2 data points are required for the Less Simple Method.");
        }
        if (equivalent_record_length < 1) {
            throw std::invalid_argument("Equivalent record length must be at least 1 year.");
        }

        hecfda::model::paired_data::CurveMetaData metadata = curve_meta_data;
        (void)metadata;

        // Step 1: Convert flows to log space if using flows (matching GraphicalDistribution behavior)
        std::vector<double> stage_or_logged_flow_values;
        if (using_stages_not_flows) {
            stage_or_logged_flow_values = stages_or_flows;
        } else {
            stage_or_logged_flow_values = log_flows(stages_or_flows);
        }

        // Step 2: Extrapolate the frequency function to cover 0.9999 to 0.0001
        hecfda::model::paired_data::PairedData extrapolated_frequency_function =
            extrapolate_frequency_function(exceedance_probabilities, stage_or_logged_flow_values);

        // Step 3: Fill with required exceedance probabilities
        std::vector<double> filled_exceedance_probabilities =
            fill_input_exceedance_probabilities_with_required_points(extrapolated_frequency_function.xvals());

        // Step 4: Interpolate quantiles for the filled probabilities
        std::vector<double> interpolated_stage_or_log_flow_values =
            hecfda::model::paired_data::InterpolateQuantiles::interpolate_on_x(
                extrapolated_frequency_function.xvals(), filled_exceedance_probabilities,
                extrapolated_frequency_function.yvals());

        // Step 5: Compute standard errors using the Less Simple Method
        std::vector<double> standard_errors =
            compute_standard_errors(filled_exceedance_probabilities, interpolated_stage_or_log_flow_values,
                                     equivalent_record_length, frequent_event_threshold, rare_event_threshold);

        // Step 6: Construct distributions based on whether we're using stages or flows
        std::vector<std::unique_ptr<IDistribution>> distributions = construct_distributions(
            interpolated_stage_or_log_flow_values, standard_errors, using_stages_not_flows);

        // Step 7: Return the expanded exceedance probabilities + distributions
        return {std::move(filled_exceedance_probabilities), std::move(distributions)};
    }

   private:
    // ported from: GraphicalFrequencyUncertaintyCalculators.cs LogFlows(double[] unloggedFlows)
    static std::vector<double> log_flows(const std::vector<double>& unlogged_flows) {
        std::vector<double> logged_flows(unlogged_flows.size());
        const double min_flow = 0.01;  // for log conversion not to fail
        for (std::size_t i = 0; i < unlogged_flows.size(); ++i) {
            if (unlogged_flows[i] < min_flow) {
                logged_flows[i] = std::log(min_flow);
            } else {
                logged_flows[i] = std::log(unlogged_flows[i]);
            }
        }
        return logged_flows;
    }

    // ported from: ExtrapolateFrequencyFunction(double[] exceedanceProbabilities, double[]
    // stageOrLoggedFlowValues). NOTE: the returned PairedData's Xvals are NOT necessarily in
    // increasing order (exceedanceProbabilities is typically supplied high-to-low) -- this is safe
    // here because only .xvals()/.yvals() are ever read off the result (never .f()/.f_inverse()/
    // .integrate(), the only PairedData members that require increasing X).
    //
    // UPSTREAM QUIRK, reproduced verbatim: the "more frequent end" block below unconditionally
    // inserts exactly one element into extrapolated_exceedance_probabilities, but the number of
    // elements it inserts into extrapolated_flow_or_stage_values depends on
    // smallest_input_flow_or_stage's sign/magnitude -- 1 insert for the common cases, but 2 inserts
    // (an array-length MISMATCH against the probabilities array) when
    // -1.0e-4 <= smallest_input_flow_or_stage < 0 (the first `if` inserts once, then the second
    // if/else-if/else chain's final `else` inserts again, since neither `> 0` nor `< -1.0e-4` is
    // true). This is real upstream behavior (see the original C# comment: "matches the original
    // GraphicalDistribution logic exactly, including the independent first if statement and the
    // second if-else chain") -- not "fixed" here.
    static hecfda::model::paired_data::PairedData extrapolate_frequency_function(
        const std::vector<double>& exceedance_probabilities,
        const std::vector<double>& stage_or_logged_flow_values) {
        double tolerated_difference = kMaxIntervalForExtrapolation;
        double maximum_exceedance_probability = kLessSimpleMaxExceedProb;
        double minimum_exceedance_probability = kLessSimpleMinExceedProb;

        std::vector<double> extrapolated_flow_or_stage_values;
        std::vector<double> extrapolated_exceedance_probabilities;
        for (std::size_t i = 0; i < exceedance_probabilities.size(); ++i) {
            extrapolated_flow_or_stage_values.push_back(stage_or_logged_flow_values[i]);
            extrapolated_exceedance_probabilities.push_back(exceedance_probabilities[i]);
        }

        // More frequent end of the frequency curve
        if (maximum_exceedance_probability - extrapolated_exceedance_probabilities.front() > tolerated_difference) {
            double smallest_input_flow_or_stage = extrapolated_flow_or_stage_values[0];
            extrapolated_exceedance_probabilities.insert(extrapolated_exceedance_probabilities.begin(),
                                                           maximum_exceedance_probability);

            if (smallest_input_flow_or_stage < 0) {
                extrapolated_flow_or_stage_values.insert(extrapolated_flow_or_stage_values.begin(),
                                                          1.001 * smallest_input_flow_or_stage);
            }

            if (smallest_input_flow_or_stage > 0) {
                extrapolated_flow_or_stage_values.insert(extrapolated_flow_or_stage_values.begin(),
                                                          0.999 * smallest_input_flow_or_stage);
            } else if (smallest_input_flow_or_stage < -1.0e-4) {
                extrapolated_flow_or_stage_values[0] = 1.001 * smallest_input_flow_or_stage;
            } else {
                extrapolated_flow_or_stage_values.insert(extrapolated_flow_or_stage_values.begin(), -1.0e-4);
            }
        }

        // Less frequent end of the frequency curve
        if (extrapolated_exceedance_probabilities.back() - minimum_exceedance_probability > tolerated_difference) {
            hecfda::statistics::distributions::Normal standard_normal_distribution(0.0, 1.0);
            std::size_t n = extrapolated_exceedance_probabilities.size();
            double penultimate_input_exceedance_probability = extrapolated_exceedance_probabilities[n - 2];
            double last_input_exceedance_probability = extrapolated_exceedance_probabilities.back();
            double z_value_of_min = standard_normal_distribution.inverse_cdf(minimum_exceedance_probability);
            double z_value_of_penultimate_input_probability =
                standard_normal_distribution.inverse_cdf(penultimate_input_exceedance_probability);
            double z_value_of_last_input_probability =
                standard_normal_distribution.inverse_cdf(last_input_exceedance_probability);
            double penultimate_input_flow_or_stage = extrapolated_flow_or_stage_values[n - 2];
            double last_input_flow_or_stage = extrapolated_flow_or_stage_values.back();
            double c = (z_value_of_last_input_probability - z_value_of_penultimate_input_probability) /
                       (z_value_of_min - z_value_of_penultimate_input_probability);
            double upper_flow_or_stage = ((last_input_flow_or_stage - penultimate_input_flow_or_stage) +
                                           c * penultimate_input_flow_or_stage) /
                                          c;
            extrapolated_flow_or_stage_values.push_back(upper_flow_or_stage);
            extrapolated_exceedance_probabilities.push_back(minimum_exceedance_probability);
        }

        return hecfda::model::paired_data::PairedData(std::move(extrapolated_exceedance_probabilities),
                                                        std::move(extrapolated_flow_or_stage_values));
    }

    // ported from: FillInputExceedanceProbabilitiesWithRequiredPoints(double[]
    // inputExceedanceProbabilities). `List<double>.Sort((a, b) => b.CompareTo(a))` (descending,
    // unstable introsort) -> std::sort with std::greater<double> (descending, unstable introsort) --
    // same ordering guarantee.
    static std::vector<double> fill_input_exceedance_probabilities_with_required_points(
        const std::vector<double>& input_exceedance_probabilities) {
        std::vector<double> all_probabilities = required_exceedance_probabilities();
        for (double probability : input_exceedance_probabilities) {
            bool already_present =
                std::find(all_probabilities.begin(), all_probabilities.end(), probability) != all_probabilities.end();
            if (!already_present) {
                all_probabilities.push_back(probability);
            }
        }
        std::sort(all_probabilities.begin(), all_probabilities.end(), std::greater<double>());
        return all_probabilities;
    }

    // ported from: ComputeStandardErrors(double[] exceedanceProbabilities, double[] stagesOrFlows,
    // int equivalentRecordLength, double frequentEventThreshold, double rareEventThreshold)
    static std::vector<double> compute_standard_errors(const std::vector<double>& exceedance_probabilities,
                                                         const std::vector<double>& stages_or_flows,
                                                         int equivalent_record_length,
                                                         double frequent_event_threshold,
                                                         double rare_event_threshold) {
        std::vector<double> standard_errors(exceedance_probabilities.size());

        // Step 1: Find transition indices where we stop computing and start holding constant
        int frequent_event_index = find_closest_probability_index(exceedance_probabilities, frequent_event_threshold);
        int rare_event_index = find_closest_probability_index(exceedance_probabilities, rare_event_threshold);

        // Step 2: Compute standard errors for interior points (points that have neighbors on both sides)
        compute_interior_standard_errors(exceedance_probabilities, stages_or_flows, equivalent_record_length,
                                          standard_errors);

        // Step 3: Compute standard errors for boundary points (first and last)
        compute_boundary_standard_errors(exceedance_probabilities, stages_or_flows, equivalent_record_length,
                                          standard_errors);

        // Step 4: Apply constant standard errors at distribution tails
        apply_constant_standard_errors_at_tails(standard_errors, frequent_event_index, rare_event_index);

        return standard_errors;
    }

    // ported from: FindClosestProbabilityIndex(double[] exceedanceProbabilities, double targetProbability)
    static int find_closest_probability_index(const std::vector<double>& exceedance_probabilities,
                                                double target_probability) {
        int closest_index = -1;
        double min_distance = std::numeric_limits<double>::max();

        for (std::size_t i = 0; i < exceedance_probabilities.size(); ++i) {
            double distance = std::fabs(exceedance_probabilities[i] - target_probability);
            if (distance < min_distance) {
                closest_index = static_cast<int>(i);
                min_distance = distance;
            }
        }
        return closest_index;
    }

    // ported from: ComputeInteriorStandardErrors(...). Processes indices [1, Length-2] (skipping
    // first and last); `i + 1 < size()` is the size_t-safe form of C#'s `i < Length - 1` (avoids
    // unsigned underflow when size() is 0 or 1, for which neither loop runs any iterations either way).
    static void compute_interior_standard_errors(const std::vector<double>& exceedance_probabilities,
                                                   const std::vector<double>& stages_or_flows,
                                                   int equivalent_record_length,
                                                   std::vector<double>& standard_errors) {
        for (std::size_t i = 1; i + 1 < exceedance_probabilities.size(); ++i) {
            double non_exceedance_probability = 1.0 - exceedance_probabilities[i];
            double slope = compute_slope(exceedance_probabilities, stages_or_flows, i);
            standard_errors[i] = equation6_standard_error(non_exceedance_probability, slope, equivalent_record_length);
        }
    }

    // ported from: ComputeBoundaryStandardErrors(...). Since we can't compute slope at boundaries,
    // borrow the slope from the nearest interior point.
    static void compute_boundary_standard_errors(const std::vector<double>& exceedance_probabilities,
                                                   const std::vector<double>& stages_or_flows,
                                                   int equivalent_record_length,
                                                   std::vector<double>& standard_errors) {
        std::size_t last_index = exceedance_probabilities.size() - 1;

        // First point: use slope from second point (index 1)
        if (exceedance_probabilities.size() > 1) {
            double non_exceedance_prob_first = 1.0 - exceedance_probabilities[0];
            double slope_at_second_point = compute_slope(exceedance_probabilities, stages_or_flows, 1);
            standard_errors[0] =
                equation6_standard_error(non_exceedance_prob_first, slope_at_second_point, equivalent_record_length);
        }

        // Last point: use slope from second-to-last point (index Length-2)
        if (exceedance_probabilities.size() > 2) {
            double non_exceedance_prob_last = 1.0 - exceedance_probabilities[last_index];
            double slope_at_second_to_last =
                compute_slope(exceedance_probabilities, stages_or_flows, last_index - 1);
            standard_errors[last_index] =
                equation6_standard_error(non_exceedance_prob_last, slope_at_second_to_last, equivalent_record_length);
        }
    }

    // ported from: ApplyConstantStandardErrorsAtTails(double[] standardErrors, int
    // frequentEventIndex, int rareEventIndex). Frequent events (low exceedance prob, towards the
    // right/end of the array): held constant from frequentEventIndex to the end. Rare events (high
    // exceedance prob, towards the left/start of the array): held constant from the start up to
    // rareEventIndex.
    static void apply_constant_standard_errors_at_tails(std::vector<double>& standard_errors,
                                                          int frequent_event_index, int rare_event_index) {
        double frequent_event_standard_error = standard_errors[static_cast<std::size_t>(frequent_event_index)];
        for (std::size_t i = static_cast<std::size_t>(frequent_event_index); i < standard_errors.size(); ++i) {
            standard_errors[i] = frequent_event_standard_error;
        }

        double rare_event_standard_error = standard_errors[static_cast<std::size_t>(rare_event_index)];
        for (int i = 0; i < rare_event_index; ++i) {
            standard_errors[static_cast<std::size_t>(i)] = rare_event_standard_error;
        }
    }

    // ported from: ComputeSlope(double[] exceedanceProbabilities, double[]
    // stageOrLoggedFlowValues, int i). Computes the slope of the frequency curve at index `i`
    // (1 <= i <= Length-2) using normal-probability interpolation at a small epsilon on either side.
    static double compute_slope(const std::vector<double>& exceedance_probabilities,
                                 const std::vector<double>& stage_or_logged_flow_values, std::size_t i) {
        // step 1: identify the non-exceedance probability and coinciding quantiles for which we're
        // calculating the slope
        double p = 1 - exceedance_probabilities[i];
        double q = stage_or_logged_flow_values[i];

        double p_minus = 1 - exceedance_probabilities[i - 1];
        double q_minus = stage_or_logged_flow_values[i - 1];

        double p_plus = 1 - exceedance_probabilities[i + 1];
        double q_plus = stage_or_logged_flow_values[i + 1];

        // step 2: identify probability margins that feed into the slope calculator
        double epsilon = 0.00001;
        double p_minus_epsilon = p - epsilon;
        double p_plus_epsilon = p + epsilon;

        // step 3: interpolate the quantiles at the probability margins
        double q_minus_epsilon = interpolate_normally(p, p_minus, q, q_minus, p_minus_epsilon);
        double q_plus_epsilon = interpolate_normally(p_plus, p, q_plus, q, p_plus_epsilon);

        // step 4: calculate slope between the probability margins
        double slope = (q_plus_epsilon - q_minus_epsilon) / (p_plus_epsilon - p_minus_epsilon);
        return slope;
    }

    // ported from: InterpolateNormally(double p, double p_minus, double q, double q_minus, double
    // p_target). Interpolates a quantile value at a target probability using a normal-probability
    // (z-score) transformation. `Normal(0.0, 1.0)` stands in for C#'s `new Normal()`, matching the
    // same substitution used throughout this file and in interpolate_quantiles.hpp.
    static double interpolate_normally(double p, double p_minus, double q, double q_minus, double p_target) {
        hecfda::statistics::distributions::Normal standard_normal(0.0, 1.0);

        double z = standard_normal.inverse_cdf(p);
        double z_minus = standard_normal.inverse_cdf(p_minus);
        double z_target = standard_normal.inverse_cdf(p_target);

        double q_target = q_minus + (z_target - z_minus) / (z - z_minus) * (q - q_minus);
        return q_target;
    }

    // ported from: Equation6StandardError(double nonExceedanceProbability, double slope, int erl).
    // Calculates standard error using Equation 6 from the HEC-FDA Technical Reference (CPD-72a):
    // SE^2 = [p(1-p)] / [(1/slope)^2 * ERL].
    static double equation6_standard_error(double non_exceedance_probability, double slope, int erl) {
        double standard_error_squared =
            (non_exceedance_probability * (1 - non_exceedance_probability)) / (std::pow(1 / slope, 2.0) * erl);
        return std::sqrt(standard_error_squared);
    }

    // ported from: ConstructDistributions(double[] means, double[] standardErrors, bool
    // usingStagesNotFlows). Returns `unique_ptr<IDistribution>` rather than the C# return type's
    // concrete `ContinuousDistribution[]` -- Normal/LogNormal both derive from ContinuousDistribution
    // which derives from IDistribution (see continuous_distribution.hpp/i_distribution.hpp), and
    // IDistribution is what UncertainPairedData's ctor (Task P2T3) consumes, so this upcast lets a
    // Task P2T4b caller hand the result straight to UncertainPairedData without an extra conversion.
    static std::vector<std::unique_ptr<IDistribution>> construct_distributions(
        const std::vector<double>& means, const std::vector<double>& standard_errors, bool using_stages_not_flows) {
        std::vector<std::unique_ptr<IDistribution>> distributions;
        distributions.reserve(means.size());

        if (using_stages_not_flows) {
            // Use Normal distributions for stages
            for (std::size_t i = 0; i < means.size(); ++i) {
                distributions.push_back(
                    std::make_unique<hecfda::statistics::distributions::Normal>(means[i], standard_errors[i]));
            }
        } else {
            // Use LogNormal distributions for flows
            for (std::size_t i = 0; i < means.size(); ++i) {
                distributions.push_back(
                    std::make_unique<hecfda::statistics::distributions::LogNormal>(means[i], standard_errors[i]));
            }
        }

        return distributions;
    }
};
}  // namespace utilities
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_UTILITIES_GRAPHICAL_FREQUENCY_UNCERTAINTY_CALCULATORS_HPP
