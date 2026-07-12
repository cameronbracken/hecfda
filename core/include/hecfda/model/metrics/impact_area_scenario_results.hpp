// ported from: HEC.FDA.Model/metrics/ImpactAreaScenarioResults.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_IMPACT_AREA_SCENARIO_RESULTS_HPP
#define HECFDA_MODEL_METRICS_IMPACT_AREA_SCENARIO_RESULTS_HPP
#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/aggregated_consequences_binned.hpp"
#include "hecfda/model/metrics/categoried_paired_data.hpp"
#include "hecfda/model/metrics/categoried_uncertain_paired_data.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/metrics/performance_by_thresholds.hpp"
#include "hecfda/model/metrics/study_area_consequences_binned.hpp"
#include "hecfda/model/metrics/threshold.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/histograms/dynamic_histogram.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: ImpactAreaScenarioResults.cs `public class ImpactAreaScenarioResults`. The
// per-impact-area container the EAD compute (`ImpactAreaScenarioSimulation::compute`, Phase 5
// Tasks 7-11) RETURNS: one `PerformanceByThresholds` (Task 3), one `StudyAreaConsequencesBinned`
// (Phase 4 Task 4), and the two consequence-frequency-curve collections
// (`CategoriedPairedData`/`CategoriedUncertainPairedData`, Task 4), plus every result-query
// delegator the rest of the codebase reads this container through. This is the top of the
// metrics closure Phase 5 Tasks 1-5 built: everything else in `hecfda::model::metrics` feeds into
// one of these.
//
// Move-only: `performance_by_thresholds_` (PerformanceByThresholds) and `consequence_results_`
// (StudyAreaConsequencesBinned) are both themselves move-only (see their own headers), so this
// class is move-only too, matching the "fresh construction per use" convention already
// established for every other Phase 5 metrics container.
//
// Public-ctor `StudyAreaConsequencesBinned` construction, a deliberate port deviation: C#'s two
// public ctors build `ConsequenceResults` via `StudyAreaConsequencesBinned(impactAreaID)` / `(bool
// isNull)` -- the two "null"/dummy ctors that `study_area_consequences_binned.hpp`'s own
// SEVERANCES note declined to port (they need the also-severed placeholder-`DynamicHistogram`
// ctor). Since `StudyAreaConsequencesBinned` has no default ctor and is move-only, this port
// constructs `consequence_results_` with the "public for testing"
// `(std::vector<AggregatedConsequencesBinned>)` ctor passed an EMPTY vector instead of the
// single-dummy-element C# placeholder. This matches how the real compute path actually uses these
// public ctors in practice: nothing reads the dummy placeholder element (`ConsequenceType.
// UNASSIGNED`, matched by no real query), and the EAD compute loop (Tasks 7-11) populates
// `consequence_results_` via `add_new_consequence_result_object` (already ported, Phase 4 Task
// 4) -- unaffected by whether the list started with zero or one (unread) elements.
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//  - The private `ImpactAreaScenarioResults(PerformanceByThresholds, StudyAreaConsequencesBinned,
//    int)` ctor: exists solely to feed `ReadFromXML`'s reconstruction, dropped alongside it
//    (repo-wide XML severance).
//  - `WriteToXml()`/`static ReadFromXML(XElement)`: XML (de)serialization, no equivalent surface
//    in this port (repo-wide XML severance).
//  - The `_uncertainCurveLock` field and its `lock (_uncertainCurveLock)` block in
//    `GetOrCreateUncertainConsequenceFrequencyCurve`: C# uses this for thread-safe use inside
//    `Parallel.For` compute loops. This port's Monte Carlo loop (Tasks 7-11) is serial (matches
//    the repo-wide "no threading primitives" convention -- .NET's seeded `Random` port,
//    `DotNetRandom`, is likewise consumed serially throughout this port), so the lock and its
//    backing field are dropped entirely, not merely no-op'd.
//
// Reference stability: `uncertain_consequence_frequency_curves_` is a `std::deque`, not a
// `std::vector`, specifically so `get_or_create_uncertain_consequence_frequency_curve` can safely
// return a REFERENCE into it (see that method's own comment). `std::deque::emplace_back`/
// `push_back` never invalidates references to existing elements (only iterators), matching C#'s
// `List<CategoriedUncertainPairedData>` -- a list of references to heap objects, where `.Add(...)`
// never moves/invalidates a previously-handed-out element reference either. A `std::vector` of
// values would NOT have this guarantee (`.emplace_back` can reallocate and invalidate every
// existing reference on growth), which is why `std::deque` was chosen here. The sibling
// `consequence_frequency_functions_` (`std::vector<CategoriedPairedData>`) does not need the same
// treatment: nothing in this port -- or in the upstream `ImpactAreaScenarioSimulation.cs` call
// sites (`ConsequenceFrequencyFunctions.Add(...)` at line 532, `.Select(...)` over the whole list
// at line 155) -- ever holds a reference into it across a call that could grow it; it is only
// appended to and read wholesale, so plain `std::vector` value semantics are safe as-is.
class ImpactAreaScenarioResults {
   public:
    using ConvergenceCriteria = hecfda::statistics::ConvergenceCriteria;
    using DynamicHistogram = hecfda::statistics::histograms::DynamicHistogram;

    // Move-only: performance_by_thresholds_/consequence_results_ are themselves move-only (see
    // class comment).
    ImpactAreaScenarioResults(ImpactAreaScenarioResults&&) = default;
    ImpactAreaScenarioResults& operator=(ImpactAreaScenarioResults&&) = default;
    ImpactAreaScenarioResults(const ImpactAreaScenarioResults&) = delete;
    ImpactAreaScenarioResults& operator=(const ImpactAreaScenarioResults&) = delete;

    // ported from: ImpactAreaScenarioResults.cs `public ImpactAreaScenarioResults(int
    // impactAreaID, bool isNull)`. See the class comment for why `consequence_results_` is built
    // from an empty vector rather than C#'s single-dummy-element `StudyAreaConsequencesBinned(
    // impactAreaID)` ctor.
    ImpactAreaScenarioResults(int impact_area_id, bool is_null)
        : performance_by_thresholds_(true),
          consequence_results_(std::vector<AggregatedConsequencesBinned>{}),
          impact_area_id_(impact_area_id),
          is_null_(is_null) {}

    // ported from: ImpactAreaScenarioResults.cs `public ImpactAreaScenarioResults(int
    // impactAreaID)`. See the class comment for why `consequence_results_` is built from an empty
    // vector rather than C#'s `StudyAreaConsequencesBinned(false)` ctor.
    explicit ImpactAreaScenarioResults(int impact_area_id)
        : performance_by_thresholds_(),
          consequence_results_(std::vector<AggregatedConsequencesBinned>{}),
          impact_area_id_(impact_area_id),
          is_null_(false) {}

    // ported from: ImpactAreaScenarioResults.cs `public PerformanceByThresholds
    // PerformanceByThresholds { get; set; }` -- "exposed publicly for testing". Mutable reference
    // accessor stands in for the C# public setter: a caller wanting to "set" it move-assigns
    // through the returned reference (`results.performance_by_thresholds() = std::move(other)`),
    // matching the mutable-reference-accessor convention `Threshold::system_performance_results()`
    // already established for a move-only member.
    PerformanceByThresholds& performance_by_thresholds() { return performance_by_thresholds_; }
    const PerformanceByThresholds& performance_by_thresholds() const { return performance_by_thresholds_; }

    // ported from: ImpactAreaScenarioResults.cs `public StudyAreaConsequencesBinned
    // ConsequenceResults { get; }` -- read-only in C# (no setter), but the compute loop mutates
    // the object THROUGH it (add_new_consequence_result_object/add_consequence_realization/
    // put_data_into_histograms are all non-const), so the accessor returns a mutable reference.
    StudyAreaConsequencesBinned& consequence_results() { return consequence_results_; }
    const StudyAreaConsequencesBinned& consequence_results() const { return consequence_results_; }

    int impact_area_id() const { return impact_area_id_; }
    bool is_null() const { return is_null_; }

    // ported from: ImpactAreaScenarioResults.cs `public List<CategoriedPairedData>
    // ConsequenceFrequencyFunctions { get; set; } = [];`.
    std::vector<CategoriedPairedData>& consequence_frequency_functions() {
        return consequence_frequency_functions_;
    }
    const std::vector<CategoriedPairedData>& consequence_frequency_functions() const {
        return consequence_frequency_functions_;
    }

    // ported from: ImpactAreaScenarioResults.cs `public List<CategoriedUncertainPairedData>
    // UncertainConsequenceFrequencyCurves { get; set; } = [];`. `std::deque`, not `std::vector` --
    // see the class comment's "Reference stability" note.
    std::deque<CategoriedUncertainPairedData>& uncertain_consequence_frequency_curves() {
        return uncertain_consequence_frequency_curves_;
    }
    const std::deque<CategoriedUncertainPairedData>& uncertain_consequence_frequency_curves() const {
        return uncertain_consequence_frequency_curves_;
    }

    // ported from: ImpactAreaScenarioResults.cs `public double MeanAEP(int thresholdID)`.
    double mean_aep(int threshold_id) const {
        return performance_by_thresholds_.get_threshold(threshold_id).system_performance_results().mean_aep();
    }

    // ported from: ImpactAreaScenarioResults.cs `public double MedianAEP(int thresholdID)`.
    double median_aep(int threshold_id) const {
        return performance_by_thresholds_.get_threshold(threshold_id).system_performance_results().median_aep();
    }

    // ported from: ImpactAreaScenarioResults.cs `public double AEPWithGivenAssurance(int
    // thresholdID, double assurance)`.
    double aep_with_given_assurance(int threshold_id, double assurance) const {
        return performance_by_thresholds_.get_threshold(threshold_id)
            .system_performance_results()
            .aep_with_given_assurance(assurance);
    }

    // ported from: ImpactAreaScenarioResults.cs `public double AssuranceOfAEP(int thresholdID,
    // double exceedanceProbability)`.
    double assurance_of_aep(int threshold_id, double exceedance_probability) const {
        return performance_by_thresholds_.get_threshold(threshold_id)
            .system_performance_results()
            .assurance_of_aep(exceedance_probability);
    }

    // ported from: ImpactAreaScenarioResults.cs `public DynamicHistogram
    // GetAEPHistogramForPlotting(int thresholdID)`. NON-const: SystemPerformanceResults::
    // get_aep_histogram() (made public by this task, see system_performance_results.hpp) is
    // itself non-const.
    DynamicHistogram& get_aep_histogram_for_plotting(int threshold_id) {
        return performance_by_thresholds_.get_threshold(threshold_id).system_performance_results().get_aep_histogram();
    }

    // ported from: ImpactAreaScenarioResults.cs `public double LongTermExceedanceProbability(int
    // thresholdID, int years)`.
    double long_term_exceedance_probability(int threshold_id, int years) const {
        return performance_by_thresholds_.get_threshold(threshold_id)
            .system_performance_results()
            .long_term_exceedance_probability(years);
    }

    // ported from: ImpactAreaScenarioResults.cs `public double AssuranceOfEvent(int thresholdID,
    // double standardNonExceedanceProbability)`. NON-const: SystemPerformanceResults::
    // assurance_of_event is itself non-const (calls force_de_queue).
    double assurance_of_event(int threshold_id, double standard_non_exceedance_probability) {
        Threshold& thresh = performance_by_thresholds_.get_threshold(threshold_id);
        return thresh.system_performance_results().assurance_of_event(standard_non_exceedance_probability,
                                                                        thresh.threshold_value());
    }

    // ported from: ImpactAreaScenarioResults.cs `public double MeanExpectedAnnualConsequences(int
    // impactAreaID = ..DEFAULT_MISSING_VALUE, string damageCategory = null, string assetCategory =
    // null, ConsequenceType consequenceType = ConsequenceType.Damage, RiskType riskType =
    // RiskType.Total)`. Default args transcribed verbatim (note: RiskType defaults to Total HERE,
    // NOT Fail like StudyAreaConsequencesBinned::sample_mean_damage's own default -- this level's
    // default is a SEPARATE C# default declaration, always forwarded explicitly to
    // SampleMeanDamage, so SampleMeanDamage's own default is shadowed/dead on this call path;
    // same "each level keeps its own real default" pattern already documented for
    // GetConsequenceResult vs. FilterByCategories). NON-const: StudyAreaConsequencesBinned::
    // sample_mean_damage is itself non-const (see that method's own comment).
    double mean_expected_annual_consequences(int impact_area_id = kDefaultMissingValue,
                                              const std::optional<std::string>& damage_category = std::nullopt,
                                              const std::optional<std::string>& asset_category = std::nullopt,
                                              ConsequenceType consequence_type = ConsequenceType::Damage,
                                              RiskType risk_type = RiskType::Total) {
        return consequence_results_.sample_mean_damage(damage_category, asset_category, impact_area_id,
                                                         consequence_type, risk_type);
    }

    // ported from: ImpactAreaScenarioResults.cs `public IHistogram GetSpecificHistogram(int
    // impactAreaID, string damageCategory, string assetCategory)`. Note the C# WRAPPER's argument
    // order (impactAreaID, damageCategory, assetCategory) differs from the order it forwards to
    // `ConsequenceResults.GetSpecificHistogram(damageCategory, assetCategory, impactAreaID)` --
    // transcribed verbatim, including the reordering.
    const DynamicHistogram* get_specific_histogram(int impact_area_id, const std::string& damage_category,
                                                    const std::string& asset_category) const {
        return consequence_results_.get_specific_histogram(damage_category, asset_category, impact_area_id);
    }

    // ported from: ImpactAreaScenarioResults.cs `public bool ResultsAreConverged(double
    // upperConfidenceLimitProb, double lowerConfidenceLimitProb, bool checkConsequenceResults)`.
    // NON-const: both private helpers below mutate histogram convergence caches.
    bool results_are_converged(double upper_confidence_limit_prob, double lower_confidence_limit_prob,
                                bool check_consequence_results) {
        bool consequence_converged = true;
        if (check_consequence_results) {
            consequence_converged =
                consequence_results_are_converged(upper_confidence_limit_prob, lower_confidence_limit_prob);
        }
        bool performance_converged =
            performance_results_are_converged(upper_confidence_limit_prob, lower_confidence_limit_prob);
        return consequence_converged && performance_converged;
    }

    // ported from: ImpactAreaScenarioResults.cs `public long RemainingIterations(double
    // upperConfidenceLimitProb, double lowerConfidenceLimitProb, bool computeWithDamage)`.
    // Transcribed statement-for-statement, including the C# `List<long>` accumulation (each
    // list always has at least one 0 element pushed up front, so `.Max()` -- here
    // `std::max_element` -- never sees an empty range).
    std::int64_t remaining_iterations(double upper_confidence_limit_prob, double lower_confidence_limit_prob,
                                       bool compute_with_damage) {
        std::vector<std::int64_t> ead_iterations_remaining;
        ead_iterations_remaining.push_back(0);
        if (compute_with_damage) {
            for (AggregatedConsequencesBinned& result : consequence_results_.consequence_result_list()) {
                if (result.consequence_histogram()->histogram_is_zero_valued()) {
                    ead_iterations_remaining.push_back(0);
                } else {
                    std::int64_t its_remaining = result.consequence_histogram()->estimate_iterations_remaining(
                        upper_confidence_limit_prob, lower_confidence_limit_prob);
                    ead_iterations_remaining.push_back(its_remaining);
                }
            }
        } else {
            ead_iterations_remaining.push_back(0);
        }

        std::vector<std::int64_t> performance_iterations_remaining;
        for (Threshold& threshold : performance_by_thresholds_.list_of_thresholds()) {
            std::int64_t its_remaining = threshold.system_performance_results().assurance_remaining_iterations(
                upper_confidence_limit_prob, lower_confidence_limit_prob);
            performance_iterations_remaining.push_back(its_remaining);
        }
        std::int64_t max_ead =
            *std::max_element(ead_iterations_remaining.begin(), ead_iterations_remaining.end());
        std::int64_t max_performance = *std::max_element(performance_iterations_remaining.begin(),
                                                           performance_iterations_remaining.end());
        return std::max(max_ead, max_performance);
    }

    // ported from: ImpactAreaScenarioResults.cs `public void ParallelResultsAreConverged(double
    // upperConfidenceLimitProb, double lowerConfidenceLimitProb)`. C# name kept verbatim despite
    // returning void (a side-effecting convergence-cache warm-up, not a predicate) -- matches
    // SystemPerformanceResults::parallel_results_are_converged's own "sequential loop over a
    // Parallel.For" port, since each iteration below only mutates its own Threshold's convergence
    // cache (no shared mutable state).
    void parallel_results_are_converged(double upper_confidence_limit_prob, double lower_confidence_limit_prob) {
        for (Threshold& threshold : performance_by_thresholds_.list_of_thresholds()) {
            threshold.system_performance_results().parallel_results_are_converged(upper_confidence_limit_prob,
                                                                                    lower_confidence_limit_prob);
        }
    }

    // ported from: ImpactAreaScenarioResults.cs `public bool Equals(ImpactAreaScenarioResults
    // incomingIContainResults)`.
    bool equals(const ImpactAreaScenarioResults& incoming_i_contain_results) const {
        bool performance_matches =
            performance_by_thresholds_.equals(incoming_i_contain_results.performance_by_thresholds_);
        bool damage_results_match = consequence_results_.equals(incoming_i_contain_results.consequence_results_);
        if (!performance_matches || !damage_results_match) {
            return false;
        }
        return true;
    }

    // ported from: ImpactAreaScenarioResults.cs `public CategoriedUncertainPairedData
    // GetOrCreateUncertainConsequenceFrequencyCurve(double[] xvals, string damageCategory, string
    // assetCategory, ConsequenceType consequenceType, RiskType riskType, ConvergenceCriteria
    // convergenceCriteria)`. Drops the C# `lock (_uncertainCurveLock)` -- see the class comment's
    // SEVERANCES note. Returns a REFERENCE into `uncertain_consequence_frequency_curves_`, the
    // value-semantics equivalent of C#'s reference-type `List<T>` element (matching the same
    // "hand back the live object" contract `Threshold::system_performance_results()`/
    // `PerformanceByThresholds::get_threshold()` already establish for this port's other
    // find-or-append accessors). Reference stability across a later `emplace_back` that appends a
    // NEW (damageCategory, assetCategory, consequenceType, riskType) combination is guaranteed by
    // `uncertain_consequence_frequency_curves_` being a `std::deque` (see the class comment's
    // "Reference stability" note) -- this matches C#'s `List<T>`-of-references, where `.Add(...)`
    // never invalidates a previously returned element reference either.
    CategoriedUncertainPairedData& get_or_create_uncertain_consequence_frequency_curve(
        const std::vector<double>& xvals, const std::string& damage_category, const std::string& asset_category,
        ConsequenceType consequence_type, RiskType risk_type, ConvergenceCriteria convergence_criteria) {
        for (CategoriedUncertainPairedData& curve : uncertain_consequence_frequency_curves_) {
            if (curve.damage_category() == damage_category && curve.asset_category() == asset_category &&
                curve.consequence_type() == consequence_type && curve.risk_type() == risk_type) {
                return curve;
            }
        }
        uncertain_consequence_frequency_curves_.emplace_back(xvals, damage_category, asset_category,
                                                               consequence_type, risk_type, convergence_criteria);
        return uncertain_consequence_frequency_curves_.back();
    }

    // ported from: ImpactAreaScenarioResults.cs `public void
    // PutUncertainFrequencyCurvesIntoHistograms()`.
    void put_uncertain_frequency_curves_into_histograms() {
        for (CategoriedUncertainPairedData& curve : uncertain_consequence_frequency_curves_) {
            curve.put_data_into_histograms();
        }
    }

   private:
    // ported from: ImpactAreaScenarioResults.cs's implicit `utilities.IntegerGlobalConstants.
    // DEFAULT_MISSING_VALUE` default arg on MeanExpectedAnnualConsequences (-999), defined locally
    // per the repo's established convention (see structure.hpp's kDefaultMissingValue /
    // study_area_consequences_binned.hpp's own copy) since the whole (mostly-severed) utilities
    // file isn't otherwise ported.
    static constexpr int kDefaultMissingValue = -999;

    // ported from: ImpactAreaScenarioResults.cs `private bool PerformanceResultsAreConverged(double
    // upperConfidenceLimitProb, double lowerConfidenceLimitProb)`. AND across every threshold's
    // assurance convergence test, short-circuiting (return false) on the first non-converged
    // threshold.
    bool performance_results_are_converged(double upper_confidence_limit_prob, double lower_confidence_limit_prob) {
        for (Threshold& threshold : performance_by_thresholds_.list_of_thresholds()) {
            bool threshold_assurance_is_converged = threshold.system_performance_results().assurance_test_for_convergence(
                upper_confidence_limit_prob, lower_confidence_limit_prob);
            if (!threshold_assurance_is_converged) {
                return false;
            }
        }
        return true;
    }

    // ported from: ImpactAreaScenarioResults.cs `private bool ConsequenceResultsAreConverged(double
    // upperConfidenceLimitProb, double lowerConfidenceLimitProb)`. A DIFFERENT method from
    // StudyAreaConsequencesBinned::results_are_converged (which does NOT skip zero-valued
    // histograms) -- this one walks ConsequenceResults.ConsequenceResultList directly and skips
    // any zero-valued histogram, matching the C# source exactly.
    bool consequence_results_are_converged(double upper_confidence_limit_prob, double lower_confidence_limit_prob) {
        for (AggregatedConsequencesBinned& result : consequence_results_.consequence_result_list()) {
            if (result.consequence_histogram()->histogram_is_zero_valued()) {
                continue;
            }
            if (!result.consequence_histogram()->is_histogram_converged(upper_confidence_limit_prob,
                                                                          lower_confidence_limit_prob)) {
                return false;
            }
        }
        return true;
    }

    PerformanceByThresholds performance_by_thresholds_;
    StudyAreaConsequencesBinned consequence_results_;
    int impact_area_id_;
    bool is_null_;
    std::vector<CategoriedPairedData> consequence_frequency_functions_;
    std::deque<CategoriedUncertainPairedData> uncertain_consequence_frequency_curves_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_IMPACT_AREA_SCENARIO_RESULTS_HPP
