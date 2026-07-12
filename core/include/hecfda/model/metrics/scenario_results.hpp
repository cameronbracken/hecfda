// ported from: HEC.FDA.Model/metrics/ScenarioResults.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_SCENARIO_RESULTS_HPP
#define HECFDA_MODEL_METRICS_SCENARIO_RESULTS_HPP
#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/aggregated_consequences_binned.hpp"
#include "hecfda/model/metrics/aggregated_consequences_by_quantile.hpp"
#include "hecfda/model/metrics/categoried_uncertain_paired_data.hpp"
#include "hecfda/model/metrics/consequence_extensions.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/metrics/impact_area_scenario_results.hpp"
#include "hecfda/model/metrics/study_area_consequences_binned.hpp"
#include "hecfda/model/metrics/study_area_consequences_by_quantile.hpp"
#include "hecfda/model/paired_data/curve_meta_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/statistics/distributions/empirical.hpp"
#include "hecfda/statistics/histograms/dynamic_histogram.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: ScenarioResults.cs `public class ScenarioResults : ValidationErrorLogger`. The
// container `Scenario.Compute` (not yet ported) returns: a list of `ImpactAreaScenarioResults`
// (Phase 5) plus the scenario-level aggregators that sum/enumerate across every impact area --
// `Alternative.AnnualizationCompute`'s eventual consumer of one base-year and one future-year
// instance of this class.
//
// Move-only: `results_list_` holds `ImpactAreaScenarioResults`, itself move-only (see that
// header's class comment), so `std::vector<ImpactAreaScenarioResults>` -- and therefore this
// class -- is move-only too, matching the "fresh construction per use" convention every other
// Phase 5/6 metrics container in this file tree already follows.
//
// `ComputeDate`/`SoftwareVersion`: plain `std::string` fields with get/set, transcribed exactly
// as C# declared them (`{ get; set; }` auto-properties). SEVERANCE: `ScenarioResults.cs` itself
// never stamps these with `DateTime.Now`/an `Assembly` version anywhere in this file -- that
// stamping (if any) belongs to the not-yet-ported `Scenario.Compute` caller (Task 8). This port
// therefore has nothing to sever HERE beyond documenting the invariant: never add a `DateTime`/
// `Assembly`-reading default to these two fields when `Scenario::compute` is ported later: they
// stay caller-set plain strings, matching this file's own (already-severed-of-timestamps)
// surface.
//
// `GetResults(int)` on a miss: SEVERED MVVM Fatal `ErrorMessage`+dummy-`ImpactAreaScenarioResults
// (9999, true)`-fallback, per the repo-wide convention already established by
// `PerformanceByThresholds::get_threshold`/`StudyAreaConsequencesBinned::require_consequence_
// result`/`SystemPerformanceResults::get_assurance`: this port throws `std::runtime_error`
// instead. Unlike those precedents, `get_results` has more than one direct caller inside THIS
// class (every AEP/assurance pass-through, plus `equals`), so this is a real (if narrow)
// behavior change from C#: `equals(...)` in particular will now THROW rather than silently
// return `false` if the comparison scenario is missing one of `this`'s impact area IDs (C#'s
// dummy fallback makes `ImpactAreaScenarioResults::Equals` compare against a mismatched
// placeholder object, which returns `false` without throwing). Documented, not worked around --
// matches how `PerformanceByThresholds::equals` already tolerates `get_threshold`'s own
// analogous throw-on-miss changing behavior relative to C#'s dummy fallback.
//
// `GetAccumulatedLifeLossFnCurveData()`'s `return null` (no life-loss data, or mismatched Xvals
// across impact areas): ported as `std::optional<UncertainPairedData>` returning `std::nullopt`,
// matching the established convention for a nullable move-only `UncertainPairedData` result (see
// `SystemPerformanceResults::system_response_function_`/`ImpactAreaScenarioSimulation`'s own
// `std::optional<UncertainPairedData>` members) -- `UncertainPairedData` has no default/null
// sentinel state of its own (move-only, no default ctor with "empty" semantics), so `optional` is
// the only faithful analogue of a nullable reference return here. Reads `(DynamicHistogram)
// upd.Yvals[i]` via the new `UncertainPairedData::y_at(i)` accessor added by this task (see that
// header) -- no other caller in this port needed indexed read access into an already-built
// curve's Yvals array, so no such accessor existed before this task.
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//  - MVVM `ValidationErrorLogger` base + `ErrorMessage`/`ReportMessage`: no messaging/validation-
//    log infrastructure in this port (repo-wide MVVM severance). Every miss/empty-result branch
//    that reported a Fatal `ErrorMessage` in C# either throws (see `get_results` above) or
//    returns the same non-throwing fallback value C# itself returns after the (omitted)
//    `ReportMessage` call (`get_consequences_distribution`'s empty-stack `Empirical()` fallback,
//    matching `StudyAreaConsequencesByQuantile::get_aggregate_empirical_distribution`'s own
//    documented no-op-`ReportMessage` treatment).
//  - `WriteToXML()`/`static ReadFromXML(XElement)`: XML (de)serialization, no equivalent surface
//    in this port (repo-wide XML severance).
class ScenarioResults {
   public:
    using DynamicHistogram = hecfda::statistics::histograms::DynamicHistogram;
    using Empirical = hecfda::statistics::distributions::Empirical;
    using UncertainPairedData = hecfda::model::paired_data::UncertainPairedData;
    using CurveMetaData = hecfda::model::paired_data::CurveMetaData;
    using IDistribution = hecfda::statistics::distributions::IDistribution;

    // ported from: ScenarioResults.cs `internal ScenarioResults()` -- empty ResultsList,
    // ComputeDate/SoftwareVersion left at their C# `null`/default(string) (transcribed as empty
    // std::string, the value-type analogue).
    ScenarioResults() = default;

    // Move-only (see class comment).
    ScenarioResults(ScenarioResults&&) = default;
    ScenarioResults& operator=(ScenarioResults&&) = default;
    ScenarioResults(const ScenarioResults&) = delete;
    ScenarioResults& operator=(const ScenarioResults&) = delete;

    // ported from: ScenarioResults.cs `public string ComputeDate { get; set; }` /
    // `public string SoftwareVersion { get; set; }`. See class comment: never stamped here.
    const std::string& compute_date() const { return compute_date_; }
    void set_compute_date(std::string value) { compute_date_ = std::move(value); }
    const std::string& software_version() const { return software_version_; }
    void set_software_version(std::string value) { software_version_ = std::move(value); }

    // ported from: ScenarioResults.cs `public List<ImpactAreaScenarioResults> ResultsList { get; }
    // = new List<ImpactAreaScenarioResults>();`.
    std::vector<ImpactAreaScenarioResults>& results_list() { return results_list_; }
    const std::vector<ImpactAreaScenarioResults>& results_list() const { return results_list_; }

    // ported from: ScenarioResults.cs `public void AddResults(ImpactAreaScenarioResults
    // resultsToAdd)`.
    void add_results(ImpactAreaScenarioResults results_to_add) {
        results_list_.push_back(std::move(results_to_add));
    }

    // ported from: ScenarioResults.cs `public ImpactAreaScenarioResults GetResults(int
    // impactAreaID)`. See class comment's SEVERANCES-adjacent note: throws on a miss instead of
    // C#'s log-and-return-dummy(9999)-fallback.
    ImpactAreaScenarioResults& get_results(int impact_area_id) {
        for (ImpactAreaScenarioResults& results : results_list_) {
            if (results.impact_area_id() == impact_area_id) {
                return results;
            }
        }
        throw std::runtime_error(
            "ScenarioResults::get_results: no ImpactAreaScenarioResults found for impact_area_id=" +
            std::to_string(impact_area_id) +
            " (mirrors C# ReportMessage(Fatal)+dummy(9999)-fallback, severed here per repo "
            "convention -- see PerformanceByThresholds::get_threshold)");
    }
    const ImpactAreaScenarioResults& get_results(int impact_area_id) const {
        for (const ImpactAreaScenarioResults& results : results_list_) {
            if (results.impact_area_id() == impact_area_id) {
                return results;
            }
        }
        throw std::runtime_error(
            "ScenarioResults::get_results: no ImpactAreaScenarioResults found for impact_area_id=" +
            std::to_string(impact_area_id) +
            " (mirrors C# ReportMessage(Fatal)+dummy(9999)-fallback, severed here per repo "
            "convention -- see PerformanceByThresholds::get_threshold)");
    }

    // ported from: ScenarioResults.cs `public List<int> GetImpactAreaIDs(ConsequenceType
    // consequenceType)`. Distinct RegionID (first-seen order) across every
    // AggregatedConsequencesBinned in every impact area's ConsequenceResults whose ConsequenceType
    // matches.
    std::vector<int> get_impact_area_ids(ConsequenceType consequence_type) const {
        std::vector<int> ids;
        for (const ImpactAreaScenarioResults& ia : results_list_) {
            for (const AggregatedConsequencesBinned& result : ia.consequence_results().consequence_result_list()) {
                if (result.consequence_type() != consequence_type) continue;
                if (std::find(ids.begin(), ids.end(), result.region_id()) == ids.end()) {
                    ids.push_back(result.region_id());
                }
            }
        }
        return ids;
    }

    // ported from: ScenarioResults.cs `public List<string> GetAssetCategories(ConsequenceType
    // consequenceType = ConsequenceType.Damage)`. Distinct AssetCategory (first-seen order).
    std::vector<std::string> get_asset_categories(ConsequenceType consequence_type = ConsequenceType::Damage) const {
        std::vector<std::string> categories;
        for (const ImpactAreaScenarioResults& ia : results_list_) {
            for (const AggregatedConsequencesBinned& result : ia.consequence_results().consequence_result_list()) {
                if (result.consequence_type() != consequence_type) continue;
                if (std::find(categories.begin(), categories.end(), result.asset_category()) == categories.end()) {
                    categories.push_back(result.asset_category());
                }
            }
        }
        return categories;
    }

    // ported from: ScenarioResults.cs `public List<string> GetDamageCategories(ConsequenceType
    // consequenceType = ConsequenceType.Damage)`. Distinct DamageCategory (first-seen order).
    std::vector<std::string> get_damage_categories(ConsequenceType consequence_type = ConsequenceType::Damage) const {
        std::vector<std::string> categories;
        for (const ImpactAreaScenarioResults& ia : results_list_) {
            for (const AggregatedConsequencesBinned& result : ia.consequence_results().consequence_result_list()) {
                if (result.consequence_type() != consequence_type) continue;
                if (std::find(categories.begin(), categories.end(), result.damage_category()) == categories.end()) {
                    categories.push_back(result.damage_category());
                }
            }
        }
        return categories;
    }

    // ported from: ScenarioResults.cs `public List<RiskType> GetRiskTypes()`. Distinct RiskType
    // (first-seen order), NO ConsequenceType filter (matches C#'s unfiltered `.Select(...)`).
    std::vector<RiskType> get_risk_types() const {
        std::vector<RiskType> types;
        for (const ImpactAreaScenarioResults& ia : results_list_) {
            for (const AggregatedConsequencesBinned& result : ia.consequence_results().consequence_result_list()) {
                if (std::find(types.begin(), types.end(), result.risk_type()) == types.end()) {
                    types.push_back(result.risk_type());
                }
            }
        }
        return types;
    }

    // ported from: ScenarioResults.cs `public List<ConsequenceType> GetConsequenceTypes()`.
    // Distinct ConsequenceType (first-seen order), no filter.
    std::vector<ConsequenceType> get_consequence_types() const {
        std::vector<ConsequenceType> types;
        for (const ImpactAreaScenarioResults& ia : results_list_) {
            for (const AggregatedConsequencesBinned& result : ia.consequence_results().consequence_result_list()) {
                if (std::find(types.begin(), types.end(), result.consequence_type()) == types.end()) {
                    types.push_back(result.consequence_type());
                }
            }
        }
        return types;
    }

    // ported from: ScenarioResults.cs `public IHistogram GetAEPHistogramForPlotting(int
    // impactAreaID, int thresholdID = 0)`. NON-const: ImpactAreaScenarioResults::
    // get_aep_histogram_for_plotting is itself non-const.
    DynamicHistogram& get_aep_histogram_for_plotting(int impact_area_id, int threshold_id = 0) {
        return get_results(impact_area_id).get_aep_histogram_for_plotting(threshold_id);
    }
    // ported from: ScenarioResults.cs `public double MeanAEP(int impactAreaID, int thresholdID =
    // 0)`.
    double mean_aep(int impact_area_id, int threshold_id = 0) const {
        return get_results(impact_area_id).mean_aep(threshold_id);
    }
    // ported from: ScenarioResults.cs `public double MedianAEP(int impactAreaID, int thresholdID =
    // 0)`.
    double median_aep(int impact_area_id, int threshold_id = 0) const {
        return get_results(impact_area_id).median_aep(threshold_id);
    }
    // ported from: ScenarioResults.cs `public double AssuranceOfAEP(int impactAreaID, double
    // exceedanceProbability, int thresholdID = 0)`.
    double assurance_of_aep(int impact_area_id, double exceedance_probability, int threshold_id = 0) const {
        return get_results(impact_area_id).assurance_of_aep(threshold_id, exceedance_probability);
    }
    // ported from: ScenarioResults.cs `public double AEPWithGivenAssurance(int impactAreaID,
    // double assurance, int thresholdID = 0)`.
    double aep_with_given_assurance(int impact_area_id, double assurance, int threshold_id = 0) const {
        return get_results(impact_area_id).aep_with_given_assurance(threshold_id, assurance);
    }
    // ported from: ScenarioResults.cs `public double LongTermExceedanceProbability(int
    // impactAreaID, int years, int thresholdID = 0)`.
    double long_term_exceedance_probability(int impact_area_id, int years, int threshold_id = 0) const {
        return get_results(impact_area_id).long_term_exceedance_probability(threshold_id, years);
    }
    // ported from: ScenarioResults.cs `public double AssuranceOfEvent(int impactAreaID, double
    // standardNonExceedanceProbability, int thresholdID = 0)`. NON-const: ImpactAreaScenarioResults
    // ::assurance_of_event is itself non-const.
    double assurance_of_event(int impact_area_id, double standard_non_exceedance_probability,
                               int threshold_id = 0) {
        return get_results(impact_area_id).assurance_of_event(threshold_id, standard_non_exceedance_probability);
    }

    // ported from: ScenarioResults.cs `public double SampleMeanExpectedAnnualConsequences(int
    // impactAreaID = ..DEFAULT_MISSING_VALUE, string damageCategory = null, string assetCategory =
    // null, ConsequenceType consequenceType = ConsequenceType.Damage, RiskType riskType =
    // RiskType.Fail)`. Sums StudyAreaConsequencesBinned::sample_mean_damage across every impact
    // area. NON-const: sample_mean_damage is itself non-const (see that method's own comment).
    double sample_mean_expected_annual_consequences(int impact_area_id = kDefaultMissingValue,
                                                      const std::optional<std::string>& damage_category = std::nullopt,
                                                      const std::optional<std::string>& asset_category = std::nullopt,
                                                      ConsequenceType consequence_type = ConsequenceType::Damage,
                                                      RiskType risk_type = RiskType::Fail) {
        double consequence_value = 0.0;
        for (ImpactAreaScenarioResults& ia : results_list_) {
            consequence_value +=
                ia.consequence_results().sample_mean_damage(damage_category, asset_category, impact_area_id,
                                                              consequence_type, risk_type);
        }
        return consequence_value;
    }

    // ported from: ScenarioResults.cs `public double ConsequencesExceededWithProbabilityQ(double
    // exceedanceProbability, int impactAreaID = ..DEFAULT_MISSING_VALUE, string damageCategory =
    // null, string assetCategory = null, ConsequenceType consequenceType = ConsequenceType.Damage,
    // RiskType riskType = RiskType.Total)`. Sums InverseCDF(1 - exceedanceProbability) over every
    // AggregatedConsequencesBinned matching the filter, across every impact area. Fully const:
    // consequence_extensions::filter_by_categories's const overload +
    // AggregatedConsequencesBinned::consequence_histogram()'s const overload + DynamicHistogram::
    // inverse_cdf (const) chain all the way through.
    double consequences_exceeded_with_probability_q(
        double exceedance_probability, int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage, RiskType risk_type = RiskType::Total) const {
        double non_exceedance_probability = 1.0 - exceedance_probability;
        double consequence_value = 0.0;
        for (const ImpactAreaScenarioResults& ia : results_list_) {
            std::vector<const AggregatedConsequencesBinned*> matches = consequence_extensions::filter_by_categories(
                ia.consequence_results().consequence_result_list(), damage_category, asset_category, impact_area_id,
                consequence_type, risk_type);
            for (const AggregatedConsequencesBinned* result : matches) {
                consequence_value += result->consequence_histogram()->inverse_cdf(non_exceedance_probability);
            }
        }
        return consequence_value;
    }

    // ported from: ScenarioResults.cs `public Empirical GetConsequencesDistribution(int
    // impactAreaID = ..DEFAULT_MISSING_VALUE, string damageCategory = null, string assetCategory =
    // null, ConsequenceType consequenceType = ConsequenceType.Damage, RiskType riskType =
    // RiskType.Total)`. Per impact area: filter -> DynamicHistogram::
    // convert_to_empirical_distribution -> collect; then Empirical::stack_empirical_distributions
    // (sum) across the whole collection. SEVERED MVVM Fatal ErrorMessage-on-empty: returns a
    // default `Empirical()` instead of C#'s log-and-return-arbitrary-object fallback (same value
    // C#'s own `new Empirical()` fallback returns -- only the ReportMessage call itself is
    // omitted). Fully const, same chain as consequences_exceeded_with_probability_q above.
    Empirical get_consequences_distribution(int impact_area_id = kDefaultMissingValue,
                                             const std::optional<std::string>& damage_category = std::nullopt,
                                             const std::optional<std::string>& asset_category = std::nullopt,
                                             ConsequenceType consequence_type = ConsequenceType::Damage,
                                             RiskType risk_type = RiskType::Total) const {
        std::vector<Empirical> empirical_dists_to_stack;
        for (const ImpactAreaScenarioResults& ia : results_list_) {
            std::vector<const AggregatedConsequencesBinned*> matches = consequence_extensions::filter_by_categories(
                ia.consequence_results().consequence_result_list(), damage_category, asset_category, impact_area_id,
                consequence_type, risk_type);
            for (const AggregatedConsequencesBinned* result : matches) {
                empirical_dists_to_stack.push_back(result->consequence_histogram()->convert_to_empirical_distribution());
            }
        }
        if (empirical_dists_to_stack.empty()) {
            // SEVERED MVVM: C# reports a Fatal ErrorMessage via ReportMessage(this, ...) here
            // before returning `new Empirical()` below -- omitted (documented no-op, see class
            // comment).
            return Empirical();
        }
        return Empirical::stack_empirical_distributions(empirical_dists_to_stack, Empirical::StackOp::sum);
    }

    // ported from: ScenarioResults.cs `public UncertainPairedData
    // GetAccumulatedLifeLossFnCurveData()`. Accumulates life-loss F-N curve data across every
    // impact area by summing the DynamicHistogram distributions at each AEP ordinate. Returns
    // `std::nullopt` (C#'s `return null`) if fewer than two impact areas carry LifeLoss uncertain-
    // curve data, or if the Xvals arrays don't match across impact areas -- see class comment for
    // why `std::optional<UncertainPairedData>` is this port's nullable-return analogue.
    std::optional<UncertainPairedData> get_accumulated_life_loss_fn_curve_data() const {
        std::vector<UncertainPairedData> life_loss_upds;
        for (const ImpactAreaScenarioResults& ia : results_list_) {
            const CategoriedUncertainPairedData* curve = nullptr;
            for (const CategoriedUncertainPairedData& candidate : ia.uncertain_consequence_frequency_curves()) {
                if (candidate.consequence_type() == ConsequenceType::LifeLoss) {
                    curve = &candidate;
                    break;
                }
            }
            if (curve != nullptr && !curve->y_histograms().empty()) {
                life_loss_upds.push_back(curve->get_uncertain_paired_data());
            }
        }

        if (life_loss_upds.size() <= 1) {
            return std::nullopt;
        }

        const std::vector<double>& reference_xvals = life_loss_upds.front().xvals();
        for (std::size_t i = 1; i < life_loss_upds.size(); ++i) {
            if (life_loss_upds[i].xvals() != reference_xvals) {
                return std::nullopt;
            }
        }

        std::vector<Empirical> stacked_empiricals;
        stacked_empiricals.reserve(reference_xvals.size());
        for (std::size_t i = 0; i < reference_xvals.size(); ++i) {
            std::vector<Empirical> empiricals_at_ordinate;
            empiricals_at_ordinate.reserve(life_loss_upds.size());
            for (const UncertainPairedData& upd : life_loss_upds) {
                const auto* histogram = dynamic_cast<const DynamicHistogram*>(&upd.y_at(i));
                if (histogram == nullptr) {
                    // Mirrors C#'s unchecked `(DynamicHistogram)upd.Yvals[i]` hard cast, which
                    // throws InvalidCastException on a type mismatch -- every Yvals entry this
                    // port ever populates for a life-loss curve IS a DynamicHistogram (see
                    // CategoriedUncertainPairedData::get_uncertain_paired_data), so this branch is
                    // unreachable in practice, matching the C# cast's own "should never happen".
                    throw std::runtime_error(
                        "ScenarioResults::get_accumulated_life_loss_fn_curve_data: Yvals[i] is not "
                        "a DynamicHistogram (mirrors C#'s InvalidCastException on a bad "
                        "(DynamicHistogram) cast)");
                }
                empiricals_at_ordinate.push_back(histogram->convert_to_empirical_distribution());
            }
            stacked_empiricals.push_back(
                Empirical::stack_empirical_distributions(empiricals_at_ordinate, Empirical::StackOp::sum));
        }

        std::vector<std::unique_ptr<IDistribution>> ys;
        ys.reserve(stacked_empiricals.size());
        for (Empirical& empirical : stacked_empiricals) {
            ys.push_back(std::make_unique<Empirical>(std::move(empirical)));
        }
        return UncertainPairedData(reference_xvals, std::move(ys), CurveMetaData());
    }

    // ported from: ScenarioResults.cs `public static StudyAreaConsequencesByQuantile
    // ConvertToStudyAreaConsequencesByQuantile(ScenarioResults results, ConsequenceType
    // consequenceTypeFilter)`. Loops every impact area, converts its ConsequenceResults via
    // StudyAreaConsequencesBinned::convert_to_study_area_consequences_by_quantile (Phase 6 Task
    // 4), concatenates every resulting ConsequenceResultList, and wraps the total in a fresh
    // StudyAreaConsequencesByQuantile via its "public for testing" ctor.
    static StudyAreaConsequencesByQuantile convert_to_study_area_consequences_by_quantile(
        const ScenarioResults& results, ConsequenceType consequence_type_filter) {
        std::vector<AggregatedConsequencesByQuantile> aggregated_consequences_by_quantiles;
        for (const ImpactAreaScenarioResults& ia : results.results_list_) {
            StudyAreaConsequencesByQuantile per_impact_area =
                StudyAreaConsequencesBinned::convert_to_study_area_consequences_by_quantile(ia.consequence_results(),
                                                                                              consequence_type_filter);
            const std::vector<AggregatedConsequencesByQuantile>& list = per_impact_area.consequence_result_list();
            aggregated_consequences_by_quantiles.insert(aggregated_consequences_by_quantiles.end(), list.begin(),
                                                          list.end());
        }
        return StudyAreaConsequencesByQuantile(std::move(aggregated_consequences_by_quantiles));
    }

    // ported from: ScenarioResults.cs `public bool Equals(ScenarioResults
    // scenarioResultsForComparison)`. See class comment's `get_results` note: THROWS (rather than
    // C#'s silent-`false`-via-dummy-fallback) if `scenario_results_for_comparison` is missing an
    // impact area ID this object has.
    bool equals(const ScenarioResults& scenario_results_for_comparison) const {
        bool results_are_equal = true;
        for (const ImpactAreaScenarioResults& scenario_results : results_list_) {
            const ImpactAreaScenarioResults& to_compare =
                scenario_results_for_comparison.get_results(scenario_results.impact_area_id());
            results_are_equal = scenario_results.equals(to_compare);
            if (!results_are_equal) {
                break;
            }
        }
        return results_are_equal;
    }

   private:
    // ported from: ScenarioResults.cs's implicit `utilities.IntegerGlobalConstants.
    // DEFAULT_MISSING_VALUE` default arg (-999), defined locally per the repo's established
    // convention (see study_area_consequences_binned.hpp's own kDefaultMissingValue) since the
    // whole (mostly-severed) utilities file isn't otherwise ported.
    static constexpr int kDefaultMissingValue = -999;

    std::string compute_date_;
    std::string software_version_;
    std::vector<ImpactAreaScenarioResults> results_list_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_SCENARIO_RESULTS_HPP
