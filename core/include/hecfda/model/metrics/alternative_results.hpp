// ported from: HEC.FDA.Model/metrics/AlternativeResults.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_ALTERNATIVE_RESULTS_HPP
#define HECFDA_MODEL_METRICS_ALTERNATIVE_RESULTS_HPP
#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/aggregated_consequences_by_quantile.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/metrics/scenario_results.hpp"
#include "hecfda/model/metrics/study_area_consequences_by_quantile.hpp"
#include "hecfda/statistics/distributions/empirical.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: AlternativeResults.cs `public class AlternativeResults : ValidationErrorLogger` --
// the container `Alternative.AnnualizationCompute` (Task 9, not yet ported) returns: the
// study-area equivalent-annual-damage (EqAD) quantile results plus the base-year/future-year
// `ScenarioResults` (Task 5) the EqAD was computed from. `AlternativeComparisonReport` (Task 10)
// consumes with-project/without-project instances of this class.
//
// THE CORE DELEGATION PATTERN (the reason this class exists): every EqAD-facing query branches on
// `scenarios_are_identical_` -- when the base-year and future-year `ScenarioResults` are equal
// (the identical-scenario short-circuit `Alternative::annualization_compute` takes when there's
// nothing to interpolate/discount between), the EqAD figure IS the (single) scenario's own
// expected-annual-consequences figure, so the query delegates straight to
// `base_year_scenario_results_`; otherwise it delegates to `eqad_results_`, the actual
// quantile-walked EqAD distribution. `sample_mean_base_year_ead`/`sample_mean_future_year_ead`
// (and their distribution/exceedance siblings) never branch -- they always read their own
// `ScenarioResults`, independent of the EqAD path.
//
// Move-only: holds `base_year_scenario_results_`/`future_year_scenario_results_`, both
// `ScenarioResults` (itself move-only, see that header's class comment), so this class is
// necessarily move-only too -- explicit move/copy declarations added, matching house style.
//
// Ctors transcribed exactly from the C# source's three ctors:
//  - Parameterless: `IsNull = true`, `AlternativeID = 0`, `EqadResults = StudyAreaConsequencesByQuantile
//    (AlternativeID)` (the explicit-int ctor, NOT the dummy parameterless one -- transcribed
//    exactly as C# calls it), `AnalysisYears = {2030, 2049}`, `PeriodOfAnalysis = 50`.
//  - `(int id, std::vector<int> analysis_years, int period_of_analysis)`: `AlternativeID = id`,
//    `PeriodOfAnalysis = periodOfAnalysis`, `EqadResults = StudyAreaConsequencesByQuantile(id)`,
//    `IsNull = false`, `AnalysisYears = analysisYears`.
//  - internal `(StudyAreaConsequencesByQuantile, int id, std::vector<int> analysis_years, int
//    period_of_analysis, bool is_null)`: the "public for testing" ctor -- takes an
//    already-built `EqadResults` directly (this is the ctor this task's fixture uses).
//  In ALL THREE C# ctors, `BaseYearScenarioResults`/`FutureYearScenarioResults` are left
//  UNASSIGNED (C# auto-properties default to `null`). This port has no nullable sentinel for a
//  move-only `ScenarioResults` (unlike `ScenarioResults::get_accumulated_life_loss_fn_curve_data`'s
//  `std::optional<UncertainPairedData>` return, there is no established "nullable move-only field"
//  convention in this port, and the interface spec for this task declares these two fields as
//  plain `ScenarioResults`, not `optional`) -- so both are left default-constructed (an empty
//  `ScenarioResults()`, matching `ScenarioResults`'s own already-documented "empty stack ->
//  default `Empirical()`" no-throw convention for queries against an empty results list). Callers
//  (`Alternative::annualization_compute`, Task 9) MUST assign both via
//  `set_base_year_scenario_results`/`set_future_year_scenario_results` before any base/future-year
//  query is meaningful; querying an unset instance silently returns the same zero/empty answer a
//  freshly-constructed empty `ScenarioResults` would, it does not throw.
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//  - `ValidationErrorLogger` base + `AddRules()` (`AddSinglePropertyRule` registrations for "the
//    most likely future year must be at least 1 year greater than the base year" and "the period
//    of analysis must be >= the difference between the analysis years, inclusive"): no MVVM
//    validation/messaging infrastructure in this port (repo-wide MVVM severance). `AddRules()`
//    itself is a no-op in C# unless/until something calls `HasFatalErrors`/`Validate()` on the
//    object (never called by any method in this file), so dropping it changes no observable
//    behavior of any method ported below -- documented no-op, not reproduced as a callable check.
class AlternativeResults {
   public:
    using Empirical = hecfda::statistics::distributions::Empirical;

    // ported from: AlternativeResults.cs's implicit `utilities.IntegerGlobalConstants.
    // DEFAULT_MISSING_VALUE` default arg (-999), defined locally per the repo's established
    // convention (see study_area_consequences_by_quantile.hpp's own kDefaultMissingValue) since
    // the whole (mostly-severed) utilities file isn't otherwise ported.
    static constexpr int kDefaultMissingValue = -999;

    // ported from: AlternativeResults.cs `public AlternativeResults()`.
    AlternativeResults()
        : scenarios_are_identical_(false),
          alternative_id_(0),
          eqad_results_(StudyAreaConsequencesByQuantile(0)),
          base_year_scenario_results_(),
          future_year_scenario_results_(),
          analysis_years_{2030, 2049},
          period_of_analysis_(50),
          is_null_(true) {}

    // ported from: AlternativeResults.cs `public AlternativeResults(int id, List<int>
    // analysisYears, int periodOfAnalysis)`.
    AlternativeResults(int id, std::vector<int> analysis_years, int period_of_analysis)
        : scenarios_are_identical_(false),
          alternative_id_(id),
          eqad_results_(StudyAreaConsequencesByQuantile(id)),
          base_year_scenario_results_(),
          future_year_scenario_results_(),
          analysis_years_(std::move(analysis_years)),
          period_of_analysis_(period_of_analysis),
          is_null_(false) {}

    // ported from: AlternativeResults.cs `internal AlternativeResults(
    // StudyAreaConsequencesByQuantile studyAreaConsequencesByQuantile, int id, List<int>
    // analysisYears, int periodOfAnalysis, bool isNull)` -- "public for testing".
    AlternativeResults(StudyAreaConsequencesByQuantile eqad_results, int id, std::vector<int> analysis_years,
                        int period_of_analysis, bool is_null)
        : scenarios_are_identical_(false),
          alternative_id_(id),
          eqad_results_(std::move(eqad_results)),
          base_year_scenario_results_(),
          future_year_scenario_results_(),
          analysis_years_(std::move(analysis_years)),
          period_of_analysis_(period_of_analysis),
          is_null_(is_null) {}

    // Move-only (see class comment).
    AlternativeResults(AlternativeResults&&) = default;
    AlternativeResults& operator=(AlternativeResults&&) = default;
    AlternativeResults(const AlternativeResults&) = delete;
    AlternativeResults& operator=(const AlternativeResults&) = delete;

    // ported from: AlternativeResults.cs `internal bool ScenariosAreIdentical { get; set; } =
    // false`.
    bool scenarios_are_identical() const { return scenarios_are_identical_; }
    void set_scenarios_are_identical(bool value) { scenarios_are_identical_ = value; }

    // ported from: AlternativeResults.cs `public int AlternativeID { get; }`.
    int alternative_id() const { return alternative_id_; }

    // ported from: AlternativeResults.cs `public StudyAreaConsequencesByQuantile EqadResults {
    // get; internal set; }`.
    const StudyAreaConsequencesByQuantile& eqad_results() const { return eqad_results_; }
    void set_eqad_results(StudyAreaConsequencesByQuantile value) { eqad_results_ = std::move(value); }

    // ported from: AlternativeResults.cs `public List<int> AnalysisYears { get; }`.
    const std::vector<int>& analysis_years() const { return analysis_years_; }

    // ported from: AlternativeResults.cs `public int PeriodOfAnalysis { get; }`.
    int period_of_analysis() const { return period_of_analysis_; }

    // ported from: AlternativeResults.cs `public bool IsNull { get; }`.
    bool is_null() const { return is_null_; }

    // ported from: AlternativeResults.cs `internal ScenarioResults BaseYearScenarioResults {
    // get; set; }`. See class comment: caller-assigned, defaults to an empty ScenarioResults().
    const ScenarioResults& base_year_scenario_results() const { return base_year_scenario_results_; }
    void set_base_year_scenario_results(ScenarioResults value) {
        base_year_scenario_results_ = std::move(value);
    }

    // ported from: AlternativeResults.cs `internal ScenarioResults FutureYearScenarioResults {
    // get; set; }`. See class comment: caller-assigned, defaults to an empty ScenarioResults().
    const ScenarioResults& future_year_scenario_results() const { return future_year_scenario_results_; }
    void set_future_year_scenario_results(ScenarioResults value) {
        future_year_scenario_results_ = std::move(value);
    }

    // ported from: AlternativeResults.cs `public List<int> GetImpactAreaIDs(ConsequenceType
    // consequenceType = ConsequenceType.Damage)`. Distinct RegionID (first-seen order) over
    // eqad_results_'s ConsequenceResultList.
    std::vector<int> get_impact_area_ids(ConsequenceType consequence_type = ConsequenceType::Damage) const {
        std::vector<int> ids;
        for (const AggregatedConsequencesByQuantile& result : eqad_results_.consequence_result_list()) {
            if (result.consequence_type() != consequence_type) continue;
            if (std::find(ids.begin(), ids.end(), result.region_id()) == ids.end()) {
                ids.push_back(result.region_id());
            }
        }
        return ids;
    }

    // ported from: AlternativeResults.cs `public List<string> GetAssetCategories(ConsequenceType
    // consequenceType = ConsequenceType.Damage)`. Distinct AssetCategory (first-seen order).
    std::vector<std::string> get_asset_categories(ConsequenceType consequence_type = ConsequenceType::Damage) const {
        std::vector<std::string> categories;
        for (const AggregatedConsequencesByQuantile& result : eqad_results_.consequence_result_list()) {
            if (result.consequence_type() != consequence_type) continue;
            if (std::find(categories.begin(), categories.end(), result.asset_category()) == categories.end()) {
                categories.push_back(result.asset_category());
            }
        }
        return categories;
    }

    // ported from: AlternativeResults.cs `public List<string> GetDamageCategories(ConsequenceType
    // consequenceType = ConsequenceType.Damage)`. Distinct DamageCategory (first-seen order).
    std::vector<std::string> get_damage_categories(ConsequenceType consequence_type = ConsequenceType::Damage) const {
        std::vector<std::string> categories;
        for (const AggregatedConsequencesByQuantile& result : eqad_results_.consequence_result_list()) {
            if (result.consequence_type() != consequence_type) continue;
            if (std::find(categories.begin(), categories.end(), result.damage_category()) == categories.end()) {
                categories.push_back(result.damage_category());
            }
        }
        return categories;
    }

    // ported from: AlternativeResults.cs `public List<RiskType> GetRiskTypes()`. Distinct
    // RiskType (first-seen order), NO ConsequenceType filter (matches C#'s unfiltered
    // `.Select(...)`).
    std::vector<RiskType> get_risk_types() const {
        std::vector<RiskType> types;
        for (const AggregatedConsequencesByQuantile& result : eqad_results_.consequence_result_list()) {
            if (std::find(types.begin(), types.end(), result.risk_type()) == types.end()) {
                types.push_back(result.risk_type());
            }
        }
        return types;
    }

    // ported from: AlternativeResults.cs `public double SampleMeanEqad(int impactAreaID = ...,
    // string damageCategory = null, string assetCategory = null, ConsequenceType consequenceType
    // = ConsequenceType.Damage, RiskType riskType = RiskType.Total)`. THE delegation pattern (see
    // class comment): identical scenarios -> the base-year scenario's own expected-annual-
    // consequences figure; otherwise -> the quantile-walked EqAD results. NON-const: mirrors
    // ScenarioResults::sample_mean_expected_annual_consequences's own non-const-ness (see that
    // method's comment) on the identical-scenarios branch.
    double sample_mean_eqad(int impact_area_id = kDefaultMissingValue,
                             const std::optional<std::string>& damage_category = std::nullopt,
                             const std::optional<std::string>& asset_category = std::nullopt,
                             ConsequenceType consequence_type = ConsequenceType::Damage,
                             RiskType risk_type = RiskType::Total) {
        if (scenarios_are_identical_) {
            return base_year_scenario_results_.sample_mean_expected_annual_consequences(
                impact_area_id, damage_category, asset_category, consequence_type, risk_type);
        }
        return eqad_results_.sample_mean_damage(damage_category, asset_category, impact_area_id, consequence_type,
                                                  risk_type);
    }

    // ported from: AlternativeResults.cs `public double SampleMeanBaseYearEAD(...)`. Never
    // branches on scenarios_are_identical_ -- always the base-year scenario's own figure.
    // NON-const: see sample_mean_eqad's comment.
    double sample_mean_base_year_ead(int impact_area_id = kDefaultMissingValue,
                                      const std::optional<std::string>& damage_category = std::nullopt,
                                      const std::optional<std::string>& asset_category = std::nullopt,
                                      ConsequenceType consequence_type = ConsequenceType::Damage,
                                      RiskType risk_type = RiskType::Total) {
        return base_year_scenario_results_.sample_mean_expected_annual_consequences(
            impact_area_id, damage_category, asset_category, consequence_type, risk_type);
    }

    // ported from: AlternativeResults.cs `public double SampleMeanFutureYearEAD(...)`. Never
    // branches -- always the future-year scenario's own figure. NON-const: see sample_mean_eqad's
    // comment.
    double sample_mean_future_year_ead(int impact_area_id = kDefaultMissingValue,
                                        const std::optional<std::string>& damage_category = std::nullopt,
                                        const std::optional<std::string>& asset_category = std::nullopt,
                                        ConsequenceType consequence_type = ConsequenceType::Damage,
                                        RiskType risk_type = RiskType::Total) {
        return future_year_scenario_results_.sample_mean_expected_annual_consequences(
            impact_area_id, damage_category, asset_category, consequence_type, risk_type);
    }

    // ported from: AlternativeResults.cs `public double EqadExceededWithProbabilityQ(double
    // exceedanceProbability, ...)`. Same identical-vs-eqad branch as sample_mean_eqad.
    double eqad_exceeded_with_probability_q(double exceedance_probability,
                                             int impact_area_id = kDefaultMissingValue,
                                             const std::optional<std::string>& damage_category = std::nullopt,
                                             const std::optional<std::string>& asset_category = std::nullopt,
                                             ConsequenceType consequence_type = ConsequenceType::Damage,
                                             RiskType risk_type = RiskType::Total) const {
        if (scenarios_are_identical_) {
            return base_year_scenario_results_.consequences_exceeded_with_probability_q(
                exceedance_probability, impact_area_id, damage_category, asset_category, consequence_type,
                risk_type);
        }
        return eqad_results_.consequence_exceeded_with_probability_q(exceedance_probability, damage_category,
                                                                        asset_category, impact_area_id,
                                                                        consequence_type, risk_type);
    }

    // ported from: AlternativeResults.cs `public double BaseYearEADDamageExceededWithProbabilityQ(
    // ...)`. Never branches.
    double base_year_ead_exceeded_with_probability_q(
        double exceedance_probability, int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage, RiskType risk_type = RiskType::Total) const {
        return base_year_scenario_results_.consequences_exceeded_with_probability_q(
            exceedance_probability, impact_area_id, damage_category, asset_category, consequence_type, risk_type);
    }

    // ported from: AlternativeResults.cs `public double
    // FutureYearEADDamageExceededWithProbabilityQ(...)`. Never branches.
    double future_year_ead_exceeded_with_probability_q(
        double exceedance_probability, int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage, RiskType risk_type = RiskType::Total) const {
        return future_year_scenario_results_.consequences_exceeded_with_probability_q(
            exceedance_probability, impact_area_id, damage_category, asset_category, consequence_type, risk_type);
    }

    // ported from: AlternativeResults.cs `public Empirical GetEqadDistribution(...)`. Same
    // identical-vs-eqad branch as sample_mean_eqad.
    Empirical get_eqad_distribution(int impact_area_id = kDefaultMissingValue,
                                     const std::optional<std::string>& damage_category = std::nullopt,
                                     const std::optional<std::string>& asset_category = std::nullopt,
                                     ConsequenceType consequence_type = ConsequenceType::Damage,
                                     RiskType risk_type = RiskType::Total) const {
        if (scenarios_are_identical_) {
            return base_year_scenario_results_.get_consequences_distribution(
                impact_area_id, damage_category, asset_category, consequence_type, risk_type);
        }
        return eqad_results_.get_aggregate_empirical_distribution(damage_category, asset_category, impact_area_id,
                                                                     consequence_type, risk_type);
    }

    // ported from: AlternativeResults.cs `public Empirical GetBaseYearEADDistribution(...)`.
    // Never branches.
    Empirical get_base_year_ead_distribution(int impact_area_id = kDefaultMissingValue,
                                              const std::optional<std::string>& damage_category = std::nullopt,
                                              const std::optional<std::string>& asset_category = std::nullopt,
                                              ConsequenceType consequence_type = ConsequenceType::Damage,
                                              RiskType risk_type = RiskType::Total) const {
        return base_year_scenario_results_.get_consequences_distribution(impact_area_id, damage_category,
                                                                            asset_category, consequence_type,
                                                                            risk_type);
    }

    // ported from: AlternativeResults.cs `public Empirical GetFutureYearEADDistribution(...)`.
    // Never branches.
    Empirical get_future_year_ead_distribution(int impact_area_id = kDefaultMissingValue,
                                                const std::optional<std::string>& damage_category = std::nullopt,
                                                const std::optional<std::string>& asset_category = std::nullopt,
                                                ConsequenceType consequence_type = ConsequenceType::Damage,
                                                RiskType risk_type = RiskType::Total) const {
        return future_year_scenario_results_.get_consequences_distribution(impact_area_id, damage_category,
                                                                              asset_category, consequence_type,
                                                                              risk_type);
    }

    // ported from: AlternativeResults.cs `internal void AddConsequenceResults(
    // AggregatedConsequencesByQuantile consequenceResultToAdd)` (AlternativeResults.cs:231-238).
    // Dedupes via eqad_results_.get_consequence_result's dummy-on-miss return contract, called with
    // the (damageCategory, assetCategory, RegionID, ConsequenceType) 4-arg form so riskType defaults
    // to RiskType::Total -- matching upstream's 4-arg call exactly (upstream does NOT pass
    // consequenceResultToAdd's own RiskType here). On a miss, upstream appends unconditionally via
    // `EqadResults.ConsequenceResultList.Add(consequenceResultToAdd)`.
    //
    // Calls the raw unconditional-append mutator (add_consequence_result_object_unchecked) rather
    // than add_existing_consequence_result_object, to mirror upstream's direct list `.Add(...)`
    // literally. NOTE for future readers: routing through add_existing_consequence_result_object
    // here would NOT actually change observable behavior -- filter_by_categories' RiskType
    // predicate is `risk_type == RiskType::Total || risk_type == candidate.risk_type()` (see
    // consequence_extensions.hpp), i.e. RiskType::Total is a WILDCARD that matches a candidate of
    // ANY risk type, not an exact-Total match. The Total-defaulted 4-arg check above is therefore
    // STRICTLY BROADER than add_existing_consequence_result_object's own 5-arg check (which passes
    // consequence_result_to_add's own, possibly-non-Total, risk_type): whenever the 4-arg check
    // finds zero matches for (damageCategory, assetCategory, RegionID, ConsequenceType) across ALL
    // risk types, the narrower 5-arg check (a strict subset of that search) necessarily also finds
    // zero. So the two forms are provably behaviorally equivalent here -- this form is preferred
    // purely because it mirrors upstream's actual statement (`ConsequenceResultList.Add(...)`,
    // not a second dedup call) rather than for any behavior difference.
    void add_consequence_results(AggregatedConsequencesByQuantile consequence_result_to_add) {
        AggregatedConsequencesByQuantile existing = eqad_results_.get_consequence_result(
            consequence_result_to_add.damage_category(), consequence_result_to_add.asset_category(),
            consequence_result_to_add.region_id(), consequence_result_to_add.consequence_type());
        if (existing.is_null()) {
            eqad_results_.add_consequence_result_object_unchecked(std::move(consequence_result_to_add));
        }
    }

   private:
    bool scenarios_are_identical_;
    int alternative_id_;
    StudyAreaConsequencesByQuantile eqad_results_;
    ScenarioResults base_year_scenario_results_;
    ScenarioResults future_year_scenario_results_;
    std::vector<int> analysis_years_;
    int period_of_analysis_;
    bool is_null_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_ALTERNATIVE_RESULTS_HPP
