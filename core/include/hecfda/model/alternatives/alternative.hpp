// ported from: HEC.FDA.Model/alternatives/Alternative.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_ALTERNATIVES_ALTERNATIVE_HPP
#define HECFDA_MODEL_ALTERNATIVES_ALTERNATIVE_HPP
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/aggregated_consequences_binned.hpp"
#include "hecfda/model/metrics/aggregated_consequences_by_quantile.hpp"
#include "hecfda/model/metrics/alternative_results.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/metrics/impact_area_scenario_results.hpp"
#include "hecfda/model/metrics/scenario_results.hpp"
#include "hecfda/model/metrics/study_area_consequences_by_quantile.hpp"
#include "hecfda/statistics/distributions/empirical.hpp"
namespace hecfda {
namespace model {
namespace alternatives {

// ported from: Alternative.cs `public static class Alternative`. THE headline benefits-analysis
// math of FDA: annualizes a base-year and future-year ScenarioResults (Task 5) pair into
// equivalent-annual-damage (EqAD) AlternativeResults (Task 6) via a deterministic quantile walk
// (NOT a seeded Monte Carlo -- no RNG anywhere in this file). AlternativeComparisonReport (Task
// 10, not yet ported) consumes the AlternativeResults this produces.
//
// Struct-of-static-methods, mirroring Mathematics'/SpecialFunctions' own "static class" -> C++
// `struct` convention (see mathematics.hpp) for a C# `public static class` with no instance state.
//
// Ownership deviation (documented, necessary): C#'s `ScenarioResults computedResultsBaseYear`/
// `computedResultsFutureYear` parameters are reference-typed -- `AnnualizationCompute` can freely
// alias BOTH `AlternativeResults.BaseYearScenarioResults` and `.FutureYearScenarioResults` to the
// SAME object (the single-scenario null-coalesce case: `computedResultsBaseYear ??=
// computedResultsFutureYear`), and later callers see one shared object through two properties.
// `ScenarioResults` in this port is move-only (Task 5: it owns move-only `ImpactAreaScenarioResults`
// -> `StudyAreaConsequencesBinned` -> `AggregatedConsequencesBinned`, itself owning
// `unique_ptr<DynamicHistogram>` -- no clone/copy surface anywhere in that chain), so it cannot be
// duplicated the way a C# reference can, and `AlternativeResults::set_base_year_scenario_results`/
// `set_future_year_scenario_results` (Task 6) each take ownership BY MOVE. `annualization_compute`
// therefore takes `ScenarioResults*` (mutable, nullable, non-owning pointers the caller retains
// until the call returns) rather than `const ScenarioResults*`: the pointee is moved OUT of exactly
// once into the result. When base and future alias the SAME object (the single-scenario case),
// only `base_year_scenario_results_` is populated with the real data (the only field
// `AlternativeResults::sample_mean_eqad`'s identical-scenario branch ever reads -- see that
// method's own comment); `future_year_scenario_results_` is left at its default-empty state rather
// than attempting a second move from an already-moved-from source. When base and future are two
// DISTINCT objects (the general two-scenario path, even when they happen to compare `equals()`-
// true), both are moved independently with no such conflict. This is a real, narrow behavior
// difference from C#'s "two properties, one shared object" aliasing in the single-scenario case;
// no fixture assertion here depends on `future_year_scenario_results_` being populated in that
// case (`sample_mean_eqad`'s identical-branch never reads it).
struct Alternative {
    using ScenarioResults = hecfda::model::metrics::ScenarioResults;
    using ImpactAreaScenarioResults = hecfda::model::metrics::ImpactAreaScenarioResults;
    using AlternativeResults = hecfda::model::metrics::AlternativeResults;
    using AggregatedConsequencesBinned = hecfda::model::metrics::AggregatedConsequencesBinned;
    using AggregatedConsequencesByQuantile = hecfda::model::metrics::AggregatedConsequencesByQuantile;
    using ConsequenceType = hecfda::model::metrics::ConsequenceType;
    using RiskType = hecfda::model::metrics::RiskType;
    using Empirical = hecfda::statistics::distributions::Empirical;

    // ported from: Alternative.cs `private static double[] Interpolate(double baseYearEAD, double
    // mostLikelyFutureEAD, int baseYear, int mostLikelyFutureYear, int periodOfAnalysis)`.
    // `yearsBetweenBaseAndMLFInclusive = mostLikelyFutureYear - baseYear + 1` -- INCLUSIVE of both
    // endpoints. Index 0 is assigned baseYearEAD TWICE: once directly, then again (redundantly, to
    // the identical value -- at i=0 the ramp term is `(mostLikelyFutureEAD-baseYearEAD)*(0/(n-1))
    // == 0`) by the loop below. Transcribed verbatim, including the redundant first assignment. The
    // ramp loop runs `i` over `[0, yearsBetweenBaseAndMLFInclusive)`, linearly interpolating from
    // baseYearEAD (i=0) to mostLikelyFutureEAD (i=yearsBetweenBaseAndMLFInclusive-1) INCLUSIVE of
    // both ends; every remaining index from yearsBetweenBaseAndMLFInclusive through
    // periodOfAnalysis-1 is held flat at mostLikelyFutureEAD.
    static std::vector<double> interpolate(double base_year_ead, double most_likely_future_ead, int base_year,
                                            int most_likely_future_year, int period_of_analysis) {
        int years_between_base_and_mlf_inclusive = most_likely_future_year - base_year + 1;
        std::vector<double> interpolated_eads(static_cast<std::size_t>(period_of_analysis));
        interpolated_eads[0] = base_year_ead;
        for (int i = 0; i < years_between_base_and_mlf_inclusive; ++i) {
            interpolated_eads[static_cast<std::size_t>(i)] =
                base_year_ead + (most_likely_future_ead - base_year_ead) *
                                     (static_cast<double>(i) /
                                      static_cast<double>(years_between_base_and_mlf_inclusive - 1));
        }
        for (int i = years_between_base_and_mlf_inclusive; i < period_of_analysis; ++i) {
            interpolated_eads[static_cast<std::size_t>(i)] = most_likely_future_ead;
        }
        return interpolated_eads;
    }

    // ported from: Alternative.cs `private static double PresentValueCompute(double[]
    // interpolatedEADs, double discountRate)`. `periodOfAnalysis` is re-derived from
    // `interpolatedEADs.Length` (matches C# reading the array's own length rather than a
    // separately threaded parameter -- interpolate() above always sizes the array to the caller's
    // periodOfAnalysis, so this is the same value either way). `i + 1`: discounting starts one year
    // in (year 0's EAD is still discounted by one period).
    static double present_value_compute(const std::vector<double>& interpolated_eads, double discount_rate) {
        int period_of_analysis = static_cast<int>(interpolated_eads.size());
        double sum_present_value_ead = 0.0;
        for (int i = 0; i < period_of_analysis; ++i) {
            double present_value_interest_factor = 1.0 / std::pow(1.0 + discount_rate, i + 1);
            sum_present_value_ead += interpolated_eads[static_cast<std::size_t>(i)] * present_value_interest_factor;
        }
        return sum_present_value_ead;
    }

    // ported from: Alternative.cs `private static double IntoEquivalentAnnualTerms(double
    // sumPresentValueEAD, int periodOfAnalysis, double discountRate)`. PVIFA = present value
    // interest factor of annuity.
    static double into_equivalent_annual_terms(double sum_present_value_ead, int period_of_analysis,
                                                double discount_rate) {
        double present_value_interest_factor_of_annuity =
            (1.0 - 1.0 / std::pow(1.0 + discount_rate, period_of_analysis)) / discount_rate;
        return sum_present_value_ead / present_value_interest_factor_of_annuity;
    }

    // ported from: Alternative.cs `public static double ComputeEqad(double baseYearEAD, int
    // baseYear, double mostLikelyFutureEAD, int mostLikelyFutureYear, int periodOfAnalysis, double
    // discountRate)`. THE tightest oracle pin in this task: interpolate -> present-value sum ->
    // equivalent-annual-terms, three pure functions chained with no randomness.
    static double compute_eqad(double base_year_ead, int base_year, double most_likely_future_ead,
                                int most_likely_future_year, int period_of_analysis, double discount_rate) {
        std::vector<double> interpolated_eads = interpolate(base_year_ead, most_likely_future_ead, base_year,
                                                              most_likely_future_year, period_of_analysis);
        double sum_present_value_ead = present_value_compute(interpolated_eads, discount_rate);
        return into_equivalent_annual_terms(sum_present_value_ead, period_of_analysis, discount_rate);
    }

    // ported from: Alternative.cs `private static bool CanCompute(int baseYear, int futureYear, int
    // periodOfAnalysis)`.
    static bool can_compute(int base_year, int future_year, int period_of_analysis) {
        int difference = future_year - base_year + 1;
        return base_year <= future_year && difference >= 2 && difference <= period_of_analysis;
    }

    // ported from: Alternative.cs `private static AggregatedConsequencesByQuantile IterateOnEqad(
    // AggregatedConsequencesBinned baseYearDamageResult, AggregatedConsequencesBinned
    // mlfYearDamageResult, int baseYear, int futureYear, int periodOfAnalysis, double discountRate,
    // bool iterateOnFutureYear = true, ProgressReporter reporter = null)`. `ProgressReporter`
    // dropped (repo-wide MVVM/progress-messaging severance). `probabilitySteps = 25000`; step i's
    // probability is `(i + 0.5) / 25000`, sampling each side's EAD via
    // `ConsequenceHistogram.InverseCDF(probabilityStep)`, computing that step's EqAD via
    // compute_eqad, and collecting into `result_list` (Parallel.For -> serial `for`: each step
    // reads only its own `i` and appends independently, so serializing changes nothing about the
    // result -- matches every other Parallel.For->serial port in this repo, e.g. Empirical::
    // stack_empirical_distributions). The MEAN is computed SEPARATELY from the 25000-step walk:
    // compute_eqad is called ONE more time on the two ConsequenceHistograms' own SampleMean
    // properties, and that result is FORCE-SET onto the Empirical::fit_to_sample(result_list)
    // result via set_sample_mean (fit_to_sample never sets SampleMean itself -- see that method's
    // own comment): "the SampleMeans of the Consequence histograms may not be the mean of those
    // histograms; Sample mean is propagated from the original sample data" (upstream comment,
    // transcribed). C#'s `var convergenceCriteria = iterateOnFutureYear ? ... : ...;` local is a
    // dead read (assigned, never used again in the method body) -- NOT reproduced here: a pure
    // `ConvergenceCriteria` property getter with no side effects, so omitting it changes no
    // observable behavior (this repo's convention is to only reproduce dead C# reads that have an
    // observable side effect; see CLAUDE.md's faithful-bugs list for the bar this clears).
    //
    // FAITHFUL BUG (deliberately reproduced, do NOT fix without an explicit upstream change): the
    // Parallel.For body unconditionally dereferences BOTH `baseYearDamageResult.ConsequenceHistogram`
    // and `mlfYearDamageResult.ConsequenceHistogram`, regardless of `iterateOnFutureYear`. The
    // caller's own comments describe an "assume zero damage" fallback for the missing side, but no
    // such fallback exists anywhere in the C# body -- a genuinely null/missing side
    // NullReferenceExceptions in real C#. This port makes that failure mode an explicit
    // `std::runtime_error` (mirroring the NRE, matching this port's repo-wide "throw instead of
    // UB-dereference" convention) rather than implementing the un-written "assume zero" behavior
    // the comment describes but the code does not.
    static AggregatedConsequencesByQuantile iterate_on_eqad(const AggregatedConsequencesBinned* base_year_damage_result,
                                                              const AggregatedConsequencesBinned* mlf_year_damage_result,
                                                              int base_year, int future_year, int period_of_analysis,
                                                              double discount_rate, bool iterate_on_future_year) {
        if (base_year_damage_result == nullptr || mlf_year_damage_result == nullptr) {
            throw std::runtime_error(
                "Alternative::iterate_on_eqad: a missing base/future AggregatedConsequencesBinned "
                "would NullReferenceException in the real C# (both sides are dereferenced "
                "unconditionally inside IterateOnEqad's Parallel.For body, regardless of "
                "iterateOnFutureYear -- see this function's comment)");
        }

        constexpr int kProbabilitySteps = 25000;
        std::vector<double> result_list;
        result_list.reserve(static_cast<std::size_t>(kProbabilitySteps));
        for (int i = 0; i < kProbabilitySteps; ++i) {
            double probability_step = (static_cast<double>(i) + 0.5) / static_cast<double>(kProbabilitySteps);
            double ead_sampled_base_year =
                base_year_damage_result->consequence_histogram()->inverse_cdf(probability_step);
            double ead_sampled_future_year =
                mlf_year_damage_result->consequence_histogram()->inverse_cdf(probability_step);
            double eqad = compute_eqad(ead_sampled_base_year, base_year, ead_sampled_future_year, future_year,
                                        period_of_analysis, discount_rate);
            result_list.push_back(eqad);
        }

        double ead_sample_mean_base = base_year_damage_result->consequence_histogram()->sample_mean();
        double ead_sample_mean_future = mlf_year_damage_result->consequence_histogram()->sample_mean();
        double mean_eqad = compute_eqad(ead_sample_mean_base, base_year, ead_sample_mean_future, future_year,
                                         period_of_analysis, discount_rate);

        const std::string& damage_category = iterate_on_future_year ? mlf_year_damage_result->damage_category()
                                                                      : base_year_damage_result->damage_category();
        const std::string& asset_category = iterate_on_future_year ? mlf_year_damage_result->asset_category()
                                                                     : base_year_damage_result->asset_category();
        int region_id =
            iterate_on_future_year ? mlf_year_damage_result->region_id() : base_year_damage_result->region_id();
        ConsequenceType consequence_type = iterate_on_future_year ? mlf_year_damage_result->consequence_type()
                                                                    : base_year_damage_result->consequence_type();
        RiskType risk_type =
            iterate_on_future_year ? mlf_year_damage_result->risk_type() : base_year_damage_result->risk_type();

        Empirical emp_result = Empirical::fit_to_sample(result_list);
        emp_result.set_sample_mean(mean_eqad);
        return AggregatedConsequencesByQuantile(damage_category, asset_category, std::move(emp_result), region_id,
                                                 consequence_type, risk_type);
    }

    // ported from: Alternative.cs `private static void ProcessBaseAndFutureYearScenarioResults(...)`.
    // Matches base<->future ImpactAreaScenarioResults by ImpactAreaID (`future_year_results.
    // get_results(id)`, which THROWS on a miss -- see scenario_results.hpp's own documented
    // throw-vs-C#'s-dummy-fallback deviation; this task inherits that behavior change rather than
    // re-litigating it). Within each matched impact area, matches AggregatedConsequencesBinned by
    // (DamageCategory, AssetCategory, RegionID, ConsequenceType) via the newly-public
    // StudyAreaConsequencesBinned::get_consequence_result (Task 9 un-severance, see that method's
    // comment). LIFE-LOSS EXCLUSION: any base-year OR future-year consequence whose ConsequenceType
    // is LifeLoss is skipped entirely (`continue`) before ever reaching iterate_on_eqad -- "there
    // is no concept of EqAD for life loss" (upstream doc comment on AnnualizationCompute).
    // "Unprocessed future" tracking: C#'s `List<T>.Remove(x)` removes by REFERENCE identity (both
    // `ImpactAreaScenarioResults` and `AggregatedConsequencesBinned` are C# reference types with no
    // overridden `Equals`/`GetHashCode`) -- this port's translation tracks the same identity via
    // POINTERS into `future_year_results`'s own containers (`std::find`/`erase` by pointer value),
    // the value-semantics equivalent.
    static void process_base_and_future_year_scenario_results(const std::vector<int>& analysis_years,
                                                                double discount_rate, int period_of_analysis,
                                                                const ScenarioResults& base_year_results,
                                                                const ScenarioResults& future_year_results,
                                                                AlternativeResults& alternative_results) {
        std::vector<const ImpactAreaScenarioResults*> unprocessed_future_results;
        for (const ImpactAreaScenarioResults& ia : future_year_results.results_list()) {
            unprocessed_future_results.push_back(&ia);
        }

        for (const ImpactAreaScenarioResults& base_year_impact_area : base_year_results.results_list()) {
            // May throw if no future-year impact area shares this ID -- see scenario_results.hpp's
            // documented get_results throw-vs-C#'s-dummy-fallback deviation.
            const ImpactAreaScenarioResults& future_year_impact_area =
                future_year_results.get_results(base_year_impact_area.impact_area_id());

            auto it = std::find(unprocessed_future_results.begin(), unprocessed_future_results.end(),
                                 &future_year_impact_area);
            if (it != unprocessed_future_results.end()) {
                unprocessed_future_results.erase(it);
            }

            std::vector<const AggregatedConsequencesBinned*> unprocessed_future_consequences;
            for (const AggregatedConsequencesBinned& result :
                 future_year_impact_area.consequence_results().consequence_result_list()) {
                unprocessed_future_consequences.push_back(&result);
            }

            // Process consequences that exist in the base year.
            for (const AggregatedConsequencesBinned& base_year_consequence :
                 base_year_impact_area.consequence_results().consequence_result_list()) {
                // ONLY CONVERTING DAMAGE RESULTS FOR EQAD.
                if (base_year_consequence.consequence_type() == ConsequenceType::LifeLoss) {
                    continue;
                }
                const AggregatedConsequencesBinned* future_year_consequence =
                    future_year_impact_area.consequence_results().get_consequence_result(
                        base_year_consequence.damage_category(), base_year_consequence.asset_category(),
                        base_year_consequence.region_id(), base_year_consequence.consequence_type());

                AggregatedConsequencesByQuantile eqad_result =
                    iterate_on_eqad(&base_year_consequence, future_year_consequence, analysis_years[0],
                                     analysis_years[1], period_of_analysis, discount_rate,
                                     /*iterate_on_future_year=*/false);

                auto cit = std::find(unprocessed_future_consequences.begin(), unprocessed_future_consequences.end(),
                                      future_year_consequence);
                if (cit != unprocessed_future_consequences.end()) {
                    unprocessed_future_consequences.erase(cit);
                }

                alternative_results.add_consequence_results(std::move(eqad_result));
            }

            // Process any future-year consequences that didn't have matching base-year results.
            for (const AggregatedConsequencesBinned* future_year_consequence : unprocessed_future_consequences) {
                // ONLY CONVERTING DAMAGE RESULTS FOR EQAD.
                if (future_year_consequence->consequence_type() == ConsequenceType::LifeLoss) {
                    continue;
                }
                const AggregatedConsequencesBinned* base_year_consequence =
                    base_year_impact_area.consequence_results().get_consequence_result(
                        future_year_consequence->damage_category(), future_year_consequence->asset_category(),
                        future_year_consequence->region_id(), future_year_consequence->consequence_type());

                AggregatedConsequencesByQuantile eqad_result =
                    iterate_on_eqad(base_year_consequence, future_year_consequence, analysis_years[0],
                                     analysis_years[1], period_of_analysis, discount_rate,
                                     /*iterate_on_future_year=*/true);

                alternative_results.add_consequence_results(std::move(eqad_result));
            }
        }

        // UNLIKELY TO HIT THIS CODE: future-year impact area scenario results that did not match
        // to any base-year impact area scenario results (no damage in a particular impact area in
        // the base year but there is damage in the future year, or vice versa, e.g. managed
        // retreat).
        if (!unprocessed_future_results.empty()) {
            throw std::runtime_error(
                "Alternative::process_base_and_future_year_scenario_results: unmatched future "
                "results not properly accounted for");
        }
    }

    // ported from: Alternative.cs `private static AlternativeResults RunAnnualizationCompute(...)`.
    // See the class comment's "Ownership deviation" note for why `computed_results_base_year`/
    // `computed_results_future_year` are mutable pointers moved-from exactly once, at the very end
    // of each branch (after every read that needs the pointee's data has already happened).
    static AlternativeResults run_annualization_compute(std::vector<int> analysis_years, double discount_rate,
                                                          int period_of_analysis, int alternative_results_id,
                                                          ScenarioResults* computed_results_base_year,
                                                          ScenarioResults* computed_results_future_year) {
        AlternativeResults alternative_results(alternative_results_id, analysis_years, period_of_analysis);

        // if we just have one, use it as both the base and future.
        if (computed_results_base_year == nullptr) {
            computed_results_base_year = computed_results_future_year;
        }
        if (computed_results_future_year == nullptr) {
            computed_results_future_year = computed_results_base_year;
        }

        if (computed_results_base_year == nullptr) {
            // Both null: no scenario results available at all -- mirrors C#'s
            // `availableResults == null` guard inside the identical-scenario branch (unreachable
            // there in real C# without an NRE first; this port checks explicitly and up front).
            return AlternativeResults();
        }

        // if scenarios are identical or only one exists, no need to compute, just use the one that
        // exists.
        bool scenarios_are_identical = (computed_results_base_year == computed_results_future_year) ||
                                        computed_results_base_year->equals(*computed_results_future_year);

        if (scenarios_are_identical) {
            alternative_results.set_scenarios_are_identical(true);
            // ONLY CONVERTING DAMAGE RESULTS FOR EQAD.
            alternative_results.set_eqad_results(ScenarioResults::convert_to_study_area_consequences_by_quantile(
                *computed_results_base_year, ConsequenceType::Damage));

            if (computed_results_base_year == computed_results_future_year) {
                // Single-scenario aliasing: see class comment's "Ownership deviation" note --
                // only base_year_scenario_results_ is populated (the only field sample_mean_eqad's
                // identical-scenario branch reads).
                alternative_results.set_base_year_scenario_results(std::move(*computed_results_base_year));
            } else {
                alternative_results.set_base_year_scenario_results(std::move(*computed_results_base_year));
                alternative_results.set_future_year_scenario_results(std::move(*computed_results_future_year));
            }
        } else {
            process_base_and_future_year_scenario_results(analysis_years, discount_rate, period_of_analysis,
                                                            *computed_results_base_year, *computed_results_future_year,
                                                            alternative_results);
            alternative_results.set_base_year_scenario_results(std::move(*computed_results_base_year));
            alternative_results.set_future_year_scenario_results(std::move(*computed_results_future_year));
        }

        return alternative_results;
    }

    // ported from: Alternative.cs `public static AlternativeResults AnnualizationCompute(double
    // discountRate, int periodOfAnalysis, int alternativeResultsID, ScenarioResults
    // computedResultsBaseYear, ScenarioResults computedResultsFutureYear, int baseYear, int
    // futureYear, ProgressReporter reporter = null)`. `ProgressReporter` dropped entirely (repo-wide
    // MVVM/progress-messaging severance, matching every other ported compute entry point). Returns
    // a default-constructed `AlternativeResults()` (`is_null() == true`) on `!can_compute(...)`,
    // matching C#'s `return null` -- see alternative_results.hpp's class comment: the parameterless
    // ctor IS this port's "null" sentinel for this move-only type.
    static AlternativeResults annualization_compute(double discount_rate, int period_of_analysis,
                                                      int alternative_results_id,
                                                      ScenarioResults* computed_results_base_year,
                                                      ScenarioResults* computed_results_future_year, int base_year,
                                                      int future_year) {
        std::vector<int> analysis_years{base_year, future_year};

        if (!can_compute(base_year, future_year, period_of_analysis)) {
            return AlternativeResults();
        }
        return run_annualization_compute(std::move(analysis_years), discount_rate, period_of_analysis,
                                          alternative_results_id, computed_results_base_year,
                                          computed_results_future_year);
    }
};

}  // namespace alternatives
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_ALTERNATIVES_ALTERNATIVE_HPP
