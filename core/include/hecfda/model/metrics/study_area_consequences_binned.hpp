// ported from: HEC.FDA.Model/metrics/StudyAreaConsequencesBinned.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_STUDY_AREA_CONSEQUENCES_BINNED_HPP
#define HECFDA_MODEL_METRICS_STUDY_AREA_CONSEQUENCES_BINNED_HPP
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/aggregated_consequences_binned.hpp"
#include "hecfda/model/metrics/aggregated_consequences_by_quantile.hpp"
#include "hecfda/model/metrics/consequence_extensions.hpp"
#include "hecfda/model/metrics/consequence_result.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/metrics/study_area_consequences_by_quantile.hpp"
#include "hecfda/model/paired_data/curve_meta_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/statistics/histograms/dynamic_histogram.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// Asset-category string constants, ported from `utilities.StringGlobalConstants` (an
// otherwise-unported utility class -- only these four members, plus UNASSIGNED, are used by the
// compute path this task ports; the whole file isn't otherwise pulled in, matching the
// `kDefaultMissingValue`-style "define locally, don't port the file" convention used by
// structure.hpp for `IntegerGlobalConstants.DEFAULT_MISSING_VALUE`).
inline constexpr const char* kStructureAssetCategory = "Structure";
inline constexpr const char* kContentAssetCategory = "Content";
inline constexpr const char* kOtherAssetCategory = "Other";
inline constexpr const char* kVehicleAssetCategory = "Vehicle";

// ported from: StudyAreaConsequencesBinned.cs `public class StudyAreaConsequencesBinned :
// ValidationErrorLogger`. The collection wrapper over per-(damageCategory, assetCategory,
// impactArea, ConsequenceType, RiskType) AggregatedConsequencesBinned results that the
// stage-damage compute drives: one instance per stage coordinate, fed ConsequenceResult
// realizations via the stage-damage add_consequence_realization overload, then converted to
// UncertainPairedData curves via the static to_uncertain_paired_data.
//
// Move-only: ConsequenceResultList holds AggregatedConsequencesBinned, itself move-only (owns
// unique_ptr<DynamicHistogram> members -- see aggregated_consequences_binned.hpp), so this class
// is move-only too (the implicitly-deleted copy ctor/assignment is intentional, not an oversight).
//
// `results_are_converged`/`remaining_iterations` are NON-CONST, deliberately deviating from the
// task brief's `const`-annotated signatures: both call AggregatedConsequencesBinned::
// consequence_histogram() (the non-const overload) and then DynamicHistogram::
// is_histogram_converged/estimate_iterations_remaining, which are themselves non-const on
// DynamicHistogram (they cache convergence state -- IsConverged/ConvergedIteration -- as a side
// effect, matching the C# IHistogram interface's non-const `IsHistogramConverged`/
// `EstimateIterationsRemaining`). This was already anticipated in
// aggregated_consequences_binned.hpp's class comment when Task 3 added the non-const
// consequence_histogram() overload specifically for this task.
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//  - The two "null"/dummy ctors, `StudyAreaConsequencesBinned(int impactAreaID)` and
//    `StudyAreaConsequencesBinned(bool isNull)`: both build a placeholder
//    `AggregatedConsequencesBinned(int, ConsequenceType, RiskType)` "dummy" instance, itself
//    already declined by aggregated_consequences_binned.hpp's own DONE_WITH_CONCERNS (it needs
//    the severed parameterless "ARBITRARY histogram" DynamicHistogram ctor). Not needed by this
//    task's compute path (the ctor below, `(std::vector<AggregatedConsequencesBinned>)`, is the
//    "public for testing" one the task brief calls out).
//  - `WriteToXML()`/`ReadFromXML(XElement)`: XML (de)serialization, no equivalent surface in this
//    port (repo-wide XML severance, matching CurveMetaData/ConvergenceCriteria/PairedData).
//  - `GetAggregateEmpiricalDistribution(...)`: needs DynamicHistogram::
//    ConvertToEmpiricalDistribution (deferred, see dynamic_histogram.hpp) and
//    Empirical::StackEmpiricalDistributions (not ported). Also emits the same MVVM Fatal
//    ErrorMessage-on-miss pattern as GetSpecificHistogram below.
//  - `ConsequenceExceededWithProbabilityQ(...)` (the "Aggregation"-region overload, distinct from
//    AggregatedConsequencesBinned's own same-named member): a
//    `ConsequenceResultList.FilterByCategories(...).Sum(...)` one-liner over
//    AggregatedConsequencesBinned::consequence_exceeded_with_probability_q (already ported, Task
//    3). Not required by this task's produced interface or fixture; trivial to add later using
//    consequence_extensions::filter_by_categories, mirroring sample_mean_damage below, if a
//    caller needs it. (Its sibling `SampleMeanDamage(...)` WAS ported, Phase 5 Task 6 -- see
//    `sample_mean_damage` below -- once ImpactAreaScenarioResults::
//    mean_expected_annual_consequences needed it.)
//  - MVVM `ValidationErrorLogger`/`MessageHub`/`ReportMessage`: no messaging/validation-log
//    infrastructure in this port (matches the repo-wide MVVM severance). In `GetSpecificHistogram`
//    (ported below as `get_specific_histogram`, public since Phase 5 Task 6) the C# emits a Fatal
//    `ErrorMessage` on a lookup miss and then returns a placeholder `new DynamicHistogram()` (the
//    same severed "ARBITRARY histogram" ctor referenced above) -- there is no placeholder to fall
//    back to here, so a miss throws `std::runtime_error` instead. This task's compute path never
//    actually misses (every (damageCategory, assetCategory) combination queried by
//    to_uncertain_paired_data comes from GetDamageCategories/GetAssetCategories, which only ever
//    enumerate categories that already exist in ConsequenceResultList), so the throw is
//    unreachable in practice, matching the C# Fatal-and-continue path being "should never happen"
//    in practice too.
//
// Phase 5 Task 6 additions (ImpactAreaScenarioResults, the container this class is nested inside):
// `equals` (fully const -- see that method's own comment for why it didn't need the non-const
// treatment `sample_mean_damage`/`results_are_converged`/`remaining_iterations` need), a const
// `get_consequence_result` overload it relies on, `sample_mean_damage` (see the removed
// SEVERANCES bullet above), `get_specific_histogram` made public, and a non-const
// `consequence_result_list()` overload (see that accessor's own comment).
//
// Phase 6 Task 4 UN-SEVERANCE: `convert_to_study_area_consequences_by_quantile` (static, below)
// was the other half of the `ConvertToStudyAreaConsequencesByQuantile(...)` SEVERANCES bullet
// removed above -- it needed StudyAreaConsequencesByQuantile (Task 3),
// AggregatedConsequencesByQuantile (Task 2), and
// AggregatedConsequencesBinned::convert_to_single_empirical_distribution_of_consequences (this
// same task, Task 4, in aggregated_consequences_binned.hpp), all now available.
class StudyAreaConsequencesBinned {
   public:
    using IDistribution = hecfda::statistics::distributions::IDistribution;
    using DynamicHistogram = hecfda::statistics::histograms::DynamicHistogram;
    using CurveMetaData = hecfda::model::paired_data::CurveMetaData;
    using UncertainPairedData = hecfda::model::paired_data::UncertainPairedData;

    // ported from: StudyAreaConsequencesBinned.cs `public StudyAreaConsequencesBinned(
    // List<AggregatedConsequencesBinned> damageResults)` -- "public for testing".
    explicit StudyAreaConsequencesBinned(std::vector<AggregatedConsequencesBinned> damage_results)
        : consequence_result_list_(std::move(damage_results)) {}

    // Move-only (see class comment): ConsequenceResultList elements are move-only.
    StudyAreaConsequencesBinned(StudyAreaConsequencesBinned&&) = default;
    StudyAreaConsequencesBinned& operator=(StudyAreaConsequencesBinned&&) = default;
    StudyAreaConsequencesBinned(const StudyAreaConsequencesBinned&) = delete;
    StudyAreaConsequencesBinned& operator=(const StudyAreaConsequencesBinned&) = delete;

    const std::vector<AggregatedConsequencesBinned>& consequence_result_list() const {
        return consequence_result_list_;
    }
    // Non-const overload, added Phase 5 Task 6: ImpactAreaScenarioResults::consequence_results_are_
    // converged/remaining_iterations (the ImpactAreaScenarioResults-level convergence helpers, NOT
    // the same-named methods below -- see impact_area_scenario_results.hpp) need mutable access to
    // each result's ConsequenceHistogram to call its non-const is_histogram_converged/
    // estimate_iterations_remaining, mirroring the const/non-const accessor pair convention already
    // established by PerformanceByThresholds::list_of_thresholds().
    std::vector<AggregatedConsequencesBinned>& consequence_result_list() { return consequence_result_list_; }

    // ported from: StudyAreaConsequencesBinned.cs `internal void AddNewConsequenceResultObject(
    // string damageCategory, string assetCategory, ConvergenceCriteria convergenceCriteria, int
    // impactAreaID, ConsequenceType consequenceType, RiskType riskType)`. Adds a freshly
    // constructed AggregatedConsequencesBinned only if no existing result matches the
    // (damageCategory, assetCategory, impactAreaID, consequenceType, riskType) combo.
    void add_new_consequence_result_object(const std::string& damage_category,
                                            const std::string& asset_category,
                                            statistics::ConvergenceCriteria convergence_criteria,
                                            int impact_area_id, ConsequenceType consequence_type,
                                            RiskType risk_type) {
        AggregatedConsequencesBinned* existing = get_consequence_result(
            damage_category, asset_category, impact_area_id, consequence_type, risk_type);
        if (existing == nullptr) {
            consequence_result_list_.emplace_back(damage_category, asset_category,
                                                    convergence_criteria, impact_area_id,
                                                    consequence_type, risk_type);
        }
    }

    // ported from: StudyAreaConsequencesBinned.cs `public void
    // AddExistingConsequenceResultObject(AggregatedConsequencesBinned consequenceResultToAdd)` --
    // "public for testing purposes". Adds the given (already-constructed) result only if no
    // existing result matches its own (damageCategory, assetCategory, region, ConsequenceType,
    // RiskType). Takes by value (moved in): AggregatedConsequencesBinned is move-only, mirroring
    // the C# reference-assignment `ConsequenceResultList.Add(consequenceResultToAdd)`.
    void add_existing_consequence_result_object(AggregatedConsequencesBinned consequence_result_to_add) {
        AggregatedConsequencesBinned* existing = get_consequence_result(
            consequence_result_to_add.damage_category(), consequence_result_to_add.asset_category(),
            consequence_result_to_add.region_id(), consequence_result_to_add.consequence_type(),
            consequence_result_to_add.risk_type());
        if (existing == nullptr) {
            consequence_result_list_.push_back(std::move(consequence_result_to_add));
        }
    }

    // ported from: StudyAreaConsequencesBinned.cs `internal void AddConsequenceRealization(double
    // damageEstimate, string damageCategory, string assetCategory, int impactAreaID, long
    // iteration, ConsequenceType consequenceType, RiskType riskType)` -- the EAD-binning overload
    // (single asset category, explicit ConsequenceType/RiskType).
    void add_consequence_realization(double damage_estimate, const std::string& damage_category,
                                      const std::string& asset_category, int impact_area_id,
                                      std::int64_t iteration, ConsequenceType consequence_type,
                                      RiskType risk_type) {
        require_consequence_result(damage_category, asset_category, impact_area_id, consequence_type,
                                    risk_type)
            ->add_consequence_realization(damage_estimate, iteration);
    }

    // ported from: StudyAreaConsequencesBinned.cs `internal void AddConsequenceRealization(
    // ConsequenceResult consequenceResult, string damageCategory, int impactAreaID, int
    // iteration)` -- the stage-damage binning overload. Splits the ConsequenceResult into FOUR
    // per-asset-category realizations (Structure/Content/Vehicle/Other, in that exact order,
    // matching the C# source), each looked up via the default-argument (ConsequenceType::Damage,
    // RiskType::Fail) overload of get_consequence_result -- transcribed field-for-field:
    //   Structure <- consequenceResult.StructureDamage / .DamagedStructuresQuantity
    //   Content   <- consequenceResult.ContentDamage   / .DamagedContentsQuantity
    //   Vehicle   <- consequenceResult.VehicleDamage   / .DamagedVehiclesQuantity
    //   Other     <- consequenceResult.OtherDamage     / .DamagedOthersQuantity
    void add_consequence_realization(const ConsequenceResult& consequence_result,
                                      const std::string& damage_category, int impact_area_id,
                                      int iteration) {
        require_consequence_result(damage_category, kStructureAssetCategory, impact_area_id)
            ->add_consequence_realization(consequence_result.structure_damage(), iteration,
                                           consequence_result.damaged_structures_quantity());
        require_consequence_result(damage_category, kContentAssetCategory, impact_area_id)
            ->add_consequence_realization(consequence_result.content_damage(), iteration,
                                           consequence_result.damaged_contents_quantity());
        require_consequence_result(damage_category, kVehicleAssetCategory, impact_area_id)
            ->add_consequence_realization(consequence_result.vehicle_damage(), iteration,
                                           consequence_result.damaged_vehicles_quantity());
        require_consequence_result(damage_category, kOtherAssetCategory, impact_area_id)
            ->add_consequence_realization(consequence_result.other_damage(), iteration,
                                           consequence_result.damaged_others_quantity());
    }

    // ported from: StudyAreaConsequencesBinned.cs `public void PutDataIntoHistograms()`.
    void put_data_into_histograms() {
        for (AggregatedConsequencesBinned& result : consequence_result_list_) {
            result.put_data_into_histogram();
        }
    }

    // ported from: StudyAreaConsequencesBinned.cs `public bool ResultsAreConverged(double
    // upperConfidenceLimit, double lowerConfidenceLimit)`. AND across every result's consequence
    // histogram, short-circuiting (break) on the first non-converged result. NON-CONST: see the
    // class comment. Dereferences consequence_histogram() unconditionally -- callers must have
    // run put_data_into_histograms() first (matches the C# NullReferenceException-on-miscall
    // behavior; not defensively guarded here, same as upstream).
    bool results_are_converged(double upper_confidence_limit, double lower_confidence_limit) {
        for (AggregatedConsequencesBinned& result : consequence_result_list_) {
            bool histogram_is_converged =
                result.consequence_histogram()->is_histogram_converged(upper_confidence_limit,
                                                                         lower_confidence_limit);
            if (!histogram_is_converged) {
                return false;
            }
        }
        return true;
    }

    // ported from: StudyAreaConsequencesBinned.cs `internal long RemainingIterations(double
    // upperProb, double lowerProb)`. Max across every result's estimated remaining iterations (0
    // if that result's histogram is zero-valued). NON-CONST: see the class comment. C#
    // `.Max()` throws InvalidOperationException on an empty sequence; reproduced here as
    // std::runtime_error rather than indexing undefined behavior on an empty
    // ConsequenceResultList.
    std::int64_t remaining_iterations(double upper_prob, double lower_prob) {
        if (consequence_result_list_.empty()) {
            throw std::runtime_error(
                "StudyAreaConsequencesBinned::remaining_iterations: ConsequenceResultList is "
                "empty (mirrors C# Enumerable.Max() throwing on an empty sequence).");
        }
        std::int64_t max_remaining = std::numeric_limits<std::int64_t>::min();
        for (AggregatedConsequencesBinned& result : consequence_result_list_) {
            std::int64_t remaining = result.consequence_histogram()->histogram_is_zero_valued()
                                          ? 0
                                          : result.consequence_histogram()->estimate_iterations_remaining(
                                                upper_prob, lower_prob);
            max_remaining = std::max(max_remaining, remaining);
        }
        return max_remaining;
    }

    // ported from: StudyAreaConsequencesBinned.cs `public static (List<UncertainPairedData>,
    // List<UncertainPairedData>) ToUncertainPairedData(List<double> xValues,
    // List<StudyAreaConsequencesBinned> yValues, int impactAreaID)`. For every (damageCategory x
    // assetCategory) combination present in the LAST element of yValues (`yValues[^1]`,
    // transcribed verbatim -- categories are assumed identical across every yValues element, so
    // only the last is actually consulted), collects that combination's consequence histogram
    // (and, in parallel, its damaged-element-quantity histogram) across every yValues element into
    // one UncertainPairedData each, paired against the SAME xValues array. Histograms are CLONED
    // (via DynamicHistogram's compiler-generated copy ctor -- deep-copies bin_counts_ and every
    // other member, no unique_ptr members to complicate it) into the fresh
    // UncertainPairedData's owned Yvals, rather than aliased: C# Yvals holds the SAME object
    // references ConsequenceResultList already owns (GC keeps them alive); this port's
    // UncertainPairedData owns unique_ptr<IDistribution> and StudyAreaConsequencesBinned's
    // AggregatedConsequencesBinned members retain their own histogram ownership, so a clone is the
    // value-semantics equivalent -- bit-identical InverseCDF/PDF/SampleMean results, since nothing
    // mutates a histogram again after put_data_into_histograms() has run. This also means NO new
    // "IHistogram[] ctor" was needed on UncertainPairedData: DynamicHistogram already derives
    // IDistribution (via ContinuousDistribution -- see dynamic_histogram.hpp's class comment), so
    // a cloned `unique_ptr<DynamicHistogram>` converts to `unique_ptr<IDistribution>` and the
    // EXISTING `UncertainPairedData(xs, vector<unique_ptr<IDistribution>>, metadata)` ctor is
    // reused verbatim (matching the C# source, which likewise has only ONE UncertainPairedData
    // ctor -- `ToUncertainPairedData`'s `IHistogram[]` arrays reach it via C# array covariance
    // over `IDistribution[]`, not a second overload).
    static std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>>
    to_uncertain_paired_data(const std::vector<double>& x_values,
                              const std::vector<StudyAreaConsequencesBinned>& y_values,
                              int impact_area_id) {
        std::vector<UncertainPairedData> damage_upds;
        std::vector<UncertainPairedData> quantity_upds;

        std::vector<std::string> damage_categories = y_values.back().get_damage_categories();
        std::vector<std::string> asset_categories = y_values.back().get_asset_categories();

        for (const std::string& damage_category : damage_categories) {
            for (const std::string& asset_category : asset_categories) {
                CurveMetaData damage_meta("X Values", "Consequences",
                                          "Consequences Uncertain Paired Data", damage_category,
                                          impact_area_id, asset_category);
                CurveMetaData quantity_meta("X Values", "Damaged Elements Quantity",
                                            "Damaged Elements Quantity Uncertain Paired Data",
                                            damage_category, impact_area_id, asset_category);

                std::vector<std::unique_ptr<IDistribution>> damage_histograms;
                std::vector<std::unique_ptr<IDistribution>> quantity_histograms;

                for (const StudyAreaConsequencesBinned& consequence_distributions : y_values) {
                    const DynamicHistogram* histogram = consequence_distributions.get_specific_histogram(
                        damage_category, asset_category, impact_area_id, false);
                    damage_histograms.push_back(std::make_unique<DynamicHistogram>(*histogram));

                    const DynamicHistogram* quantity_histogram = consequence_distributions.get_specific_histogram(
                        damage_category, asset_category, impact_area_id, true);
                    quantity_histograms.push_back(std::make_unique<DynamicHistogram>(*quantity_histogram));
                }

                damage_upds.emplace_back(x_values, std::move(damage_histograms), damage_meta);
                quantity_upds.emplace_back(x_values, std::move(quantity_histograms), quantity_meta);
            }
        }
        return {std::move(damage_upds), std::move(quantity_upds)};
    }

    // ported from: StudyAreaConsequencesBinned.cs `public static StudyAreaConsequencesByQuantile
    // ConvertToStudyAreaConsequencesByQuantile(StudyAreaConsequencesBinned
    // studyAreaConsequencesBinned, ConsequenceType filterByConsequenceType)`. UN-SEVERED Phase 6
    // Task 4 (see class comment). Filters ConsequenceResultList down to exactly the results whose
    // ConsequenceType equals the filter argument (a plain `==` compare, NOT the wildcard-aware
    // consequence_extensions::filter_by_categories predicate used elsewhere in this file --
    // transcribed verbatim from the C# `Where((r) => r.ConsequenceType ==
    // filterByConsequenceType)`), converts each survivor via
    // AggregatedConsequencesBinned::convert_to_single_empirical_distribution_of_consequences(), and
    // collects the results into a fresh StudyAreaConsequencesByQuantile via its
    // `(std::vector<AggregatedConsequencesByQuantile>)` "public for testing" ctor (matching the C#
    // `new(aggregatedConsequencesByQuantiles)` call).
    static StudyAreaConsequencesByQuantile convert_to_study_area_consequences_by_quantile(
        const StudyAreaConsequencesBinned& study_area_consequences_binned,
        ConsequenceType filter_by_consequence_type) {
        std::vector<AggregatedConsequencesByQuantile> aggregated_consequences_by_quantiles;
        for (const AggregatedConsequencesBinned& aggregated_consequences_binned :
             study_area_consequences_binned.consequence_result_list_) {
            if (aggregated_consequences_binned.consequence_type() != filter_by_consequence_type) {
                continue;
            }
            aggregated_consequences_by_quantiles.push_back(
                aggregated_consequences_binned.convert_to_single_empirical_distribution_of_consequences());
        }
        return StudyAreaConsequencesByQuantile(std::move(aggregated_consequences_by_quantiles));
    }

    // ported from: StudyAreaConsequencesBinned.cs `public double SampleMeanDamage(string
    // damageCategory = null, string assetCategory = null, int impactAreaID =
    // ..DEFAULT_MISSING_VALUE, ConsequenceType consequenceType = ConsequenceType.Damage, RiskType
    // riskType = RiskType.Fail)` -- the "Aggregation"-region overload (distinct from
    // AggregatedConsequencesBinned's own same-named method). Added Phase 5 Task 6: this task's
    // ImpactAreaScenarioResults::mean_expected_annual_consequences is the first caller (the class
    // comment's SEVERANCES note anticipated this: "trivial to add later ... if a caller needs it").
    // Sums sample_mean_expected_annual_consequences() over every result matching the filter --
    // NON-CONST for the same reason results_are_converged/remaining_iterations are (see class
    // comment): filter_by_categories needs mutable access to consequence_result_list_ to return
    // pointers into it, even though nothing here actually mutates a result.
    double sample_mean_damage(const std::optional<std::string>& damage_category = std::nullopt,
                               const std::optional<std::string>& asset_category = std::nullopt,
                               int impact_area_id = kDefaultMissingValue,
                               ConsequenceType consequence_type = ConsequenceType::Damage,
                               RiskType risk_type = RiskType::Fail) {
        std::vector<AggregatedConsequencesBinned*> matches = consequence_extensions::filter_by_categories(
            consequence_result_list_, damage_category, asset_category, impact_area_id, consequence_type,
            risk_type);
        double sum = 0.0;
        for (AggregatedConsequencesBinned* result : matches) {
            sum += result->sample_mean_expected_annual_consequences();
        }
        return sum;
    }

    // ported from: StudyAreaConsequencesBinned.cs `public IHistogram GetSpecificHistogram(string
    // damageCategory, string assetCategory, int impactAreaID, bool getQuantityHistogram = false)`.
    // Was private until Phase 5 Task 6: ImpactAreaScenarioResults::get_specific_histogram is this
    // method's first direct caller (`to_uncertain_paired_data` above already called it internally).
    // A direct linear scan for an EXACT (damageCategory, assetCategory, impactAreaID) match -- note
    // this does NOT filter by ConsequenceType/RiskType (unlike get_consequence_result/
    // filter_by_categories), matching the C# source exactly. On a miss: SEVERED MVVM
    // ErrorMessage/ReportMessage + severed placeholder `new DynamicHistogram()` fallback -- see the
    // class comment's SEVERANCES entry for why this throws instead.
    const DynamicHistogram* get_specific_histogram(const std::string& damage_category,
                                                    const std::string& asset_category, int impact_area_id,
                                                    bool get_quantity_histogram = false) const {
        const DynamicHistogram* return_histogram = nullptr;
        for (const AggregatedConsequencesBinned& result : consequence_result_list_) {
            if (result.damage_category() == damage_category && result.asset_category() == asset_category &&
                result.region_id() == impact_area_id) {
                return_histogram = get_quantity_histogram ? result.damaged_element_quantity_histogram()
                                                            : result.consequence_histogram();
            }
        }
        if (return_histogram == nullptr) {
            throw std::runtime_error(
                "StudyAreaConsequencesBinned::get_specific_histogram: no result for "
                "damage_category='" +
                damage_category + "', asset_category='" + asset_category +
                "', impact_area_id=" + std::to_string(impact_area_id) +
                " (mirrors the C# Fatal-ErrorMessage-and-placeholder-histogram fallback, which has "
                "no equivalent placeholder in this port -- see class comment)");
        }
        return return_histogram;
    }

    // ported from: StudyAreaConsequencesBinned.cs `public bool Equals(StudyAreaConsequencesBinned
    // inputDamageResults)`. Added Phase 5 Task 6: ImpactAreaScenarioResults::equals is this
    // method's first caller (the class comment's SEVERANCES note anticipated this). Unlike
    // sample_mean_damage/get_specific_histogram above, kept fully CONST: uses the const
    // get_consequence_result overload below (itself backed by consequence_extensions::
    // filter_by_categories's const overload) rather than the non-const private helper the other
    // methods share, since AggregatedConsequencesBinned::equals and DynamicHistogram::equals are
    // both const -- no forced non-const propagation here, matching Threshold::equals/
    // PerformanceByThresholds::equals/AggregatedConsequencesBinned::equals's own const convention.
    bool equals(const StudyAreaConsequencesBinned& input_damage_results) const {
        for (const AggregatedConsequencesBinned& damage_result : consequence_result_list_) {
            const AggregatedConsequencesBinned* input_damage_result = input_damage_results.get_consequence_result(
                damage_result.damage_category(), damage_result.asset_category(), damage_result.region_id(),
                damage_result.consequence_type(), damage_result.risk_type());
            if (input_damage_result == nullptr) {
                return false;
            }
            if (!damage_result.equals(*input_damage_result)) {
                return false;
            }
        }
        return true;
    }

   private:
    // ported from: StudyAreaConsequencesBinned.cs `public AggregatedConsequencesBinned
    // GetConsequenceResult(string damageCategory, string assetCategory, int impactAreaID =
    // ..DEFAULT_MISSING_VALUE, ConsequenceType consequenceType = ConsequenceType.Damage, RiskType
    // riskType = RiskType.Fail)`. Default args transcribed verbatim (note: RiskType defaults to
    // Fail here, NOT Total/wildcard like filter_by_categories's own default -- GetConsequenceResult
    // always forwards an explicit riskType). Returns nullptr on no match (C# `FirstOrDefault()`
    // returning null), rather than the AggregatedConsequencesByQuantile/Empirical aggregation-region
    // callers this also served upstream (severed here -- see class comment).
    AggregatedConsequencesBinned* get_consequence_result(
        const std::string& damage_category, const std::string& asset_category,
        int impact_area_id = kDefaultMissingValue, ConsequenceType consequence_type = ConsequenceType::Damage,
        RiskType risk_type = RiskType::Fail) {
        std::vector<AggregatedConsequencesBinned*> matches = consequence_extensions::filter_by_categories(
            consequence_result_list_, damage_category, asset_category, impact_area_id, consequence_type,
            risk_type);
        return matches.empty() ? nullptr : matches.front();
    }

    // const overload of the above, added Phase 5 Task 6 for equals() (see that method's comment):
    // the same lookup, but through consequence_extensions::filter_by_categories's const overload,
    // so a const StudyAreaConsequencesBinned can still be queried read-only.
    const AggregatedConsequencesBinned* get_consequence_result(
        const std::string& damage_category, const std::string& asset_category,
        int impact_area_id = kDefaultMissingValue, ConsequenceType consequence_type = ConsequenceType::Damage,
        RiskType risk_type = RiskType::Fail) const {
        std::vector<const AggregatedConsequencesBinned*> matches = consequence_extensions::filter_by_categories(
            consequence_result_list_, damage_category, asset_category, impact_area_id, consequence_type,
            risk_type);
        return matches.empty() ? nullptr : matches.front();
    }

    // Shared by both add_consequence_realization overloads: get_consequence_result, but throws
    // instead of returning nullptr on a miss. C# has no equivalent helper -- both overloads call
    // GetConsequenceResult(...) directly and dereference the (possibly null) result immediately,
    // which throws NullReferenceException on a miss. This port makes that failure mode an explicit,
    // documented std::runtime_error instead of an implicit null-pointer dereference (undefined
    // behavior in C++, unlike C#'s catchable NRE).
    AggregatedConsequencesBinned* require_consequence_result(const std::string& damage_category,
                                                               const std::string& asset_category,
                                                               int impact_area_id,
                                                               ConsequenceType consequence_type = ConsequenceType::Damage,
                                                               RiskType risk_type = RiskType::Fail) {
        AggregatedConsequencesBinned* result = get_consequence_result(
            damage_category, asset_category, impact_area_id, consequence_type, risk_type);
        if (result == nullptr) {
            throw std::runtime_error(
                "StudyAreaConsequencesBinned: no AggregatedConsequencesBinned matches "
                "damage_category='" +
                damage_category + "', asset_category='" + asset_category +
                "', impact_area_id=" + std::to_string(impact_area_id) +
                " (mirrors C# NullReferenceException on GetConsequenceResult(...) returning null)");
        }
        return result;
    }

    // ported from: StudyAreaConsequencesBinned.cs `private List<string> GetAssetCategories()`.
    // Distinct asset categories, in first-seen order.
    std::vector<std::string> get_asset_categories() const {
        std::vector<std::string> categories;
        for (const AggregatedConsequencesBinned& result : consequence_result_list_) {
            if (std::find(categories.begin(), categories.end(), result.asset_category()) == categories.end()) {
                categories.push_back(result.asset_category());
            }
        }
        return categories;
    }

    // ported from: StudyAreaConsequencesBinned.cs `private List<string> GetDamageCategories()`.
    // Distinct damage categories, in first-seen order.
    std::vector<std::string> get_damage_categories() const {
        std::vector<std::string> categories;
        for (const AggregatedConsequencesBinned& result : consequence_result_list_) {
            if (std::find(categories.begin(), categories.end(), result.damage_category()) == categories.end()) {
                categories.push_back(result.damage_category());
            }
        }
        return categories;
    }

    // ported from: StudyAreaConsequencesBinned.cs's implicit `utilities.IntegerGlobalConstants.
    // DEFAULT_MISSING_VALUE` default arg on GetConsequenceResult (-999), defined locally per the
    // repo's established convention (see structure.hpp's kDefaultMissingValue) since the whole
    // (mostly-severed) utilities file isn't otherwise ported.
    static constexpr int kDefaultMissingValue = -999;

    std::vector<AggregatedConsequencesBinned> consequence_result_list_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_STUDY_AREA_CONSEQUENCES_BINNED_HPP
