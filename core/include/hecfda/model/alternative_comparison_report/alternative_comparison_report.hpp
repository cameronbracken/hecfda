// ported from: HEC.FDA.Model/alternativeComparisonReport/AlternativeComparisonReport.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_ALTERNATIVE_COMPARISON_REPORT_ALTERNATIVE_COMPARISON_REPORT_HPP
#define HECFDA_MODEL_ALTERNATIVE_COMPARISON_REPORT_ALTERNATIVE_COMPARISON_REPORT_HPP
#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/aggregated_consequences_binned.hpp"
#include "hecfda/model/metrics/aggregated_consequences_by_quantile.hpp"
#include "hecfda/model/metrics/alternative_comparison_report_results.hpp"
#include "hecfda/model/metrics/alternative_results.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/metrics/impact_area_scenario_results.hpp"
#include "hecfda/model/metrics/scenario_results.hpp"
#include "hecfda/model/metrics/study_area_consequences_binned.hpp"
#include "hecfda/model/metrics/study_area_consequences_by_quantile.hpp"
#include "hecfda/statistics/distributions/empirical.hpp"
namespace hecfda {
namespace model {
namespace alternative_comparison_report {

// ported from: AlternativeComparisonReport.cs `public enum AlternativeComparisonReportType`.
enum class AlternativeComparisonReportType { BaseYearEADReduced, FutureYearEADReduced };

// ported from: AlternativeComparisonReport.cs `public static class AlternativeComparisonReport`
// -- the FINAL numeric compute of the port: with/without-project damage-reduction (benefits) via
// empirical-distribution subtraction (without-project MINUS with-project, "damage reduced" =
// benefit). Ported as a class of static methods (matching the repo's `Alternative`/`Scenario`
// static-class precedent) rather than free functions, purely for namespacing.
//
// THE SUBTRACTION SEAM (`iterate_on_consequence_distribution_result`, the reason every one of the
// three reduction passes below funnels through one place): stacks
// `[withoutProjectDamageResult.ConsequenceDistribution, withProjectDamageResult.
// ConsequenceDistribution]` via `Empirical::stack_empirical_distributions(..., StackOp::subtract)`
// -- combine() folds LEFT-TO-RIGHT (`distributions[0] op distributions[1] op ...`), so this is
// literally `without - with` = the reduction (benefit) a project provides. The category-source
// branch (`iterateOnWithProject`) picks WHICH side's DamageCategory/AssetCategory/RegionID/
// ConsequenceType label the resulting `AggregatedConsequencesByQuantile` -- with-project's own
// labels when a with-project result drove the match (the common case, walking the with-project
// list), or without-project's own labels when an UNMATCHED without-project result is being
// reported (no with-project counterpart exists, so there is nothing with-project to label it
// with). RiskType is ALWAYS taken from `withoutProjectDamageResult` in BOTH branches -- transcribed
// verbatim, not a copy-paste slip (upstream picks one canonical RiskType source regardless of which
// category labels win).
//
// THE EQAD PASS (`compute_distribution_of_eqad_reduced`) walks `AggregatedConsequencesByQuantile`
// results (Task 2) directly off each `AlternativeResults::eqad_results()` (Task 6), matched by
// (DamageCategory, AssetCategory, RegionID, ConsequenceType) via `StudyAreaConsequencesByQuantile::
// get_consequence_result` (Task 3, RiskType defaults to the WILDCARD `RiskType::Total` there --
// matches any risk type). `AggregatedConsequencesByQuantile` is copyable (Task 2's own class
// comment), so the "already processed" tracking list here is a `std::vector<
// AggregatedConsequencesByQuantile>` matched by KEY (the same 5 identifying fields
// filter_by_categories filters by: damage/asset category, region, consequence type, AND the
// matched entry's own risk type) rather than by C#'s reference-identity `List<T>.Remove` -- the
// value-semantics analogue of that removal, parallel to (but distinct from) `alternative.hpp`'s
// own POINTER-identity tracking for its move-only `AggregatedConsequencesBinned` case below. A
// miss returns a dummy `AggregatedConsequencesByQuantile()` (`is_null() == true`,
// damage/asset="unassigned", region=0); removing that dummy from the tracking list is a safe
// no-op (it will not spuriously match a real entry in any fixture this task exercises), matching
// C#'s own reference-identity `Remove` being a no-op for a freshly-`new`'d object that was never
// in the list.
//
// THE EAD PASSES (`compute_distribution_ead_reduced`, run once per `AlternativeComparisonReportType`)
// walk `AggregatedConsequencesBinned` results (Task 3/4) off each alternative's base-year/future-year
// `ScenarioResults` (Task 5), matched per impact area (`ScenarioResults::get_results`, THROWS on a
// miss -- inherited deviation, see that header's own documented throw-vs-C#'s-dummy-fallback
// choice, same as `alternative.hpp`'s `process_base_and_future_year_scenario_results`) then by the
// same 4-key via `StudyAreaConsequencesBinned::get_consequence_result` (Task 3, RETURNS A POINTER,
// nullptr on a miss, RiskType defaulting to the NON-wildcard `RiskType::Fail` there -- see that
// header's own note on why that default is deliberately NOT `Total`). Since `AggregatedConsequences
// Binned` is MOVE-ONLY (owns `unique_ptr<DynamicHistogram>` members), the "already processed"
// tracking here uses POINTER identity (`std::find`/`erase` by pointer value into the SAME
// container), the exact precedent `alternative.hpp::process_base_and_future_year_scenario_results`
// already established for this same move-only-list-remove situation. Each matched (or, on a genuine
// miss, freshly-fabricated placeholder) pair is converted to a single-empirical
// `AggregatedConsequencesByQuantile` via `AggregatedConsequencesBinned::
// convert_to_single_empirical_distribution_of_consequences` (Task 4) before being handed to the
// SAME subtraction seam the EqAD pass uses.
//
// THE MISSING-COUNTERPART PLACEHOLDER: when a with-project (or without-project) consequence result
// has no counterpart on the other side, C# fabricates one via `new AggregatedConsequencesBinned(
// damageCategory, assetCategory, RegionID, ConsequenceType)` -- the "null"/dummy ctor
// `aggregated_consequences_binned.hpp` originally deferred porting ("not needed by this task's
// compute path... revisit if a later task needs an explicit null sentinel instance"). THIS task is
// that later task; see that header's own comment on the now-added ctor for how it reproduces the
// C# `new DynamicHistogram()` "ARBITRARY histogram" placeholder (ten 0-observations) without adding
// a standalone ctor for it to `DynamicHistogram` itself.
//
// SEVERED (present in the C# file, deliberately NOT ported here):
//  - `ProgressReporter pr` (default/None() fallback, `ReportMessage`/`ReportProgress` calls
//    throughout): no progress-reporting infrastructure in this port (repo-wide severance, matching
//    every other Phase 5/6 compute entry point).
//  - `ValidateAlternativeResults` + `OperationResult`: C# checks `withoutProjectAlternativeResults
//    == null` (a C# reference literally being null) and returns `null` early on a miss, reporting a
//    `Fail` message. This port's `compute_alternative_comparison_report` takes its with/without
//    inputs BY VALUE (see "Ownership deviation" below) -- a by-value C++ parameter is never "null",
//    it always holds SOME (possibly `is_null() == true`, i.e. "empty data") `AlternativeResults`
//    instance, so the C# null-reference check has no representable analogue here and is not
//    reproduced; the port always proceeds directly to the compute (per this task's own brief: "do
//    the compute if valid").
//  - The FIRST `switch (type)` inside `ComputeDistributionEADReduced` (building `withProj`/
//    `withProjectScenarioResultsList`, an `IEnumerable<ScenarioResults>`): verified DEAD CODE in the
//    C# source -- assigned but never read again (the loop below re-derives its own
//    `withProjResultsList` per-alternative via a SECOND, actually-used `switch` on `type`). Inert
//    housekeeping with zero observable effect; not transcribed (unlike the "Faithful upstream bugs"
//    list in `.claude/CLAUDE.md`, which covers OBSERVABLE-behavior quirks -- this is not one).
//
// Ownership deviation (move-only `AlternativeResults`, Task 6): the task brief's interface sketch
// gives `compute_alternative_comparison_report` a `const AlternativeResults&`/
// `const std::vector<AlternativeResults>&` signature, but `AlternativeComparisonReportResults`'s
// ctor (Task 7) takes its with/without-project results BY VALUE (moved in, matching that class's
// own "fresh construction per use" convention) -- and `AlternativeResults` has an explicitly
// DELETED copy ctor (move-only, Task 6's own class comment), so a const-ref input could never be
// moved into that ctor without a copy. This port instead takes both inputs BY VALUE: the caller
// moves ownership in, the two internal reduction passes read them through const references bound
// to those by-value locals (matching the brief's stated `compute_distribution_of_eqad_reduced`/
// `compute_distribution_ead_reduced` signatures exactly), and only at the very end -- after every
// read that needs the pointees' data has already happened -- are they `std::move`'d into the
// `AlternativeComparisonReportResults` ctor. Same "moved-from exactly once, at the very end"
// pattern `alternative.hpp::run_annualization_compute` already established for this same
// move-only-ownership tension (there via raw pointers instead, since that function also needed a
// nullable "no scenario supplied" state this one does not).
class AlternativeComparisonReport {
   public:
    using AlternativeResults = hecfda::model::metrics::AlternativeResults;
    using AlternativeComparisonReportResults = hecfda::model::metrics::AlternativeComparisonReportResults;
    using StudyAreaConsequencesByQuantile = hecfda::model::metrics::StudyAreaConsequencesByQuantile;
    using AggregatedConsequencesByQuantile = hecfda::model::metrics::AggregatedConsequencesByQuantile;
    using StudyAreaConsequencesBinned = hecfda::model::metrics::StudyAreaConsequencesBinned;
    using AggregatedConsequencesBinned = hecfda::model::metrics::AggregatedConsequencesBinned;
    using ScenarioResults = hecfda::model::metrics::ScenarioResults;
    using ImpactAreaScenarioResults = hecfda::model::metrics::ImpactAreaScenarioResults;
    using ConsequenceType = hecfda::model::metrics::ConsequenceType;
    using Empirical = hecfda::statistics::distributions::Empirical;

    // ported from: AlternativeComparisonReport.cs `public static AlternativeComparisonReportResults
    // ComputeAlternativeComparisonReport(AlternativeResults withoutProjectAlternativeResults,
    // IEnumerable<AlternativeResults> withProjectAlternativesResults, ProgressReporter pr = null)`.
    // See class comment: SEVERED ProgressReporter/OperationResult validation; BY-VALUE ownership
    // deviation for the move-only AlternativeResults inputs.
    static AlternativeComparisonReportResults compute_alternative_comparison_report(
        AlternativeResults without_project_alternative_results,
        std::vector<AlternativeResults> with_project_alternatives_results) {
        std::vector<StudyAreaConsequencesByQuantile> eqad_results = compute_distribution_of_eqad_reduced(
            without_project_alternative_results, with_project_alternatives_results);

        std::vector<StudyAreaConsequencesByQuantile> base_year_ead_results = compute_distribution_ead_reduced(
            without_project_alternative_results, with_project_alternatives_results,
            AlternativeComparisonReportType::BaseYearEADReduced);

        std::vector<StudyAreaConsequencesByQuantile> future_year_ead_results = compute_distribution_ead_reduced(
            without_project_alternative_results, with_project_alternatives_results,
            AlternativeComparisonReportType::FutureYearEADReduced);

        return AlternativeComparisonReportResults(
            std::move(with_project_alternatives_results), std::move(without_project_alternative_results),
            std::move(eqad_results), std::move(base_year_ead_results), std::move(future_year_ead_results));
    }

   private:
    // ported from: AlternativeComparisonReport.cs `private static AggregatedConsequencesByQuantile
    // IterateOnConsequenceDistributionResult(AggregatedConsequencesByQuantile withProjectDamageResult,
    // AggregatedConsequencesByQuantile withoutProjectDamageResult, ProgressReporter pr, bool
    // iterateOnWithProject = true)`. THE subtraction seam -- see class comment. SEVERED: unused
    // `pr` parameter (no progress-reporting infrastructure in this port).
    static AggregatedConsequencesByQuantile iterate_on_consequence_distribution_result(
        const AggregatedConsequencesByQuantile& with_project_damage_result,
        const AggregatedConsequencesByQuantile& without_project_damage_result, bool iterate_on_with_project = true) {
        std::vector<Empirical> empirical_list = {without_project_damage_result.consequence_distribution(),
                                                  with_project_damage_result.consequence_distribution()};
        Empirical empirical = Empirical::stack_empirical_distributions(empirical_list, Empirical::StackOp::subtract);

        if (iterate_on_with_project) {
            return AggregatedConsequencesByQuantile(
                with_project_damage_result.damage_category(), with_project_damage_result.asset_category(),
                std::move(empirical), with_project_damage_result.region_id(),
                with_project_damage_result.consequence_type(), without_project_damage_result.risk_type());
        }
        return AggregatedConsequencesByQuantile(
            without_project_damage_result.damage_category(), without_project_damage_result.asset_category(),
            std::move(empirical), without_project_damage_result.region_id(),
            without_project_damage_result.consequence_type(), without_project_damage_result.risk_type());
    }

    // Value-semantics analogue of C#'s reference-identity `List<AggregatedConsequencesByQuantile>.
    // Remove(x)` -- see class comment's "THE EQAD PASS" note. Removes (at most) the first entry in
    // `list` whose 5 identifying fields (damage/asset category, region, consequence type, risk
    // type) all match `target`'s; a no-op if nothing matches (mirrors `List<T>.Remove` returning
    // false silently, including for the dummy-on-miss `AggregatedConsequencesByQuantile()` case).
    static void remove_first_matching_by_key(std::vector<AggregatedConsequencesByQuantile>& list,
                                              const AggregatedConsequencesByQuantile& target) {
        auto it = std::find_if(list.begin(), list.end(), [&target](const AggregatedConsequencesByQuantile& x) {
            return x.damage_category() == target.damage_category() && x.asset_category() == target.asset_category() &&
                   x.region_id() == target.region_id() && x.consequence_type() == target.consequence_type() &&
                   x.risk_type() == target.risk_type();
        });
        if (it != list.end()) {
            list.erase(it);
        }
    }

    // ported from: AlternativeComparisonReport.cs `private static List<StudyAreaConsequencesByQuantile>
    // ComputeDistributionOfEqadReduced(AlternativeResults withoutProjectAlternativeResults,
    // IEnumerable<AlternativeResults> withProjectAlternativesResults, ProgressReporter pr)`. SEVERED:
    // unused `pr` parameter. See class comment's "THE EQAD PASS".
    static std::vector<StudyAreaConsequencesByQuantile> compute_distribution_of_eqad_reduced(
        const AlternativeResults& without_project_alternative_results,
        const std::vector<AlternativeResults>& with_project_alternatives_results) {
        std::vector<StudyAreaConsequencesByQuantile> damages_reduced_all_alternatives;

        for (const AlternativeResults& with_project_alternative_results : with_project_alternatives_results) {
            StudyAreaConsequencesByQuantile damage_reduced_one_alternative(
                with_project_alternative_results.alternative_id());

            std::vector<AggregatedConsequencesByQuantile> without_project_consequence_dist_list =
                without_project_alternative_results.eqad_results().consequence_result_list();

            for (const AggregatedConsequencesByQuantile& with_project_damage_result :
                 with_project_alternative_results.eqad_results().consequence_result_list()) {
                AggregatedConsequencesByQuantile without_project_damage_result =
                    without_project_alternative_results.eqad_results().get_consequence_result(
                        with_project_damage_result.damage_category(), with_project_damage_result.asset_category(),
                        with_project_damage_result.region_id(), with_project_damage_result.consequence_type());
                remove_first_matching_by_key(without_project_consequence_dist_list, without_project_damage_result);

                AggregatedConsequencesByQuantile damage_reduced_result = iterate_on_consequence_distribution_result(
                    with_project_damage_result, without_project_damage_result, /*iterate_on_with_project=*/true);
                damage_reduced_one_alternative.add_existing_consequence_result_object(
                    std::move(damage_reduced_result));
            }

            if (!without_project_consequence_dist_list.empty()) {
                for (const AggregatedConsequencesByQuantile& without_project_damage_result :
                     without_project_consequence_dist_list) {
                    AggregatedConsequencesByQuantile with_project_damage_result =
                        with_project_alternative_results.eqad_results().get_consequence_result(
                            without_project_damage_result.damage_category(),
                            without_project_damage_result.asset_category(), without_project_damage_result.region_id(),
                            without_project_damage_result.consequence_type());
                    AggregatedConsequencesByQuantile damage_reduced_result =
                        iterate_on_consequence_distribution_result(with_project_damage_result,
                                                                     without_project_damage_result,
                                                                     /*iterate_on_with_project=*/false);
                    damage_reduced_one_alternative.add_existing_consequence_result_object(
                        std::move(damage_reduced_result));
                }
            }
            damages_reduced_all_alternatives.push_back(std::move(damage_reduced_one_alternative));
        }
        return damages_reduced_all_alternatives;
    }

    // Fabricates the C# `new AggregatedConsequencesBinned(damageCategory, assetCategory, RegionID,
    // ConsequenceType)` "no counterpart" placeholder -- see class comment's "THE MISSING-COUNTERPART
    // PLACEHOLDER" note.
    static AggregatedConsequencesBinned make_missing_counterpart(const AggregatedConsequencesBinned& other) {
        return AggregatedConsequencesBinned(other.damage_category(), other.asset_category(), other.region_id(),
                                             other.consequence_type());
    }

    // ported from: AlternativeComparisonReport.cs `private static List<StudyAreaConsequencesByQuantile>
    // ComputeDistributionEADReduced(AlternativeResults withoutProjectAlternativeResults,
    // IEnumerable<AlternativeResults> withProjectAlternativesResults, AlternativeComparisonReportType
    // type, ProgressReporter pr)`. SEVERED: unused `pr` parameter; the dead first `switch (type)` --
    // see class comment. See class comment's "THE EAD PASSES".
    static std::vector<StudyAreaConsequencesByQuantile> compute_distribution_ead_reduced(
        const AlternativeResults& without_project_alternative_results,
        const std::vector<AlternativeResults>& with_project_alternatives_results,
        AlternativeComparisonReportType type) {
        const ScenarioResults& without_project_scenario_results =
            type == AlternativeComparisonReportType::BaseYearEADReduced
                ? without_project_alternative_results.base_year_scenario_results()
                : without_project_alternative_results.future_year_scenario_results();

        std::vector<StudyAreaConsequencesByQuantile> damage_reduced_alternatives;

        for (const AlternativeResults& with_proj_results : with_project_alternatives_results) {
            int alternative_id = with_proj_results.alternative_id();
            const ScenarioResults& with_proj_results_list =
                type == AlternativeComparisonReportType::BaseYearEADReduced
                    ? with_proj_results.base_year_scenario_results()
                    : with_proj_results.future_year_scenario_results();

            StudyAreaConsequencesByQuantile damage_reduced_alternative(alternative_id);

            for (const ImpactAreaScenarioResults& with_project_results : with_proj_results_list.results_list()) {
                // May throw if no without-project impact area shares this ID -- see
                // scenario_results.hpp's documented get_results throw-vs-C#'s-dummy-fallback
                // deviation (same precedent alternative.hpp's own EQAD annualization pass relies
                // on).
                const ImpactAreaScenarioResults& without_project_results =
                    without_project_scenario_results.get_results(with_project_results.impact_area_id());

                const StudyAreaConsequencesBinned& with_project_damage_results =
                    with_project_results.consequence_results();
                const StudyAreaConsequencesBinned& without_project_damage_results =
                    without_project_results.consequence_results();

                std::vector<const AggregatedConsequencesBinned*> without_project_damage_results_list;
                for (const AggregatedConsequencesBinned& result :
                     without_project_damage_results.consequence_result_list()) {
                    without_project_damage_results_list.push_back(&result);
                }

                for (const AggregatedConsequencesBinned& with_project_damage_result :
                     with_project_damage_results.consequence_result_list()) {
                    const AggregatedConsequencesBinned* without_project_damage_result =
                        without_project_damage_results.get_consequence_result(
                            with_project_damage_result.damage_category(), with_project_damage_result.asset_category(),
                            with_project_damage_result.region_id(), with_project_damage_result.consequence_type());

                    auto it = std::find(without_project_damage_results_list.begin(),
                                         without_project_damage_results_list.end(), without_project_damage_result);
                    if (it != without_project_damage_results_list.end()) {
                        without_project_damage_results_list.erase(it);
                    }

                    std::optional<AggregatedConsequencesBinned> fallback_without;
                    const AggregatedConsequencesBinned* effective_without = without_project_damage_result;
                    if (effective_without == nullptr) {
                        fallback_without.emplace(make_missing_counterpart(with_project_damage_result));
                        effective_without = &fallback_without.value();
                    }

                    AggregatedConsequencesByQuantile damage_reduced_result = iterate_on_consequence_distribution_result(
                        with_project_damage_result.convert_to_single_empirical_distribution_of_consequences(),
                        effective_without->convert_to_single_empirical_distribution_of_consequences(),
                        /*iterate_on_with_project=*/true);
                    damage_reduced_alternative.add_existing_consequence_result_object(
                        std::move(damage_reduced_result));
                }

                if (!without_project_damage_results_list.empty()) {
                    for (const AggregatedConsequencesBinned* without_project_damage_result :
                         without_project_damage_results_list) {
                        const AggregatedConsequencesBinned* with_project_damage_result =
                            with_project_damage_results.get_consequence_result(
                                without_project_damage_result->damage_category(),
                                without_project_damage_result->asset_category(),
                                without_project_damage_result->region_id(),
                                without_project_damage_result->consequence_type());

                        std::optional<AggregatedConsequencesBinned> fallback_with;
                        const AggregatedConsequencesBinned* effective_with = with_project_damage_result;
                        if (effective_with == nullptr) {
                            fallback_with.emplace(make_missing_counterpart(*without_project_damage_result));
                            effective_with = &fallback_with.value();
                        }

                        AggregatedConsequencesByQuantile damage_reduced_result =
                            iterate_on_consequence_distribution_result(
                                effective_with->convert_to_single_empirical_distribution_of_consequences(),
                                without_project_damage_result->convert_to_single_empirical_distribution_of_consequences(),
                                /*iterate_on_with_project=*/false);
                        damage_reduced_alternative.add_existing_consequence_result_object(
                            std::move(damage_reduced_result));
                    }
                }
            }
            damage_reduced_alternatives.push_back(std::move(damage_reduced_alternative));
        }
        return damage_reduced_alternatives;
    }
};

}  // namespace alternative_comparison_report
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_ALTERNATIVE_COMPARISON_REPORT_ALTERNATIVE_COMPARISON_REPORT_HPP
