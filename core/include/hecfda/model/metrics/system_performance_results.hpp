// ported from: HEC.FDA.Model/metrics/SystemPerformanceResults.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_SYSTEM_PERFORMANCE_RESULTS_HPP
#define HECFDA_MODEL_METRICS_SYSTEM_PERFORMANCE_RESULTS_HPP
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/assurance_result_storage.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/histograms/dynamic_histogram.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: SystemPerformanceResults.cs `public class SystemPerformanceResults :
// ValidationErrorLogger`. The system-performance metrics container each Threshold (Phase 5 Task 3)
// owns: an AEP AssuranceResultStorage plus one STAGE AssuranceResultStorage per standard
// non-exceedance probability the caller has registered, fed per-iteration observations by the EAD
// compute loop (Phase 5 Tasks 9/10) via add_aep_for_assurance/add_stage_for_assurance.
//
// Field order mirrors the C# source (`_CalculatePerformanceForLevee`, `_SystemResponseFunction`,
// `_ConvergenceCriteria`), with one deliberate reordering: `_SystemResponseFunction` is stored in
// `std::optional<UncertainPairedData>` (UncertainPairedData has no default ctor and is move-only --
// see uncertain_paired_data.hpp), so calculate_performance_for_levee_ is computed from the
// (UncertainPairedData, ConvergenceCriteria) ctor's PARAMETER (`system_response_function.xvals()`,
// read before the parameter is moved into the member), not from the member itself -- avoiding any
// dependency on cross-member initializer-list ordering. UncertainPairedData being move-only also
// makes this whole class move-only (matches the "fresh construction per use" convention already
// established for UncertainPairedData/StudyAreaConsequencesBinned).
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//  - `: ValidationErrorLogger` base class: no MVVM base/validation-log infrastructure in this port
//    (repo-wide MVVM severance).
//  - The two private `(ConvergenceCriteria, List<AssuranceResultStorage>)` /
//    `(UncertainPairedData, ConvergenceCriteria, List<AssuranceResultStorage>)` ctors: exist
//    solely to feed `ReadFromXML`'s reconstruction, dropped alongside it.
//  - `WriteToXML()`/`static ReadFromXML(XElement)`: XML (de)serialization, no equivalent surface
//    in this port (repo-wide XML severance).
//  - `get_assurance`'s MVVM `ErrorMessage`/`ReportMessage(this, new MessageEventArgs(errorMessage))`
//    call on a lookup miss: C# still returns a dummy `AssuranceResultStorage(STAGE_ASSURANCE_TYPE,
//    .98)` fallback (hard-coded regardless of the query's type/probability, silently masking the
//    real miss) after logging. This port throws `std::runtime_error` on a miss instead, matching
//    the repo-wide convention for severed MVVM-`ReportMessage` misses (see
//    `StudyAreaConsequencesBinned::require_consequence_result`). Every assurance type/probability
//    this task's compute path queries was registered by a prior ctor/`add_stage_assurance_histogram`
//    call, so the throw is unreachable in practice, matching the C# "should never happen" miss path.
//
// FAITHFUL UPSTREAM BUG, reproduced verbatim (see add_stage_assurance_histogram()):
// `AddStageAssuranceHistogram`'s C# `if (!Assurances.Contains(assurance))` guard is dead code.
// `AssuranceResultStorage` implements neither `IEquatable<AssuranceResultStorage>` nor overrides
// `object.Equals(object)` (its `bool Equals(AssuranceResultStorage)` is a same-named-but-different
// method, not an override), so `List<T>.Contains` falls back to reference equality. A freshly
// constructed `assurance` local can never be reference-equal to an existing list element, so
// `Contains` always returns `false` and the guard never fires -- every call unconditionally
// appends, even for a `standardNonExceedanceProbability` that already has an entry (subsequent
// `get_assurance` lookups then return whichever matching entry is found FIRST in list order, per
// C#'s `foreach` early-return). Transcribed as an unconditional `push_back`.
//
// Other quirks transcribed verbatim:
//  - `assurance_of_event`'s `threshold_value` parameter is READ but never USED when
//    `calculate_performance_for_levee_` is true: the levee branch calls
//    `calculate_assurance_for_levee(standard_non_exceedance_probability)` only, ignoring
//    `threshold_value` entirely.
//  - `assurance_is_converged`/`assurance_test_for_convergence`/`assurance_remaining_iterations`
//    hard-code `standardNonExceedanceProbability = 0.98` internally rather than taking it as a
//    parameter, so they only ever read the STAGE assurance registered at exactly 0.98.
class SystemPerformanceResults {
   public:
    using ConvergenceCriteria = hecfda::statistics::ConvergenceCriteria;
    using DynamicHistogram = hecfda::statistics::histograms::DynamicHistogram;
    using PairedData = hecfda::model::paired_data::PairedData;
    using UncertainPairedData = hecfda::model::paired_data::UncertainPairedData;

    static constexpr const char* kAepAssuranceType = "AEP";
    static constexpr const char* kStageAssuranceType = "STAGE";
    static constexpr double kAepBinWidth = 0.0002;
    static constexpr double kStageBinWidth = 0.001;

    // Move-only: system_response_function_ is std::optional<UncertainPairedData>, and
    // UncertainPairedData is itself move-only (see class comment).
    SystemPerformanceResults(SystemPerformanceResults&&) = default;
    SystemPerformanceResults& operator=(SystemPerformanceResults&&) = default;
    SystemPerformanceResults(const SystemPerformanceResults&) = delete;
    SystemPerformanceResults& operator=(const SystemPerformanceResults&) = delete;

    // ported from: SystemPerformanceResults.cs `public SystemPerformanceResults()` -- the dummy
    // ctor: one AEP placeholder plus one STAGE placeholder per ER-101 standard non-exceedance
    // probability, each built via AssuranceResultStorage's "dummy" (type, probability) ctor (see
    // assurance_result_storage.hpp).
    SystemPerformanceResults() : convergence_criteria_(ConvergenceCriteria()) {
        assurances_.emplace_back(kAepAssuranceType, 0.0);
        static const double kStandardNonExceedanceProbabilities[] = {.9, .96, .98, .99, .996, .998};
        for (double probability : kStandardNonExceedanceProbabilities) {
            assurances_.emplace_back(kStageAssuranceType, probability);
        }
    }

    // ported from: SystemPerformanceResults.cs `public SystemPerformanceResults(ConvergenceCriteria
    // convergenceCriteria)`.
    explicit SystemPerformanceResults(ConvergenceCriteria convergence_criteria)
        : convergence_criteria_(convergence_criteria) {
        assurances_.emplace_back(kAepAssuranceType, kAepBinWidth, convergence_criteria);
    }

    // ported from: SystemPerformanceResults.cs `public SystemPerformanceResults(UncertainPairedData
    // systemResponseFunction, ConvergenceCriteria convergenceCriteria)`.
    // `calculate_performance_for_levee_` is read from the PARAMETER (before it is moved into
    // `system_response_function_`) -- see the class comment for why.
    SystemPerformanceResults(UncertainPairedData system_response_function,
                              ConvergenceCriteria convergence_criteria)
        : calculate_performance_for_levee_(system_response_function.xvals().size() > 2),
          system_response_function_(std::move(system_response_function)),
          convergence_criteria_(convergence_criteria) {
        assurances_.emplace_back(kAepAssuranceType, kAepBinWidth, convergence_criteria);
    }

    const std::vector<AssuranceResultStorage>& assurances() const { return assurances_; }

    // ported from: SystemPerformanceResults.cs `public void AddStageAssuranceHistogram(double
    // standardNonExceedanceProbability, double binWidth = STAGE_BIN_WIDTH)`. See the class
    // comment's FAITHFUL UPSTREAM BUG note: the C# duplicate-avoidance guard is dead code, so this
    // unconditionally appends.
    void add_stage_assurance_histogram(double standard_non_exceedance_probability,
                                        double bin_width = kStageBinWidth) {
        assurances_.emplace_back(kStageAssuranceType, bin_width, convergence_criteria_,
                                  standard_non_exceedance_probability);
    }

    // ported from: SystemPerformanceResults.cs `public DynamicHistogram
    // GetAssuranceOfThresholdHistogram(double standardNonExceedanceProbability)`.
    const DynamicHistogram& get_assurance_of_threshold_histogram(
        double standard_non_exceedance_probability) const {
        return get_assurance(kStageAssuranceType, standard_non_exceedance_probability).assurance_histogram();
    }

    // ported from: SystemPerformanceResults.cs `public void AddAEPForAssurance(double aep, int
    // iteration)`.
    void add_aep_for_assurance(double aep, int iteration) {
        get_assurance(kAepAssuranceType).add_observation(aep, iteration);
    }

    // ported from: SystemPerformanceResults.cs `public void AddStageForAssurance(double
    // standardNonExceedanceProbability, double stage, int iteration)`.
    void add_stage_for_assurance(double standard_non_exceedance_probability, double stage, int iteration) {
        get_assurance(kStageAssuranceType, standard_non_exceedance_probability).add_observation(stage, iteration);
    }

    // ported from: SystemPerformanceResults.cs `public double MeanAEP()`.
    double mean_aep() const { return get_assurance(kAepAssuranceType).assurance_histogram().sample_mean(); }

    // ported from: SystemPerformanceResults.cs `public double MedianAEP()`.
    double median_aep() const {
        return get_assurance(kAepAssuranceType).assurance_histogram().inverse_cdf(0.5);
    }

    // ported from: SystemPerformanceResults.cs `internal double AEPWithGivenAssurance(double
    // assurance)`. The `> 1` clamp corrects a histogram-binning imperfection for AEP sets clustered
    // near 0.9999 (comment transcribed from the C# source).
    double aep_with_given_assurance(double assurance) const {
        double aep_with_given_assurance =
            get_assurance(kAepAssuranceType).assurance_histogram().inverse_cdf(assurance);
        if (aep_with_given_assurance > 1) {
            aep_with_given_assurance = 1;
        }
        return aep_with_given_assurance;
    }

    // ported from: SystemPerformanceResults.cs `internal double AssuranceOfAEP(double
    // exceedanceProbability)`.
    double assurance_of_aep(double exceedance_probability) const {
        return get_assurance(kAepAssuranceType).assurance_histogram().cdf(exceedance_probability);
    }

    // ported from: SystemPerformanceResults.cs `internal bool AssuranceIsConverged()`. Hard-codes
    // standardNonExceedanceProbability = 0.98 (see class comment).
    bool assurance_is_converged() const {
        const double standard_non_exceedance_probability = 0.98;
        return get_assurance(kStageAssuranceType, standard_non_exceedance_probability)
            .assurance_histogram()
            .is_converged();
    }

    // ported from: SystemPerformanceResults.cs `public bool AssuranceTestForConvergence(double
    // upperConfidenceLimitProb, double lowerConfidenceLimitProb)`.
    bool assurance_test_for_convergence(double upper_confidence_limit_prob,
                                         double lower_confidence_limit_prob) {
        const double standard_non_exceedance_probability = 0.98;
        return get_assurance(kStageAssuranceType, standard_non_exceedance_probability)
            .assurance_histogram()
            .is_histogram_converged(upper_confidence_limit_prob, lower_confidence_limit_prob);
    }

    // ported from: SystemPerformanceResults.cs `public long AssuranceRemainingIterations(double
    // upperConfidenceLimitProb, double lowerConfidenceLimitProb)`.
    std::int64_t assurance_remaining_iterations(double upper_confidence_limit_prob,
                                                 double lower_confidence_limit_prob) {
        const double standard_non_exceedance_probability = 0.98;
        return get_assurance(kStageAssuranceType, standard_non_exceedance_probability)
            .assurance_histogram()
            .estimate_iterations_remaining(upper_confidence_limit_prob, lower_confidence_limit_prob);
    }

    // ported from: SystemPerformanceResults.cs `public double AssuranceOfEvent(double
    // standardNonExceedanceProbability, double thresholdValue)`. `threshold_value` is unused when
    // `calculate_performance_for_levee_` is true (see class comment).
    double assurance_of_event(double standard_non_exceedance_probability, double threshold_value) {
        if (calculate_performance_for_levee_) {
            return calculate_assurance_for_levee(standard_non_exceedance_probability);
        }
        get_assurance(kStageAssuranceType, standard_non_exceedance_probability).assurance_histogram().force_de_queue();
        DynamicHistogram& assurance_histogram =
            get_assurance(kStageAssuranceType, standard_non_exceedance_probability).assurance_histogram();
        double assurance = assurance_histogram.cdf(threshold_value);
        return assurance;
    }

    // ported from: SystemPerformanceResults.cs `public double LongTermExceedanceProbability(int
    // years)`.
    double long_term_exceedance_probability(int years) const {
        return 1 - std::pow(1 - mean_aep(), years);
    }

    // ported from: SystemPerformanceResults.cs `public void ParallelResultsAreConverged(double
    // upperQuantile, double lowerQuantile)`. C#'s `Parallel.For` is ported as a sequential loop
    // (deterministic result either way -- each iteration mutates only its own AssuranceResultStorage's
    // convergence cache, no shared mutable state).
    void parallel_results_are_converged(double upper_quantile, double lower_quantile) {
        for (AssuranceResultStorage& assurance : assurances_) {
            assurance.assurance_histogram().is_histogram_converged(upper_quantile, lower_quantile);
        }
    }

    // ported from: SystemPerformanceResults.cs `public bool Equals(SystemPerformanceResults
    // projectPerformanceResults)`.
    bool equals(const SystemPerformanceResults& project_performance_results) const {
        for (const AssuranceResultStorage& assurance_result_storage : assurances_) {
            bool are_equal = assurance_result_storage.equals(project_performance_results.get_assurance(
                assurance_result_storage.assurance_type(),
                assurance_result_storage.standard_non_exceedance_probability()));
            if (!are_equal) {
                return false;
            }
        }
        return true;
    }

    // ported from: SystemPerformanceResults.cs `public void PutDataIntoHistograms()`.
    void put_data_into_histograms() {
        for (AssuranceResultStorage& assurance_result_storage : assurances_) {
            assurance_result_storage.put_data_into_histogram();
        }
    }

    // ported from: SystemPerformanceResults.cs `internal DynamicHistogram GetAEPHistogram()`. Was
    // private until Phase 5 Task 6: ImpactAreaScenarioResults::get_aep_histogram_for_plotting is
    // this method's first caller (the class comment above anticipated this -- "wire to public if/
    // when a later task's fixture needs it directly").
    DynamicHistogram& get_aep_histogram() { return get_assurance(kAepAssuranceType).assurance_histogram(); }

   private:
    // ported from: SystemPerformanceResults.cs `internal AssuranceResultStorage GetAssurance(string
    // type, double standardNonExceedanceProbabilityForAssuranceOfTargetOrLevee = 0)`. See the class
    // comment's SEVERANCES note: throws on a miss instead of C#'s log-and-return-dummy-fallback.
    AssuranceResultStorage& get_assurance(
        const std::string& type, double standard_non_exceedance_probability_for_assurance_of_target_or_levee = 0) {
        for (AssuranceResultStorage& assurance : assurances_) {
            if (assurance.assurance_type() == type &&
                assurance.standard_non_exceedance_probability() ==
                    standard_non_exceedance_probability_for_assurance_of_target_or_levee) {
                return assurance;
            }
        }
        throw std::runtime_error(
            "SystemPerformanceResults::get_assurance: no assurance found for type='" + type +
            "', standard_non_exceedance_probability=" +
            std::to_string(standard_non_exceedance_probability_for_assurance_of_target_or_levee) +
            " (mirrors C# ReportMessage(Fatal)+dummy-fallback, severed here per repo convention -- "
            "see StudyAreaConsequencesBinned::require_consequence_result)");
    }
    const AssuranceResultStorage& get_assurance(
        const std::string& type,
        double standard_non_exceedance_probability_for_assurance_of_target_or_levee = 0) const {
        for (const AssuranceResultStorage& assurance : assurances_) {
            if (assurance.assurance_type() == type &&
                assurance.standard_non_exceedance_probability() ==
                    standard_non_exceedance_probability_for_assurance_of_target_or_levee) {
                return assurance;
            }
        }
        throw std::runtime_error(
            "SystemPerformanceResults::get_assurance: no assurance found for type='" + type +
            "', standard_non_exceedance_probability=" +
            std::to_string(standard_non_exceedance_probability_for_assurance_of_target_or_levee) +
            " (mirrors C# ReportMessage(Fatal)+dummy-fallback, severed here per repo convention -- "
            "see StudyAreaConsequencesBinned::require_consequence_result)");
    }

    // ported from: SystemPerformanceResults.cs `private double CalculateAssuranceForLevee(double
    // standardNonExceedanceProbability)`. FP-sensitive fragility-curve integration: sweeps stages
    // from the system-response function's first X value upward in AssuranceHistogram::BinWidth
    // steps, accumulating exceedance-probability-weighted geotechnical failure until the median
    // levee curve reports certain failure (f(x) >= 1). Transcribed statement-for-statement,
    // including the trailing "undo last increment, redo with the pre-loop cumulative" correction.
    double calculate_assurance_for_levee(double standard_non_exceedance_probability) {
        DynamicHistogram& assurance_histogram =
            get_assurance(kStageAssuranceType, standard_non_exceedance_probability).assurance_histogram();
        PairedData median_levee_curve = system_response_function_->sample_paired_data(0.5);

        // if the user-defined system response function does not have certain failure defined,
        // then define it.
        if (median_levee_curve.yvals().back() < 1) {
            const double epsilon = 0.001;
            double largest_x = median_levee_curve.xvals().back();
            std::vector<double> x_vals = median_levee_curve.xvals();
            std::vector<double> y_vals = median_levee_curve.yvals();
            x_vals.push_back(largest_x + epsilon);
            y_vals.push_back(1.0);
            median_levee_curve = PairedData(std::move(x_vals), std::move(y_vals));
        }

        assurance_histogram.force_de_queue();
        double stage_step = assurance_histogram.bin_width();
        const std::vector<double>& stages = system_response_function_->xvals();
        double first_stage = stages[0];
        double current_stage = 0.0;
        double next_stage = 0.0;
        double current_cumulative_exceedance_probability = 0;
        double geotechnical_failure_at_average_stage = 0;
        double incremental_probability_with_failure = 0;
        double exceedance_probability_with_failure = 0;
        int i = 0;
        // calculate from the bottom of the fragility curve up, until certain failure.
        while (geotechnical_failure_at_average_stage < 1) {
            current_stage = first_stage + i * stage_step;
            next_stage = current_stage + stage_step;
            current_cumulative_exceedance_probability = 1 - assurance_histogram.cdf(current_stage);
            double next_cumulative_exceedance_probability = 1 - assurance_histogram.cdf(next_stage);
            double incremental_probability =
                current_cumulative_exceedance_probability - next_cumulative_exceedance_probability;
            double average_stage = (current_stage + next_stage) / 2;
            geotechnical_failure_at_average_stage = median_levee_curve.f(average_stage);
            incremental_probability_with_failure = incremental_probability * geotechnical_failure_at_average_stage;
            exceedance_probability_with_failure += incremental_probability_with_failure;
            i++;
        }
        // correct cumulative probability with failure by removing incorrect incremental probability
        // with failure.
        exceedance_probability_with_failure -= incremental_probability_with_failure;
        // the incremental probability with failure for the stage at which prob(failure) = 1 is the
        // current cumulative exceedance probability.
        exceedance_probability_with_failure += current_cumulative_exceedance_probability;
        double conditional_non_exceedance_probability = 1 - exceedance_probability_with_failure;
        return conditional_non_exceedance_probability;
    }

    bool calculate_performance_for_levee_ = false;
    std::optional<UncertainPairedData> system_response_function_;
    ConvergenceCriteria convergence_criteria_;
    std::vector<AssuranceResultStorage> assurances_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_SYSTEM_PERFORMANCE_RESULTS_HPP
