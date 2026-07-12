// ported from: HEC.FDA.Model/metrics/AlternativeComparisonReportResults.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_ALTERNATIVE_COMPARISON_REPORT_RESULTS_HPP
#define HECFDA_MODEL_METRICS_ALTERNATIVE_COMPARISON_REPORT_RESULTS_HPP
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/alternative_results.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/metrics/study_area_consequences_by_quantile.hpp"
#include "hecfda/statistics/distributions/empirical.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: AlternativeComparisonReportResults.cs `public class AlternativeComparisonReportResults
// : ValidationErrorLogger` -- the container `AlternativeComparisonReport.compute_alternative_
// comparison_report` (Task 10, not yet ported) returns: the with/without-project `AlternativeResults`
// (Task 6) plus three lists of REDUCED (benefit) `StudyAreaConsequencesByQuantile` (Task 3) results
// -- EqAD-reduced, base-year-EAD-reduced, and future-year-EAD-reduced -- and delegates every query
// either to one of those three lists (searched by AlternativeID) or straight through to the
// with/without `AlternativeResults`.
//
// Move-only: holds `with_project_alternative_results_` (a `std::vector<AlternativeResults>`) and
// `without_project_alternative_results_` (a single `AlternativeResults`), and `AlternativeResults`
// is itself move-only (see that header's class comment) -- so this class is necessarily move-only
// too, matching the "fresh construction per use" convention every Phase 6 metrics container in this
// file tree already follows. Explicit move/copy declarations added.
//
// Ctor transcribed from the C# source's single internal ctor: `_WithProjectAlternativeResults = [..
// withProjectAlternativeResults]` (a `List<AlternativeResults>` spread-copy of the
// `IEnumerable<AlternativeResults>` argument) is ported directly as a `std::vector<AlternativeResults>`
// by-value parameter (a move-only element type forbids any generic-iterable-copy-into-list
// translation anyway), moved into the member.
//
// `years()`: ported from `public List<int> Years => _WithoutProjectAlternativeResults.AnalysisYears;`
// -- returns the same `const std::vector<int>&` that `AlternativeResults::analysis_years()` already
// exposes (no copy), matching that method's own reference-returning convention.
//
// THE ENUMERATORS (`get_impact_area_ids`/`get_asset_categories`/`get_damage_categories`/
// `get_risk_types`): all four walk `_EqadReducedResultsList` ONLY (never the base/future-year
// lists) -- upstream's own comment explains why ("These methods now assume that the same impact
// areas are in all three results lists ... We could cycle through each of the three lists, but that
// seems unnecessary"). Distinct values are collected in first-seen order via a nested nested loop +
// linear `std::find`, mirroring `AlternativeResults`'s own `get_impact_area_ids`/etc. (LINQ
// `SelectMany(...).Distinct()` ported the same way there). `get_risk_types`, like its three
// siblings above, DOES filter by `ConsequenceType`
// (`.Where(r => r.ConsequenceType == consequenceType)` in C#) -- do not confuse this with the
// different, genuinely-unfiltered parameterless `AlternativeResults::get_risk_types()` on the
// unrelated `AlternativeResults` class.
//
// `has_reduced_results_of_type`/`get_reduced_alternative_ids`/`get_reduced_impact_area_ids`: all
// three walk `_BaseYearEADReducedResultsList` THEN `_FutureYearEADReducedResultsList` (C#'s
// `.Concat(...)`, sequential, not interleaved) -- base-year list first preserves upstream's
// first-seen distinct order exactly.
//
// THE LIST-SELECTION LOGIC (`get_consequences_reduced_results_for_given_alternative`, the reason
// every reduced-results query funnels through one place): three logical cases -- EqAD reduced
// (`!get_ead_results` -> `_EqadReducedResultsList`), base-year EAD reduced (`get_ead_results &&
// get_base_year_results` -> `_BaseYearEADReducedResultsList`), or future-year EAD reduced
// (`get_ead_results && !get_base_year_results` -> `_FutureYearEADReducedResultsList`) -- the fourth
// combination (`!get_ead_results && get_base_year_results`, i.e. "give me base-year results but NOT
// EAD results") is coded to throw (`System.ArgumentException` -> `std::invalid_argument`, matching
// `CategoriedUncertainPairedData::add_curve_realization`'s established ArgumentException mapping)
// -- but the preceding `!get_ead_results` branch already catches both values of
// get_base_year_results, so this trailing throw is UNREACHABLE dead code in both the C# source and
// this port; transcribed faithfully rather than removed (see "Faithful upstream bugs" in
// .claude/CLAUDE.md). On a miss (no entry in the selected list has a matching AlternativeID), C#
// returns a dummy `new StudyAreaConsequencesByQuantile()` (`IsNull == true`) after a
// `ReportMessage(...)` side-channel notification -- SEVERED MVVM (see below); the dummy-on-miss
// RETURN contract is kept verbatim (this class is copyable, so returning a fresh
// default-constructed instance by value on a miss costs nothing and matches upstream exactly --
// this method never throws on a miss; the illogical-combination throw above is unreachable).
//
// `get_alternative_results` (private, `GetAlternativeResults` in C#) searches
// `_WithProjectAlternativeResults` by AlternativeID. Unlike the StudyAreaConsequencesByQuantile
// case above, `AlternativeResults` is MOVE-ONLY, so a C#-style "fabricate a `new` dummy and return
// it by value" miss fallback is not representable the same way in C++ (there is no cheap
// move-only-safe way to manufacture a detached dummy AND return existing list elements by reference
// without a copy). This port instead follows the established `ScenarioResults::get_results`
// precedent (see that header's class comment) for exactly this situation: returns
// `AlternativeResults&` on a hit, throws `std::runtime_error` on a miss. A real, if narrow, behavior
// change from C# (which would silently hand back an empty dummy `AlternativeResults`) -- documented,
// not worked around, same severance choice as `ScenarioResults::get_results`.
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//  - `ValidationErrorLogger` base: no MVVM validation/messaging infrastructure in this port
//    (repo-wide MVVM severance).
//  - `GetConsequencesReducedResultsForGivenAlternative`/`GetAlternativeResults`'s on-miss MVVM
//    `ReportMessage(this, new MessageEventArgs(new Message(...)))` calls: side-channel event
//    notifications (no-ops with no MVVM bus in this port), omitted -- documented no-op, not
//    reproduced, same severance choice as `StudyAreaConsequencesByQuantile::get_consequence_result`.
//  - `internal void AddAlternativeResults(StudyAreaConsequencesByQuantile, bool isEADResults = false,
//    bool isBaseYearResults = false)`: NOT part of this task's interface (Task 7 ports the query
//    surface `AlternativeComparisonReport.compute_alternative_comparison_report`, Task 10, delegates
//    to; the three reduced-results lists are populated via the ctor, not incrementally). Note for
//    whoever ports Task 10: `AddAlternativeResults` as written in C# looks like a bug regardless --
//    it calls `GetConsequencesReducedResultsForGivenAlternative(id, isEADResults, isBaseYearResults)`
//    to check for an existing entry (respecting the 3-way list selection), but on a miss
//    unconditionally appends to `_EqadReducedResultsList` specifically, never to
//    `_BaseYearEADReducedResultsList`/`_FutureYearEADReducedResultsList` even when
//    `isEADResults`/`isBaseYearResults` selected one of those. Deferred rather than transcribed
//    speculatively here; revisit against the real call sites when Task 10 lands.
class AlternativeComparisonReportResults {
   public:
    using Empirical = hecfda::statistics::distributions::Empirical;

    // ported from: AlternativeComparisonReportResults.cs's implicit
    // `utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE` default arg (-999), defined locally
    // per the repo's established convention (see study_area_consequences_by_quantile.hpp's own
    // kDefaultMissingValue) since the whole (mostly-severed) utilities file isn't otherwise ported.
    static constexpr int kDefaultMissingValue = -999;

    // ported from: AlternativeComparisonReportResults.cs `internal AlternativeComparisonReportResults(
    // IEnumerable<AlternativeResults> withProjectAlternativeResults, AlternativeResults
    // withoutProjectAlternativeResults, List<StudyAreaConsequencesByQuantile> eqadResults,
    // List<StudyAreaConsequencesByQuantile> baseYearEADResults, List<StudyAreaConsequencesByQuantile>
    // futureYearEADResults)`.
    AlternativeComparisonReportResults(std::vector<AlternativeResults> with_project_alternative_results,
                                        AlternativeResults without_project_alternative_results,
                                        std::vector<StudyAreaConsequencesByQuantile> eqad_reduced_results_list,
                                        std::vector<StudyAreaConsequencesByQuantile> base_year_ead_reduced_results_list,
                                        std::vector<StudyAreaConsequencesByQuantile> future_year_ead_reduced_results_list)
        : eqad_reduced_results_list_(std::move(eqad_reduced_results_list)),
          base_year_ead_reduced_results_list_(std::move(base_year_ead_reduced_results_list)),
          future_year_ead_reduced_results_list_(std::move(future_year_ead_reduced_results_list)),
          with_project_alternative_results_(std::move(with_project_alternative_results)),
          without_project_alternative_results_(std::move(without_project_alternative_results)) {}

    // Move-only (see class comment).
    AlternativeComparisonReportResults(AlternativeComparisonReportResults&&) = default;
    AlternativeComparisonReportResults& operator=(AlternativeComparisonReportResults&&) = default;
    AlternativeComparisonReportResults(const AlternativeComparisonReportResults&) = delete;
    AlternativeComparisonReportResults& operator=(const AlternativeComparisonReportResults&) = delete;

    // ported from: AlternativeComparisonReportResults.cs `public List<int> Years =>
    // _WithoutProjectAlternativeResults.AnalysisYears;`.
    const std::vector<int>& years() const { return without_project_alternative_results_.analysis_years(); }

    // ported from: AlternativeComparisonReportResults.cs `public List<int> GetImpactAreaIDs(
    // ConsequenceType consequenceType = ConsequenceType.Damage)`. Walks _EqadReducedResultsList
    // only (see class comment). Distinct RegionID (first-seen order).
    std::vector<int> get_impact_area_ids(ConsequenceType consequence_type = ConsequenceType::Damage) const {
        std::vector<int> ids;
        for (const StudyAreaConsequencesByQuantile& x : eqad_reduced_results_list_) {
            for (const AggregatedConsequencesByQuantile& r : x.consequence_result_list()) {
                if (r.consequence_type() != consequence_type) continue;
                if (std::find(ids.begin(), ids.end(), r.region_id()) == ids.end()) {
                    ids.push_back(r.region_id());
                }
            }
        }
        return ids;
    }

    // ported from: AlternativeComparisonReportResults.cs `public List<string> GetAssetCategories(
    // ConsequenceType consequenceType = ConsequenceType.Damage)`. Distinct AssetCategory
    // (first-seen order).
    std::vector<std::string> get_asset_categories(ConsequenceType consequence_type = ConsequenceType::Damage) const {
        std::vector<std::string> categories;
        for (const StudyAreaConsequencesByQuantile& x : eqad_reduced_results_list_) {
            for (const AggregatedConsequencesByQuantile& r : x.consequence_result_list()) {
                if (r.consequence_type() != consequence_type) continue;
                if (std::find(categories.begin(), categories.end(), r.asset_category()) == categories.end()) {
                    categories.push_back(r.asset_category());
                }
            }
        }
        return categories;
    }

    // ported from: AlternativeComparisonReportResults.cs `public List<string> GetDamageCategories(
    // ConsequenceType consequenceType = ConsequenceType.Damage)`. Distinct DamageCategory
    // (first-seen order).
    std::vector<std::string> get_damage_categories(ConsequenceType consequence_type = ConsequenceType::Damage) const {
        std::vector<std::string> categories;
        for (const StudyAreaConsequencesByQuantile& x : eqad_reduced_results_list_) {
            for (const AggregatedConsequencesByQuantile& r : x.consequence_result_list()) {
                if (r.consequence_type() != consequence_type) continue;
                if (std::find(categories.begin(), categories.end(), r.damage_category()) == categories.end()) {
                    categories.push_back(r.damage_category());
                }
            }
        }
        return categories;
    }

    // ported from: AlternativeComparisonReportResults.cs `public List<RiskType> GetRiskTypes(
    // ConsequenceType consequenceType = ConsequenceType.Damage)`. Distinct RiskType (first-seen
    // order), filtered by ConsequenceType (`.Where(r => r.ConsequenceType == consequenceType)`),
    // same filter form as get_impact_area_ids/get_asset_categories/get_damage_categories above.
    std::vector<RiskType> get_risk_types(ConsequenceType consequence_type = ConsequenceType::Damage) const {
        std::vector<RiskType> types;
        for (const StudyAreaConsequencesByQuantile& x : eqad_reduced_results_list_) {
            for (const AggregatedConsequencesByQuantile& r : x.consequence_result_list()) {
                if (r.consequence_type() != consequence_type) continue;
                if (std::find(types.begin(), types.end(), r.risk_type()) == types.end()) {
                    types.push_back(r.risk_type());
                }
            }
        }
        return types;
    }

    // ported from: AlternativeComparisonReportResults.cs `public bool HasReducedResultsOfType(
    // ConsequenceType consequenceType)`. Walks _BaseYearEADReducedResultsList then
    // _FutureYearEADReducedResultsList (C#'s `.Concat(...)`, sequential).
    bool has_reduced_results_of_type(ConsequenceType consequence_type) const {
        for (const StudyAreaConsequencesByQuantile& x : base_year_ead_reduced_results_list_) {
            for (const AggregatedConsequencesByQuantile& r : x.consequence_result_list()) {
                if (r.consequence_type() == consequence_type) return true;
            }
        }
        for (const StudyAreaConsequencesByQuantile& x : future_year_ead_reduced_results_list_) {
            for (const AggregatedConsequencesByQuantile& r : x.consequence_result_list()) {
                if (r.consequence_type() == consequence_type) return true;
            }
        }
        return false;
    }

    // ported from: AlternativeComparisonReportResults.cs `public List<int> GetReducedAlternativeIDs(
    // ConsequenceType consequenceType)`. Base-year list then future-year list (see class comment);
    // distinct AlternativeID (first-seen order) among entries that have at least one matching
    // ConsequenceType result.
    std::vector<int> get_reduced_alternative_ids(ConsequenceType consequence_type) const {
        std::vector<int> ids;
        auto scan = [&](const std::vector<StudyAreaConsequencesByQuantile>& list) {
            for (const StudyAreaConsequencesByQuantile& x : list) {
                bool has_match = false;
                for (const AggregatedConsequencesByQuantile& r : x.consequence_result_list()) {
                    if (r.consequence_type() == consequence_type) {
                        has_match = true;
                        break;
                    }
                }
                if (!has_match) continue;
                if (std::find(ids.begin(), ids.end(), x.alternative_id()) == ids.end()) {
                    ids.push_back(x.alternative_id());
                }
            }
        };
        scan(base_year_ead_reduced_results_list_);
        scan(future_year_ead_reduced_results_list_);
        return ids;
    }

    // ported from: AlternativeComparisonReportResults.cs `public List<int> GetReducedImpactAreaIDs(
    // ConsequenceType consequenceType)`. Base-year list then future-year list (see class comment);
    // distinct RegionID (first-seen order) among matching-ConsequenceType results.
    std::vector<int> get_reduced_impact_area_ids(ConsequenceType consequence_type) const {
        std::vector<int> ids;
        auto scan = [&](const std::vector<StudyAreaConsequencesByQuantile>& list) {
            for (const StudyAreaConsequencesByQuantile& x : list) {
                for (const AggregatedConsequencesByQuantile& r : x.consequence_result_list()) {
                    if (r.consequence_type() != consequence_type) continue;
                    if (std::find(ids.begin(), ids.end(), r.region_id()) == ids.end()) {
                        ids.push_back(r.region_id());
                    }
                }
            }
        };
        scan(base_year_ead_reduced_results_list_);
        scan(future_year_ead_reduced_results_list_);
        return ids;
    }

    // ported from: AlternativeComparisonReportResults.cs `public double SampleMeanEqadReduced(int
    // alternativeID, ...)`. -> get_consequences_reduced_results_for_given_alternative(alternativeID)
    // .sample_mean_damage(...).
    double sample_mean_eqad_reduced(int alternative_id, int impact_area_id = kDefaultMissingValue,
                                     const std::optional<std::string>& damage_category = std::nullopt,
                                     const std::optional<std::string>& asset_category = std::nullopt,
                                     ConsequenceType consequence_type = ConsequenceType::Damage,
                                     RiskType risk_type = RiskType::Total) const {
        return get_consequences_reduced_results_for_given_alternative(alternative_id)
            .sample_mean_damage(damage_category, asset_category, impact_area_id, consequence_type, risk_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public double
    // SampleMeanBaseYearEADReduced(int alternativeID, ...)`. -> get_consequences_reduced_results_
    // for_given_alternative(alternativeID, /*getEADResults=*/true, /*getBaseYearResults=*/true)
    // .sample_mean_damage(...).
    double sample_mean_base_year_ead_reduced(int alternative_id, int impact_area_id = kDefaultMissingValue,
                                              const std::optional<std::string>& damage_category = std::nullopt,
                                              const std::optional<std::string>& asset_category = std::nullopt,
                                              ConsequenceType consequence_type = ConsequenceType::Damage,
                                              RiskType risk_type = RiskType::Total) const {
        StudyAreaConsequencesByQuantile results =
            get_consequences_reduced_results_for_given_alternative(alternative_id, true, true);
        return results.sample_mean_damage(damage_category, asset_category, impact_area_id, consequence_type,
                                           risk_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public double
    // SampleMeanFutureYearEADReduced(int alternativeID, ...)`. -> get_consequences_reduced_results_
    // for_given_alternative(alternativeID, /*getEADResults=*/true).sample_mean_damage(...).
    double sample_mean_future_year_ead_reduced(int alternative_id, int impact_area_id = kDefaultMissingValue,
                                                const std::optional<std::string>& damage_category = std::nullopt,
                                                const std::optional<std::string>& asset_category = std::nullopt,
                                                ConsequenceType consequence_type = ConsequenceType::Damage,
                                                RiskType risk_type = RiskType::Total) const {
        return get_consequences_reduced_results_for_given_alternative(alternative_id, true)
            .sample_mean_damage(damage_category, asset_category, impact_area_id, consequence_type, risk_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public double
    // SampleMeanWithoutProjectBaseYearEAD(...)`. -> _WithoutProjectAlternativeResults.
    // SampleMeanBaseYearEAD(...). NON-const: mirrors AlternativeResults::sample_mean_base_year_ead's
    // own non-const-ness.
    double sample_mean_without_project_base_year_ead(
        int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage) {
        return without_project_alternative_results_.sample_mean_base_year_ead(impact_area_id, damage_category,
                                                                                asset_category, consequence_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public double
    // SampleMeanWithoutProjectFutureYearEAD(...)`. -> _WithoutProjectAlternativeResults.
    // SampleMeanFutureYearEAD(...). NON-const: see sample_mean_without_project_base_year_ead.
    double sample_mean_without_project_future_year_ead(
        int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage) {
        return without_project_alternative_results_.sample_mean_future_year_ead(impact_area_id, damage_category,
                                                                                  asset_category, consequence_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public double
    // SampleMeanWithProjectBaseYearEAD(int alternativeID, ...)`. -> GetAlternativeResults(
    // alternativeID).SampleMeanBaseYearEAD(...). NON-const: see get_alternative_results's own
    // non-const-ness (class comment) and AlternativeResults::sample_mean_base_year_ead's.
    double sample_mean_with_project_base_year_ead(
        int alternative_id, int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage) {
        AlternativeResults& alternative_results = get_alternative_results(alternative_id);
        return alternative_results.sample_mean_base_year_ead(impact_area_id, damage_category, asset_category,
                                                               consequence_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public double
    // SampleMeanWithProjectFutureYearEAD(int alternativeID, ...)`. -> GetAlternativeResults(
    // alternativeID).SampleMeanFutureYearEAD(...). NON-const: see
    // sample_mean_with_project_base_year_ead.
    double sample_mean_with_project_future_year_ead(
        int alternative_id, int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage) {
        AlternativeResults& alternative_results = get_alternative_results(alternative_id);
        return alternative_results.sample_mean_future_year_ead(impact_area_id, damage_category, asset_category,
                                                                 consequence_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public double SampleMeanWithProjectEqad(
    // int alternativeID, ...)`. -> GetAlternativeResults(alternativeID).SampleMeanEqad(...).
    // NON-const: see sample_mean_with_project_base_year_ead.
    double sample_mean_with_project_eqad(int alternative_id, int impact_area_id = kDefaultMissingValue,
                                          const std::optional<std::string>& damage_category = std::nullopt,
                                          const std::optional<std::string>& asset_category = std::nullopt) {
        AlternativeResults& alternative_results = get_alternative_results(alternative_id);
        return alternative_results.sample_mean_eqad(impact_area_id, damage_category, asset_category);
    }

    // ported from: AlternativeComparisonReportResults.cs `public double
    // SampleMeanWithoutProjectEqad(...)`. -> _WithoutProjectAlternativeResults.SampleMeanEqad(...).
    // NON-const: mirrors AlternativeResults::sample_mean_eqad's own non-const-ness.
    double sample_mean_without_project_eqad(int impact_area_id = kDefaultMissingValue,
                                             const std::optional<std::string>& damage_category = std::nullopt,
                                             const std::optional<std::string>& asset_category = std::nullopt) {
        return without_project_alternative_results_.sample_mean_eqad(impact_area_id, damage_category,
                                                                       asset_category);
    }

    // ported from: AlternativeComparisonReportResults.cs `public double
    // EqadReducedExceededWithProbabilityQ(double exceedanceProbability, int alternativeID, ...)`.
    // NOTE: no RiskType parameter at this level (unlike the base/future-year siblings below) --
    // matches C#'s signature exactly; the underlying ConsequenceExceededWithProbabilityQ call uses
    // its own RiskType::Total default.
    double eqad_reduced_exceeded_with_probability_q(
        double exceedance_probability, int alternative_id, int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage) const {
        return get_consequences_reduced_results_for_given_alternative(alternative_id)
            .consequence_exceeded_with_probability_q(exceedance_probability, damage_category, asset_category,
                                                       impact_area_id, consequence_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public double
    // BaseYearEADReducedExceededWithProbabilityQ(double exceedanceProbability, int alternativeID,
    // ...)`.
    double base_year_ead_reduced_exceeded_with_probability_q(
        double exceedance_probability, int alternative_id, int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage, RiskType risk_type = RiskType::Total) const {
        return get_consequences_reduced_results_for_given_alternative(alternative_id, true, true)
            .consequence_exceeded_with_probability_q(exceedance_probability, damage_category, asset_category,
                                                       impact_area_id, consequence_type, risk_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public double
    // FutureYearEADReducedExceededWithProbabilityQ(double exceedanceProbability, int alternativeID,
    // ...)`.
    double future_year_ead_reduced_exceeded_with_probability_q(
        double exceedance_probability, int alternative_id, int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage, RiskType risk_type = RiskType::Total) const {
        return get_consequences_reduced_results_for_given_alternative(alternative_id, true)
            .consequence_exceeded_with_probability_q(exceedance_probability, damage_category, asset_category,
                                                       impact_area_id, consequence_type, risk_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public Empirical
    // GetEqadReducedResultsHistogram(int alternativeID, ...)`. NOTE: no RiskType parameter at this
    // level -- matches C#'s signature exactly; GetAggregateEmpiricalDistribution uses its own
    // RiskType::Total default.
    Empirical get_eqad_reduced_results_histogram(int alternative_id, int impact_area_id = kDefaultMissingValue,
                                                  const std::optional<std::string>& damage_category = std::nullopt,
                                                  const std::optional<std::string>& asset_category = std::nullopt,
                                                  ConsequenceType consequence_type = ConsequenceType::Damage) const {
        StudyAreaConsequencesByQuantile eqad_results = get_consequences_reduced_results_for_given_alternative(alternative_id);
        return eqad_results.get_aggregate_empirical_distribution(damage_category, asset_category, impact_area_id,
                                                                    consequence_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public Empirical
    // GetBaseYearEADReducedResultsHistogram(int alternativeID, ...)`.
    Empirical get_base_year_ead_reduced_results_histogram(
        int alternative_id, int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage, RiskType risk_type = RiskType::Total) const {
        StudyAreaConsequencesByQuantile ead_results =
            get_consequences_reduced_results_for_given_alternative(alternative_id, true, true);
        return ead_results.get_aggregate_empirical_distribution(damage_category, asset_category, impact_area_id,
                                                                   consequence_type, risk_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `public Empirical
    // GetFutureYearEADReducedResultsHistogram(int alternativeID, ...)`.
    Empirical get_future_year_ead_reduced_results_histogram(
        int alternative_id, int impact_area_id = kDefaultMissingValue,
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt,
        ConsequenceType consequence_type = ConsequenceType::Damage, RiskType risk_type = RiskType::Total) const {
        StudyAreaConsequencesByQuantile ead_results =
            get_consequences_reduced_results_for_given_alternative(alternative_id, true);
        return ead_results.get_aggregate_empirical_distribution(damage_category, asset_category, impact_area_id,
                                                                   consequence_type, risk_type);
    }

    // ported from: AlternativeComparisonReportResults.cs `internal StudyAreaConsequencesByQuantile
    // GetConsequencesReducedResultsForGivenAlternative(int alternativeID, bool getEADResults = false,
    // bool getBaseYearResults = false)`. See class comment (THE LIST-SELECTION LOGIC) for the
    // 3-case/throw-on-illogical-combo semantics.
    StudyAreaConsequencesByQuantile get_consequences_reduced_results_for_given_alternative(
        int alternative_id, bool get_ead_results = false, bool get_base_year_results = false) const {
        const std::vector<StudyAreaConsequencesByQuantile>* list_to_search;
        if (!get_ead_results) {
            list_to_search = &eqad_reduced_results_list_;
        } else if (get_ead_results && get_base_year_results) {
            list_to_search = &base_year_ead_reduced_results_list_;
        } else if (get_ead_results && !get_base_year_results) {
            list_to_search = &future_year_ead_reduced_results_list_;
        } else {
            throw std::invalid_argument("An illogical combination of arguments was provided");
        }
        for (const StudyAreaConsequencesByQuantile& consequence_dist_results : *list_to_search) {
            if (consequence_dist_results.alternative_id() == alternative_id) {
                return consequence_dist_results;
            }
        }
        // SEVERED MVVM: C# reports a message via ReportMessage(this, ...) here before returning the
        // dummy object below -- omitted (documented no-op, see class comment).
        return StudyAreaConsequencesByQuantile();
    }

   private:
    // ported from: AlternativeComparisonReportResults.cs `private AlternativeResults
    // GetAlternativeResults(int alternativeID)`. See class comment for why this throws on a miss
    // instead of C#'s dummy-fallback (AlternativeResults is move-only).
    AlternativeResults& get_alternative_results(int alternative_id) {
        for (AlternativeResults& alternative_results : with_project_alternative_results_) {
            if (alternative_results.alternative_id() == alternative_id) {
                return alternative_results;
            }
        }
        throw std::runtime_error(
            "AlternativeComparisonReportResults::get_alternative_results: no AlternativeResults found for "
            "alternative_id=" +
            std::to_string(alternative_id));
    }

    std::vector<StudyAreaConsequencesByQuantile> eqad_reduced_results_list_;
    std::vector<StudyAreaConsequencesByQuantile> base_year_ead_reduced_results_list_;
    std::vector<StudyAreaConsequencesByQuantile> future_year_ead_reduced_results_list_;
    std::vector<AlternativeResults> with_project_alternative_results_;
    AlternativeResults without_project_alternative_results_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_ALTERNATIVE_COMPARISON_REPORT_RESULTS_HPP
