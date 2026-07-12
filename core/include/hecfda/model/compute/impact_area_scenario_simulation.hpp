// ported from: HEC.FDA.Model/compute/ImpactAreaScenarioSimulation.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_COMPUTE_IMPACT_AREA_SCENARIO_SIMULATION_HPP
#define HECFDA_MODEL_COMPUTE_IMPACT_AREA_SCENARIO_SIMULATION_HPP
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/metrics/impact_area_scenario_results.hpp"
#include "hecfda/model/metrics/threshold.hpp"
#include "hecfda/model/metrics/threshold_enum.hpp"
#include "hecfda/model/paired_data/graphical_uncertain_paired_data.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/distributions/continuous_distribution_extensions.hpp"
#include "hecfda/statistics/distributions/deterministic.hpp"
#include "hecfda/statistics/validation.hpp"
namespace hecfda {
namespace model {
namespace compute {

// ported from: ImpactAreaScenarioSimulation.cs `internal readonly record struct
// FrequencyStageCurves(PairedData ChannelStage, PairedData FloodplainStage)` (lines 25-28). Holds
// both the channel (exterior) and floodplain (interior) frequency-stage curves produced by
// get_frequency_stage_sample(); when no interior/exterior relationship exists both fields hold the
// SAME realized curve. C#'s record struct wraps two REFERENCE-typed PairedData fields, so "the same
// curve" there means both fields alias one object (ReferenceEquals true). This port's PairedData is
// a plain value type (copyable, no reference semantics), so the closest observably-identical
// analog is two independently-constructed copies holding identical x/y values -- read-only callers
// (Task 9's risk/performance compute) cannot tell the difference; nothing in this port's PairedData
// surface mutates a curve out from under an aliased caller the way a shared C# reference could.
struct FrequencyStageCurves {
    hecfda::model::paired_data::PairedData channel_stage;
    hecfda::model::paired_data::PairedData floodplain_stage;
};

// ported from: ImpactAreaScenarioSimulation.cs `public class ImpactAreaScenarioSimulation :
// ValidationErrorLogger, IProgressReport` -- the EAD (expected annual damage) Monte Carlo compute
// engine. Phase 5 Task 7 ports the SKELETON: fields, the seed constants, the fluent
// `SimulationBuilder`, `can_compute`/the inherited `validate()` gate, and
// `initialize_consequence_histograms`. Phase 5 Task 8 adds `populate_random_numbers` (the seeded
// per-curve RNG setup) and the frequency-stage assembly (`get_frequency_stage_sample`/
// `get_stage_freq`), plus the `FrequencyStageCurves` struct above. Phase 5 Task 9 adds the
// risk/consequence integration and performance/threshold/assurance compute
// (`setup_performance_thresholds`, `compute_risk_from_stage_frequency` and friends,
// `compute_performance`/`compute_levee_performance`, `compute_default_threshold`). Phase 5 Task 10
// (this task) wires all of it together: `compute()` now runs the full pipeline (`can_compute` ->
// `initialize_consequence_histograms` -> `setup_performance_thresholds` ->
// `populate_random_numbers` -> `compute_iterations` -> `parallel_results_are_converged`), and adds
// `preview_compute()`, the single-deterministic-pass shortcut. Phase 5 Task 11 finishes anything
// left (see PLAN.md for what remains).
//
// Base class: `ValidationErrorLogger : Validation` is a thin WPF-messaging layer over the same
// `Validation` this port already has (`hecfda::statistics::Validation`, ported Phase 1) -- see
// `ContinuousDistribution`'s identical choice to inherit `Validation` directly rather than port a
// messaging-only intermediate. `error_level()`/`has_errors()`/`add_single_property_rule(...)`/
// `validate()` all come from that base.
//
// Move-only: holds `GraphicalUncertainPairedData`/`UncertainPairedData`/`ImpactAreaScenarioResults`
// members, all themselves move-only (see their own headers) -- matches the "fresh construction per
// use" convention already established across this port's Model-layer containers.
//
// SEVERANCES (present in the C# file, deliberately NOT ported):
//  - `IProgressReport`/`ProgressReport` event/`ReportProgress`: MVVM progress reporting, no UI
//    layer in this port. `ComputeIterations`'s `ReportProgress` calls (including the
//    `IMPACT_AREA_SIM_COMPLETED` sentinel at the very end) and the two `ReportMessage`
//    begin/end-compute calls in `Compute()` are dropped for the same reason; the
//    `completedIterations`/`expectedIterations` locals that existed solely to feed the dropped
//    `ReportProgress` percent-complete calculation are dropped alongside them (see
//    `compute_iterations()`'s own comment).
//  - `ReportMessage`/`ErrorMessage`/`MessageEventArgs`/`ComputeCompleteMessage`/
//    `FrequencyDamageMessage`: MVVM messaging, no analog (repo-wide messaging severance).
//  - `[StoredProperty("ImpactAreaScenarioSimulation")]`: reflection-driven serialization metadata,
//    no analog (repo-wide `[StoredProperty]` severance).
//  - `System.Threading`/`System.Threading.Tasks` (`CancellationToken`, the `Compute(criteria,
//    cancellationToken, ...)` overload, `Parallel.For`, `TaskCanceledException`, the
//    `AggregateException` catch/rethrow that exists solely to unwrap a `Parallel.For` cancellation):
//    this port's Monte Carlo loop (`compute_iterations()`, Task 10) is serial, matching the
//    repo-wide "no threading primitives" convention already established for
//    `ImpactAreaScenarioResults::get_or_create_uncertain_consequence_frequency_curve` (its own
//    header documents the same choice for the `_uncertainCurveLock` C# field) and for
//    `ImpactAreaScenarioResults::parallel_results_are_converged`'s own `Parallel.For`->serial-`for`
//    port. Only the 2-arg `Compute(ConvergenceCriteria, bool)` overload ("this code path currently
//    only used by tests" per the C# doc comment) is ported, with the 3-arg overload's body inlined
//    directly into it; the 3-arg cancellation-token overload itself is dropped entirely.
//  - `LogSimulationPropertyRuleErrors()`: builds intro-message strings solely for the severed
//    `ReportMessage` call -- no observable effect once messaging is gone, so dropped rather than
//    kept as a silent no-op.
//  - Per-property validation-rule registration for every `With*(UncertainPairedData)` /
//    `With*(GraphicalUncertainPairedData)` builder overload (`WithInflowOutflow`, `WithFlowStage`
//    -- both its "flow stage" HasErrors rule AND its "stage range" Fatal rule --, `WithFrequencyStage`,
//    `WithInteriorExterior`, `WithStageDamages`, `WithStageLifeLoss`, `WithNonFailureStageLifeLoss`):
//    `UncertainPairedData`/`GraphicalUncertainPairedData` have NO `validate()`/`has_errors()`
//    surface in this port (both headers' own SEVERANCES notes: "there is no rules/validation-on-
//    construct infrastructure anywhere in this Model-layer port"), so a rule whose predicate calls
//    `.Validate(); return !.HasErrors;` has no analog to call. The "stage range" rule additionally
//    needs per-point access to `Yvals[0]`/`Yvals[^1]` (via `InverseCDF`), which `UncertainPairedData`
//    does not expose (`ys_` is private with no accessor) -- porting it would require widening that
//    Phase-2 class's API, out of this task's scope. `with_flow_frequency(ContinuousDistribution)`'s
//    rule IS ported (see that method's own comment) since `ContinuousDistribution` genuinely has
//    the required `Validation` surface in this port.
//
// `can_compute()` and `initialize_consequence_histograms()` are PUBLIC here even though C#'s
// `CanCompute`/`InitializeConsequenceHistograms` are `private`: this port has no C# `internal`/
// test-assembly-friend equivalent, so -- matching this repo's existing precedent for deliberate
// access relaxations (e.g. the distribution factory's port-internal enum keys) -- they are exposed
// directly so fixture dispatch can exercise the CanCompute gate and the consequence-histogram setup
// without needing to drive the full (not-yet-implemented) `compute()` path.
class ImpactAreaScenarioSimulation : public hecfda::statistics::Validation {
   public:
    using ConvergenceCriteria = hecfda::statistics::ConvergenceCriteria;
    using ErrorLevel = hecfda::statistics::ErrorLevel;
    using ContinuousDistribution = hecfda::statistics::distributions::ContinuousDistribution;
    using UncertainPairedData = hecfda::model::paired_data::UncertainPairedData;
    using GraphicalUncertainPairedData = hecfda::model::paired_data::GraphicalUncertainPairedData;
    using ImpactAreaScenarioResults = hecfda::model::metrics::ImpactAreaScenarioResults;
    using Threshold = hecfda::model::metrics::Threshold;
    using ThresholdEnum = hecfda::model::metrics::ThresholdEnum;
    using ConsequenceType = hecfda::model::metrics::ConsequenceType;
    using RiskType = hecfda::model::metrics::RiskType;
    using PairedData = hecfda::model::paired_data::PairedData;

    class SimulationBuilder;

    // Move-only (see class comment).
    ImpactAreaScenarioSimulation(ImpactAreaScenarioSimulation&&) = default;
    ImpactAreaScenarioSimulation& operator=(ImpactAreaScenarioSimulation&&) = default;
    ImpactAreaScenarioSimulation(const ImpactAreaScenarioSimulation&) = delete;
    ImpactAreaScenarioSimulation& operator=(const ImpactAreaScenarioSimulation&) = delete;

    // ported from: ImpactAreaScenarioSimulation.cs `public const int IMPACT_AREA_SIM_COMPLETED
    // = -1001` / `public const int DEFAULT_THRESHOLD_ID = 0`.
    static constexpr int kImpactAreaSimCompleted = -1001;
    static constexpr int kDefaultThresholdId = 0;

    // ported from: ImpactAreaScenarioSimulation.cs `public static SimulationBuilder
    // Builder(int impactAreaID)`. Defined out-of-line below, after SimulationBuilder is complete
    // (mirrors OccupancyType::builder()'s layout).
    static SimulationBuilder builder(int impact_area_id);

    int impact_area_id() const { return impact_area_id_; }

    // ported from: ImpactAreaScenarioSimulation.cs `public ImpactAreaScenarioResults
    // Compute(ConvergenceCriteria convergenceCriteria, bool computeIsDeterministic = false)` --
    // "This code path currently only used by tests" per the C# doc comment. C# has this 2-arg
    // overload delegate to a 3-arg `Compute(ConvergenceCriteria, CancellationToken, bool)`
    // overload that then runs the body below; this port drops the CancellationToken entirely (see
    // class comment's SEVERANCES note -- the Monte Carlo loop is serial, no cancellation surface
    // needed) and inlines the 3-arg overload's body directly here.
    ImpactAreaScenarioResults compute(ConvergenceCriteria convergence_criteria,
                                       bool compute_is_deterministic = false) {
        if (!can_compute(convergence_criteria)) {
            impact_area_scenario_results_ = ImpactAreaScenarioResults(impact_area_id_, /*is_null=*/true);
            return std::move(impact_area_scenario_results_);
        }
        convergence_criteria_ = convergence_criteria;
        initialize_consequence_histograms(convergence_criteria_);
        setup_performance_thresholds(convergence_criteria_);
        // ReportMessage "begin compute"/"end compute" (severed, see class comment) elided.
        populate_random_numbers(convergence_criteria_);
        compute_iterations(convergence_criteria_, compute_is_deterministic);
        impact_area_scenario_results_.parallel_results_are_converged(.95, .05);
        return std::move(impact_area_scenario_results_);
    }

    // ported from: ImpactAreaScenarioSimulation.cs `public ImpactAreaScenarioResults
    // PreviewCompute()` (lines 724-731). A single deterministic pass over ONLY the failure
    // stage-damage functions (no thresholds, no performance, no life-loss, no levee) -- used by
    // callers that just want a quick single-iteration damage estimate without the full
    // SetupPerformanceThresholds/PopulateRandomNumbers/ComputeIterations machinery. `create_ea_
    // consequence_histograms` and `compute_risk_from_stage_frequency` (the per-consequence-list
    // overload) are both already-private members reachable directly from here.
    ImpactAreaScenarioResults preview_compute() {
        convergence_criteria_ = ConvergenceCriteria(1, 1);
        create_ea_consequence_histograms(ConvergenceCriteria(1, 1), failure_stage_damage_functions_,
                                          ConsequenceType::Damage, RiskType::Fail);
        FrequencyStageCurves curves = get_frequency_stage_sample(/*compute_is_deterministic=*/true, 1);
        compute_risk_from_stage_frequency(curves.floodplain_stage, /*this_compute_iteration=*/0,
                                           /*this_chunk_iteration=*/0, /*compute_is_deterministic=*/true,
                                           failure_stage_damage_functions_, ConsequenceType::Damage,
                                           /*save_annualized_result=*/true, /*save_freq_curves=*/true);
        impact_area_scenario_results_.consequence_results().put_data_into_histograms();
        return std::move(impact_area_scenario_results_);
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private bool
    // CanCompute(ConvergenceCriteria convergenceCriteria)`. `LogSimulationPropertyRuleErrors()`
    // (severed, see class comment) is not called. Public here -- see class comment.
    bool can_compute(ConvergenceCriteria convergence_criteria) {
        bool result = true;
        if (error_level() >= ErrorLevel::Fatal) {
            result = false;
        }
        convergence_criteria.validate();
        if (convergence_criteria.has_errors()) {
            result = false;
        }
        return result;
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private void
    // InitializeConsequenceHistograms(ConvergenceCriteria convergenceCriteria)`. Public here --
    // see class comment.
    void initialize_consequence_histograms(const ConvergenceCriteria& convergence_criteria) {
        if (has_failure_stage_damage_) {
            create_ea_consequence_histograms(convergence_criteria, failure_stage_damage_functions_,
                                              ConsequenceType::Damage, RiskType::Fail);
        }
        if (has_non_failure_stage_damage_) {
            create_ea_consequence_histograms(convergence_criteria, non_failure_stage_damage_functions_,
                                              ConsequenceType::Damage, RiskType::Non_Fail);
        }
        if (has_non_failure_stage_life_loss_) {
            create_ea_consequence_histograms(convergence_criteria, non_failure_stage_life_loss_functions_,
                                              ConsequenceType::LifeLoss, RiskType::Non_Fail);
        }
        if (has_failure_stage_life_loss_) {
            create_ea_consequence_histograms(convergence_criteria, failure_stage_life_loss_functions_,
                                              ConsequenceType::LifeLoss, RiskType::Fail);
        }
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private void
    // PopulateRandomNumbers(ConvergenceCriteria convergenceCriteria)` (lines 202-252). "This
    // method tells each object that will be sampled in the full compute to generate random
    // numbers for sampling." `quantityOfRandomNumbers = Convert.ToInt32(convergenceCriteria.
    // MaxIterations * 1.25)` -- Convert.ToInt32(double) rounds HALF-TO-EVEN (banker's rounding),
    // not truncation; see dynamic_histogram.hpp's convert_to_int32 for the established precedent
    // of reproducing this via std::nearbyint (honors the default round-to-nearest-ties-to-even FP
    // mode), used here directly rather than duplicating a private helper for one call site.
    //
    // Every per-curve conditional is transcribed verbatim, including which seed constant each
    // curve uses. C#'s nullable-field gate is `!<field>.CurveMetaData.IsNull` for every
    // `UncertainPairedData`/`GraphicalUncertainPairedData` field (`UncertainPairedData.IsNull` is
    // itself defined as `CurveMetaData.IsNull` upstream, verified against
    // HEC.FDA.Model/paireddata/UncertainPairedData.cs). This port's nullable UPD fields are
    // `std::optional<UncertainPairedData>`, defaulting to `std::nullopt` for "never assigned by
    // the builder" (the C# analog: the default parameterless-ctor UPD, whose CurveMetaData is
    // ALSO IsNull==true) -- so `has_value() && !metadata().is_null()` reproduces the C# gate for
    // both the "never assigned" and the "assigned but with a still-null CurveMetaData" cases
    // identically to `!IsNull` on the always-present C# object. Public here (see class comment)
    // for fixture-dispatch access, mirroring can_compute/initialize_consequence_histograms.
    void populate_random_numbers(const ConvergenceCriteria& convergence_criteria) {
        int quantity_of_random_numbers =
            static_cast<int>(std::nearbyint(convergence_criteria.max_iterations() * 1.25));

        if (frequency_discharge_ != nullptr) {
            frequency_discharge_->generate_random_samples_of_numbers(kFrequencySeed, quantity_of_random_numbers);
        }
        if (!frequency_discharge_graphical_.curve_meta_data().is_null()) {
            frequency_discharge_graphical_.generate_random_numbers(kFrequencySeed, quantity_of_random_numbers);
        }
        if (unregulated_regulated_.has_value() && !unregulated_regulated_->metadata().is_null()) {
            unregulated_regulated_->generate_random_numbers(kFlowRegulationSeed, quantity_of_random_numbers);
        }
        if (discharge_stage_.has_value() && !discharge_stage_->metadata().is_null()) {
            discharge_stage_->generate_random_numbers(kStageFlowSeed, quantity_of_random_numbers);
        }
        if (!frequency_stage_.curve_meta_data().is_null()) {
            frequency_stage_.generate_random_numbers(kFrequencySeed, quantity_of_random_numbers);
        }
        if (channel_stage_floodplain_stage_.has_value() && !channel_stage_floodplain_stage_->metadata().is_null()) {
            channel_stage_floodplain_stage_->generate_random_numbers(kExteriorInteriorSeed, quantity_of_random_numbers);
        }
        if (system_response_function_.has_value() && !system_response_function_->metadata().is_null()) {
            system_response_function_->generate_random_numbers(kSystemResponseSeed, quantity_of_random_numbers);
        }
        for (auto& stage_damage : failure_stage_damage_functions_) {
            stage_damage.generate_random_numbers(kStageDamageSeed, quantity_of_random_numbers);
        }
        for (auto& stage_damage : non_failure_stage_damage_functions_) {
            stage_damage.generate_random_numbers(kStageDamageSeed, quantity_of_random_numbers);
        }
        for (auto& stage_life_loss : failure_stage_life_loss_functions_) {
            stage_life_loss.generate_random_numbers(kStageLifeLossSeed, quantity_of_random_numbers);
        }
        for (auto& stage_life_loss : non_failure_stage_life_loss_functions_) {
            stage_life_loss.generate_random_numbers(kStageLifeLossSeed, quantity_of_random_numbers);
        }
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private PairedData GetStageFreq(bool
    // computeIsDeterministic, long thisComputeIteration, PairedData frequencyDischarge)` (lines
    // 435-451). Composes the discharge-stage (rating) curve onto the sampled frequency-discharge
    // curve, through an optional reg/unreg (inflow-outflow) transform first when present. The
    // `_UnregulatedRegulated.CurveMetaData.IsNull` gate maps to this port's `std::optional` the
    // same way populate_random_numbers()'s gates do (see its comment). `discharge_stage_` is
    // assumed present (dereferenced unconditionally, matching the C#'s unconditional
    // `_DischargeStage.SamplePairedData(...)` access on both branches) -- this method's caller
    // contract, exactly as upstream, is that a simulation built without WithFlowStage/
    // with_flow_stage never reaches here. Public here (see class comment).
    hecfda::model::paired_data::PairedData get_stage_freq(
        bool compute_is_deterministic, long this_compute_iteration,
        const hecfda::model::paired_data::PairedData& frequency_discharge) {
        using hecfda::model::paired_data::PairedData;
        if (!unregulated_regulated_.has_value() || unregulated_regulated_->metadata().is_null()) {
            PairedData discharge_stage_sample =
                discharge_stage_->sample_paired_data(this_compute_iteration, compute_is_deterministic);
            return discharge_stage_sample.compose(frequency_discharge);
        }
        PairedData inflow_outflow_sample =
            unregulated_regulated_->sample_paired_data(this_compute_iteration, compute_is_deterministic);
        PairedData transform_ff = inflow_outflow_sample.compose(frequency_discharge);
        PairedData discharge_stage_sample =
            discharge_stage_->sample_paired_data(this_compute_iteration, compute_is_deterministic);
        return discharge_stage_sample.compose(transform_ff);
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private FrequencyStageCurves
    // GetFrequencyStageSample(bool computeIsDeterministic, long thisComputeIteration)` (lines
    // 405-433). Builds the per-iteration frequency-stage realization: either straight from a
    // direct graphical frequency-stage curve (short-circuit, `_FrequencyStage` set via
    // with_frequency_stage), or by composing the sampled frequency curve (analytical
    // `bootstrap_to_paired_data` or graphical `sample_paired_data` -- see
    // sample_frequency_discharge() below) through get_stage_freq(); then, if an interior/exterior
    // (channel-stage/floodplain-stage) relationship exists, composes that too and returns distinct
    // channel/floodplain curves -- otherwise both fields hold copies of the same curve (see the
    // FrequencyStageCurves struct comment for why "copies" replaces C#'s ReferenceEquals identity
    // here). Public here (see class comment).
    FrequencyStageCurves get_frequency_stage_sample(bool compute_is_deterministic, long this_compute_iteration) {
        using hecfda::model::paired_data::PairedData;
        PairedData frequency_stage_sample =
            frequency_stage_.curve_meta_data().is_null()
                ? get_stage_freq(compute_is_deterministic, this_compute_iteration,
                                  sample_frequency_discharge(compute_is_deterministic, this_compute_iteration))
                : frequency_stage_.sample_paired_data(this_compute_iteration, compute_is_deterministic);

        if (channel_stage_floodplain_stage_.has_value() && !channel_stage_floodplain_stage_->metadata().is_null()) {
            PairedData channelstage_floodplainstage_sample =
                channel_stage_floodplain_stage_->sample_paired_data(this_compute_iteration, compute_is_deterministic);
            PairedData frequency_floodplainstage = channelstage_floodplainstage_sample.compose(frequency_stage_sample);
            return FrequencyStageCurves{std::move(frequency_stage_sample), std::move(frequency_floodplainstage)};
        }
        return FrequencyStageCurves{frequency_stage_sample, std::move(frequency_stage_sample)};
    }

    // ============================================================================================
    // Phase 5 Task 9: risk/consequence integration + performance/threshold/assurance. Public here
    // (see class comment's "public-access relaxation" rationale) so fixture dispatch can drive
    // setup_performance_thresholds() directly (the default-threshold fixture's entry point) without
    // the full Monte Carlo compute() loop, which is Task 10's job. Method order below mirrors the
    // C# source's declaration order (136-722), not call order.
    // ============================================================================================

    // ported from: ImpactAreaScenarioSimulation.cs `private void
    // SetupPerformanceThresholds(ConvergenceCriteria convergenceCriteria)` (lines 136-160).
    // Threshold ID 0 is reserved for the "default" threshold. If a levee is present, a
    // system-response threshold (ID 0) is ALWAYS added (overriding any prior ID-0 registration --
    // matches C# exactly: the `else if` below only guards the no-levee branch). Otherwise, if the
    // caller hasn't already registered threshold ID 0 (e.g. via with_additional_threshold), a
    // default threshold is computed from a SINGLE deterministic pass (iteration 1) over the
    // failure stage-damage functions. `_SystemResponseFunction.Xvals != null` (C#'s gate) maps to
    // `system_response_function_.has_value()` here: this field is only ever assigned (Xvals AND
    // CurveMetaData together) via with_levee(), so "never assigned" (`std::nullopt`) is the exact
    // analog of C#'s null-Xvals default-constructed placeholder (verified against the real
    // UncertainPairedData() parameterless ctor, which leaves Xvals null) -- unlike
    // populate_random_numbers()'s gate elsewhere in this file, which additionally checks
    // `!metadata().is_null()` for fields that CAN be assigned-but-still-null; that extra check is
    // not needed here since with_levee always sets both together.
    void setup_performance_thresholds(const ConvergenceCriteria& convergence_criteria) {
        bool default_threshold_exists = false;
        for (const auto& threshold :
             impact_area_scenario_results_.performance_by_thresholds().list_of_thresholds()) {
            if (threshold.threshold_id() == 0) {
                default_threshold_exists = true;
                break;
            }
        }

        if (system_response_function_.has_value()) {
            Threshold system_response_threshold = determine_system_response_threshold(convergence_criteria);
            impact_area_scenario_results_.performance_by_thresholds().add_threshold(
                std::move(system_response_threshold));
        } else if (!default_threshold_exists) {
            FrequencyStageCurves curves = get_frequency_stage_sample(/*compute_is_deterministic=*/true, 1);
            // Floodplain stage because we're computing risk. saveAnnualizedResult=false,
            // saveFreqCurves=true: only stages ConsequenceFrequencyFunctions (compute_default_threshold's
            // input), skips the histogram/EAD-realization bookkeeping entirely.
            compute_risk_from_stage_frequency(curves.floodplain_stage, /*this_compute_iteration=*/1,
                                               /*this_chunk_iteration=*/1, /*compute_is_deterministic=*/true,
                                               failure_stage_damage_functions_, ConsequenceType::Damage,
                                               /*save_annualized_result=*/false, /*save_freq_curves=*/true);
            std::vector<PairedData> damage_frequency_functions;
            damage_frequency_functions.reserve(
                impact_area_scenario_results_.consequence_frequency_functions().size());
            for (const auto& c : impact_area_scenario_results_.consequence_frequency_functions()) {
                damage_frequency_functions.push_back(c.frequency_curve());
            }
            // Default threshold represents a channel stage, because the stage damage functions are
            // in channel stage.
            Threshold default_threshold = compute_default_threshold(convergence_criteria, damage_frequency_functions);
            impact_area_scenario_results_.performance_by_thresholds().add_threshold(std::move(default_threshold));
        }

        create_histograms_for_assurance_of_thresholds();
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private Threshold
    // DetermineSystemResponseThreshold(ConvergenceCriteria convergenceCriteria)` (lines 187-199).
    // `_SystemResponseFunction.Xvals.Length <= 2` -> TopOfLevee, else LeveeSystemResponse.
    //
    // NOT a literal transcription of the object handed to `new Threshold(...)`: C# passes
    // `_SystemResponseFunction` itself (a reference type), so the resulting Threshold's
    // SystemPerformanceResults ends up ALIASING the exact same object the simulation keeps
    // sampling from every Monte Carlo iteration (Task 10's ComputeIterations loop, which runs
    // AFTER this method, in PopulateRandomNumbers/ComputeIterations). This port's UncertainPairedData
    // is move-only (unique_ptr<IDistribution> y-members, no clone()) -- aliasing isn't possible, and
    // moving `*system_response_function_` here would strand the simulation's own per-iteration
    // sampling with an empty curve. Instead, clone_system_response_function_for_threshold() (below)
    // builds an INDEPENDENT, value-equivalent UncertainPairedData for the Threshold's own copy,
    // leaving the simulation's system_response_function_ fully intact. See that helper's comment
    // for why the clone is provably exact for its only consumer
    // (SystemPerformanceResults::calculate_assurance_for_levee).
    Threshold determine_system_response_threshold(const ConvergenceCriteria& convergence_criteria) {
        ThresholdEnum threshold_enum = system_response_function_->xvals().size() <= 2
                                            ? ThresholdEnum::TopOfLevee
                                            : ThresholdEnum::LeveeSystemResponse;
        return Threshold(/*threshold_id=*/0, clone_system_response_function_for_threshold(), convergence_criteria,
                          threshold_enum, top_of_levee_elevation_);
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private PairedData
    // ComputeRiskFromStageFrequency(FrequencyStageCurves curves, long thisComputeIteration, long
    // thisChunkIteration, bool computeIsDeterministic)` (lines 461-490). Returns the sampled system
    // response function if a levee exists (nullopt otherwise, standing in for C#'s `return null`).
    // `_SystemResponseFunction.CurveMetaData.IsNull` maps to the established
    // `!has_value() || metadata().is_null()` gate (see populate_random_numbers()'s comment).
    std::optional<PairedData> compute_risk_from_stage_frequency(const FrequencyStageCurves& curves,
                                                                  long this_compute_iteration,
                                                                  long this_chunk_iteration,
                                                                  bool compute_is_deterministic) {
        bool no_levee = !system_response_function_.has_value() || system_response_function_->metadata().is_null();
        if (no_levee) {
            // Uses FloodplainStage, because we're adjusting the damage samples with these interior
            // stage values.
            if (has_failure_stage_damage_) {
                compute_risk_from_stage_frequency(curves.floodplain_stage, this_compute_iteration,
                                                   this_chunk_iteration, compute_is_deterministic,
                                                   failure_stage_damage_functions_, ConsequenceType::Damage);
            }
            if (has_failure_stage_life_loss_) {
                compute_risk_from_stage_frequency(curves.floodplain_stage, this_compute_iteration,
                                                   this_chunk_iteration, compute_is_deterministic,
                                                   failure_stage_life_loss_functions_, ConsequenceType::LifeLoss);
            }
            return std::nullopt;
        }
        PairedData system_response_sample =
            system_response_function_->sample_paired_data(this_compute_iteration, compute_is_deterministic);
        if (has_failure_stage_damage_) {
            compute_risk_from_stage_frequency_with_levee(curves, system_response_sample, this_compute_iteration,
                                                          this_chunk_iteration, compute_is_deterministic,
                                                          ConsequenceType::Damage);
        }
        if (has_failure_stage_life_loss_) {
            compute_risk_from_stage_frequency_with_levee(curves, system_response_sample, this_compute_iteration,
                                                          this_chunk_iteration, compute_is_deterministic,
                                                          ConsequenceType::LifeLoss);
        }
        return system_response_sample;
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private void
    // ComputePerformanceFromStageFrequency(PairedData channelStageFreq, PairedData
    // systemResponse_sample, int thisChunkIteration)` (lines 498-510). `systemResponse_sample ==
    // null` maps to `!system_response_sample.has_value()` (compute_risk_from_stage_frequency's
    // FrequencyStageCurves overload, above, returns nullopt in the no-levee case).
    void compute_performance_from_stage_frequency(const PairedData& channel_stage_freq,
                                                   const std::optional<PairedData>& system_response_sample,
                                                   int this_chunk_iteration) {
        // If there's no fragility curve, we can go a simpler path.
        if (!system_response_sample.has_value() || system_response_sample->xvals().size() <= 2) {
            compute_performance(channel_stage_freq, this_chunk_iteration);
        } else {
            // System response (levee fragility) is defined in channel stage, so use ChannelStage.
            compute_levee_performance(channel_stage_freq, *system_response_sample, this_chunk_iteration);
        }
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private void
    // ComputeRiskFromStageFrequency(PairedData frequency_stage, long thisComputeIteration, long
    // thisChunkIteration, bool computeIsDeterministic, List<UncertainPairedData>
    // consequenceFunctions, ConsequenceType consequenceType, bool saveAnnualizedResult = true, bool
    // saveFreqCurves = false)` (lines 524-551). The per-consequence-list risk compute: composes
    // each sampled stage-consequence curve onto `frequency_stage` (stage-damage compose
    // frequency-stage = damage-frequency), optionally stages the resulting curve into
    // ConsequenceFrequencyFunctions (used by compute_default_threshold), then (unless
    // save_annualized_result is false) feeds the uncertain consequence-frequency-curve accumulator
    // and integrates to an EAD realization.
    void compute_risk_from_stage_frequency(const PairedData& frequency_stage, long this_compute_iteration,
                                            long this_chunk_iteration, bool compute_is_deterministic,
                                            const std::vector<UncertainPairedData>& consequence_functions,
                                            ConsequenceType consequence_type, bool save_annualized_result = true,
                                            bool save_freq_curves = false) {
        for (const auto& stage_uncertain_consequences : consequence_functions) {
            PairedData stage_consequences_sample =
                stage_uncertain_consequences.sample_paired_data(this_compute_iteration, compute_is_deterministic);
            PairedData frequency_consequences = stage_consequences_sample.compose(frequency_stage);  // save me for FN Plot.
            if (save_freq_curves) {
                impact_area_scenario_results_.consequence_frequency_functions().emplace_back(
                    frequency_consequences, stage_uncertain_consequences.metadata().damage_category(),
                    stage_uncertain_consequences.metadata().asset_category(), consequence_type, RiskType::Fail);
            }
            if (!save_annualized_result) {
                continue;
            }
            // Add curve to uncertain consequence frequency curve for histogram aggregation.
            auto& uncertain_curve = impact_area_scenario_results_.get_or_create_uncertain_consequence_frequency_curve(
                frequency_consequences.xvals(), stage_uncertain_consequences.metadata().damage_category(),
                stage_uncertain_consequences.metadata().asset_category(), consequence_type, RiskType::Fail,
                convergence_criteria_);
            uncertain_curve.add_curve_realization(frequency_consequences, this_chunk_iteration);

            double ea_consequences_estimate = frequency_consequences.integrate();
            impact_area_scenario_results_.consequence_results().add_consequence_realization(
                ea_consequences_estimate, stage_uncertain_consequences.metadata().damage_category(),
                stage_uncertain_consequences.metadata().asset_category(), impact_area_id_, this_chunk_iteration,
                consequence_type, RiskType::Fail);
        }
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private void
    // ComputeRiskFromStageFrequency_WithLevee(FrequencyStageCurves curves, PairedData
    // systemResponse, long thisComputeIteration, long thisChunkIteration, bool
    // computeIsDeterministic, ConsequenceType type)` (lines 553-580).
    void compute_risk_from_stage_frequency_with_levee(const FrequencyStageCurves& curves,
                                                        const PairedData& system_response,
                                                        long this_compute_iteration, long this_chunk_iteration,
                                                        bool compute_is_deterministic, ConsequenceType type) {
        const std::vector<UncertainPairedData>* failure_stage_damage_functions;
        const std::vector<UncertainPairedData>* nonfailure_stage_damage_functions;
        // ConsequenceType can only be LifeLoss or Damage.
        if (type == ConsequenceType::LifeLoss) {
            failure_stage_damage_functions = &failure_stage_life_loss_functions_;
            nonfailure_stage_damage_functions = &non_failure_stage_life_loss_functions_;
        } else {
            failure_stage_damage_functions = &failure_stage_damage_functions_;
            nonfailure_stage_damage_functions = &non_failure_stage_damage_functions_;
        }

        PairedData validated_system_response = ensure_bottom_and_top_have_correct_probabilities(system_response);
        compute_annualized_consequence(curves, this_compute_iteration, this_chunk_iteration,
                                        compute_is_deterministic, type, *failure_stage_damage_functions,
                                        validated_system_response, RiskType::Fail);

        // If we have nonfail damage functions, compute those too.
        if (!nonfailure_stage_damage_functions->empty()) {
            PairedData complement_system_response = calculate_failure_prob_complement(validated_system_response);
            compute_annualized_consequence(curves, this_compute_iteration, this_chunk_iteration,
                                            compute_is_deterministic, type, *nonfailure_stage_damage_functions,
                                            complement_system_response, RiskType::Non_Fail);
        }
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private void
    // ComputeAnnualizedConsequence(FrequencyStageCurves curves, long thisComputeIteration, long
    // thisChunkIteration, bool computeIsDeterministic, ConsequenceType type,
    // List<UncertainPairedData> stageConsequenceFuncs, PairedData validatedSystemResponse, RiskType
    // riskType)` (lines 582-616). The C# branch dispatch is `ReferenceEquals(curves.ChannelStage,
    // curves.FloodplainStage)` -- reference-identity, true exactly when no interior/exterior
    // relationship exists (get_frequency_stage_sample() only ever returns two DISTINCT curves when
    // channel_stage_floodplain_stage_ is set; see FrequencyStageCurves's own class comment for why
    // this port's struct can't reproduce that identity check directly). This port re-derives the
    // same boolean from the SAME gate get_frequency_stage_sample() used to decide that in the first
    // place, rather than comparing curve contents.
    void compute_annualized_consequence(const FrequencyStageCurves& curves, long this_compute_iteration,
                                         long this_chunk_iteration, bool compute_is_deterministic,
                                         ConsequenceType type,
                                         const std::vector<UncertainPairedData>& stage_consequence_funcs,
                                         const PairedData& validated_system_response, RiskType risk_type) {
        bool has_interior_exterior =
            channel_stage_floodplain_stage_.has_value() && !channel_stage_floodplain_stage_->metadata().is_null();
        for (const auto& stage_uncertain_cons : stage_consequence_funcs) {
            PairedData stage_damage_sample =
                stage_uncertain_cons.sample_paired_data(this_compute_iteration, compute_is_deterministic);
            PairedData dam_freq = [&]() {
                if (!has_interior_exterior) {
                    // No interior-exterior: multiply in stage domain then compose (original behavior).
                    return stage_damage_sample.multiply(validated_system_response).compose(curves.floodplain_stage);
                }
                // Interior-exterior exists: damage uses floodplain stage, system response uses
                // channel stage. Compose both into frequency domain before multiplying so each
                // uses the correct stage domain.
                PairedData freq_damage = stage_damage_sample.compose(curves.floodplain_stage);
                PairedData freq_failure = validated_system_response.compose(curves.channel_stage);
                return freq_damage.multiply(freq_failure);
            }();

            // Add curve to uncertain consequence frequency curve for histogram aggregation.
            auto& uncertain_curve = impact_area_scenario_results_.get_or_create_uncertain_consequence_frequency_curve(
                dam_freq.xvals(), stage_uncertain_cons.metadata().damage_category(),
                stage_uncertain_cons.metadata().asset_category(), type, risk_type, convergence_criteria_);
            uncertain_curve.add_curve_realization(dam_freq, this_chunk_iteration);

            double ead_oraal = dam_freq.integrate();
            impact_area_scenario_results_.consequence_results().add_consequence_realization(
                ead_oraal, stage_uncertain_cons.metadata().damage_category(),
                stage_uncertain_cons.metadata().asset_category(), impact_area_id_, this_chunk_iteration, type,
                risk_type);
        }
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private static PairedData
    // CalculateFailureProbComplement(PairedData validatedSystemResponse)` (lines 618-627).
    static PairedData calculate_failure_prob_complement(const PairedData& validated_system_response) {
        std::vector<double> probability_of_non_failure(validated_system_response.yvals().size());
        for (std::size_t i = 0; i < probability_of_non_failure.size(); ++i) {
            probability_of_non_failure[i] = 1 - validated_system_response.yvals()[i];
        }
        return PairedData(validated_system_response.xvals(), std::move(probability_of_non_failure));
    }

    // ported from: ImpactAreaScenarioSimulation.cs `public void ComputePerformance(PairedData
    // frequency_stage, int thisChunkIteration)` (lines 634-643). `frequency_stage` should be
    // exterior (channel) stage frequency.
    void compute_performance(const PairedData& frequency_stage, int this_chunk_iteration) {
        for (auto& threshold_entry :
             impact_area_scenario_results_.performance_by_thresholds().list_of_thresholds()) {
            double threshold_value = threshold_entry.threshold_value();
            double aep = 1 - frequency_stage.f_inverse(threshold_value);
            threshold_entry.system_performance_results().add_aep_for_assurance(aep, this_chunk_iteration);
            get_stage_for_non_exceedance_probability(frequency_stage, threshold_entry, this_chunk_iteration);
        }
    }

    // ported from: ImpactAreaScenarioSimulation.cs `public void ComputeLeveePerformance(PairedData
    // frequency_stage, PairedData levee_curve_sample, int thisChunkIteration)` (lines 645-673).
    // FP-SENSITIVE (per the task brief): trapezoidal integration of probability-of-failure over the
    // stage-probability domain, with explicit below/above extrapolation terms. This method assumes
    // the levee fragility function spans the entire probability domain -- transcribed
    // statement-for-statement, including integration bounds/steps.
    void compute_levee_performance(const PairedData& frequency_stage, const PairedData& levee_curve_sample,
                                    int this_chunk_iteration) {
        PairedData levee_frequency_stage = levee_curve_sample.compose(frequency_stage);
        double aep = 0;
        // Extrapolate below.
        if (levee_frequency_stage.xvals()[0] != 0) {
            double initial_prob_of_stage_in_range = levee_frequency_stage.xvals()[0] - 0;
            double initial_prob_failure = (levee_frequency_stage.yvals()[0] + 0) / 2;
            aep += initial_prob_of_stage_in_range * initial_prob_failure;
        }
        // Within function range.
        for (std::size_t i = 1; i < levee_frequency_stage.xvals().size(); ++i) {
            double probability_of_stage_in_range =
                levee_frequency_stage.xvals()[i] - levee_frequency_stage.xvals()[i - 1];
            double average_prob_failure =
                (levee_frequency_stage.yvals()[i] + levee_frequency_stage.yvals()[i - 1]) / 2;
            aep += probability_of_stage_in_range * average_prob_failure;
        }
        // Extrapolate above.
        double final_prob_of_stage_in_range = 1 - levee_frequency_stage.xvals().back();
        double final_avg_prob_failure = levee_frequency_stage.yvals().back();
        aep += final_prob_of_stage_in_range * final_avg_prob_failure;
        for (auto& threshold_entry :
             impact_area_scenario_results_.performance_by_thresholds().list_of_thresholds()) {
            threshold_entry.system_performance_results().add_aep_for_assurance(aep, this_chunk_iteration);
            get_stage_for_non_exceedance_probability(frequency_stage, threshold_entry, this_chunk_iteration);
        }
    }

    // ported from: ImpactAreaScenarioSimulation.cs `public static void
    // GetStageForNonExceedanceProbability(PairedData frequency_stage, Threshold threshold, int
    // thisChunkIteration)` (lines 674-682). The ER-101 standard non-exceedance probabilities,
    // hard-coded verbatim (C#'s own `//TODO: Get rid of these hard coded doubles`, kept as-is).
    static void get_stage_for_non_exceedance_probability(const PairedData& frequency_stage, Threshold& threshold,
                                                           int this_chunk_iteration) {
        static const double kEr101RequiredNonExceedanceProbabilities[] = {.9, .96, .98, .99, .996, .998};
        for (double non_exceedance_probability : kEr101RequiredNonExceedanceProbabilities) {
            double stage_of_event = frequency_stage.f(non_exceedance_probability);
            threshold.system_performance_results().add_stage_for_assurance(non_exceedance_probability,
                                                                              stage_of_event, this_chunk_iteration);
        }
    }

    // ported from: ImpactAreaScenarioSimulation.cs `public void
    // CreateHistogramsForAssuranceOfThresholds()` (lines 683-694). Same ER-101 levels as
    // get_stage_for_non_exceedance_probability() above (independently hard-coded in C# too --
    // `//TODO: get rid of these hard-coded doubles`, kept as-is).
    void create_histograms_for_assurance_of_thresholds() {
        static const double kEr101RequiredNonExceedanceProbabilities[] = {.9, .96, .98, .99, .996, .998};
        for (auto& threshold_entry :
             impact_area_scenario_results_.performance_by_thresholds().list_of_thresholds()) {
            for (double probability : kEr101RequiredNonExceedanceProbabilities) {
                threshold_entry.system_performance_results().add_stage_assurance_histogram(probability);
            }
        }
    }

    // ported from: ImpactAreaScenarioSimulation.cs `internal static PairedData
    // ComputeTotalStageDamage(List<UncertainPairedData> failStageDamages)` (lines 695-706). Sums
    // every failure stage-damage function's deterministic representation via SumYsForGivenX.
    //
    // NOT a literal transcription of the C# accumulator seed: C# starts `totalStageDamage = new
    // PairedData(null, null, metadata)` -- a "null Xvals/Yvals" sentinel -- and relies on
    // PairedData.SumYsForGivenX's `if (Xvals == null || Yvals == null) return new
    // PairedData(input.Xvals.ToArray(), input.Yvals.ToArray())` early-return branch (verified
    // against the real C# source) to make the FIRST accumulation collapse to "just the first
    // sampled curve, copied". This port's PairedData ctor takes concrete std::vector<double> (never
    // a null pointer) and its sum_ys_for_given_x() has no such early-return branch (see
    // paired_data.hpp's own class comment: an empty-but-non-null curve throws instead) -- so a
    // literal "null Xvals" sentinel cannot be constructed here. This method instead reproduces the
    // OBSERVABLE effect directly: the first sampled curve seeds the accumulator as-is, and every
    // subsequent curve is folded in via the real sum_ys_for_given_x(). `throw`s if given no damage
    // functions at all (the C# empty-list case would build a fully-null PairedData that later
    // throws a NullReferenceException the first time compute_default_threshold's f_inverse touches
    // it; SetupPerformanceThresholds's own comment -- "Should only do this if we have damages" --
    // establishes this method's real caller contract never hits that case).
    static PairedData compute_total_stage_damage(const std::vector<UncertainPairedData>& fail_stage_damages) {
        if (fail_stage_damages.empty()) {
            throw std::runtime_error(
                "ImpactAreaScenarioSimulation::compute_total_stage_damage: no failure stage-damage "
                "functions (mirrors C#'s null-Xvals PairedData sentinel eventually throwing a "
                "NullReferenceException in f_inverse -- see this method's own comment).");
        }
        long iteration = 1;
        std::optional<PairedData> total_stage_damage;
        for (const auto& uncertain_paired_data : fail_stage_damages) {
            PairedData stage_damage_sample =
                uncertain_paired_data.sample_paired_data(iteration, /*retrieve_deterministic_representation=*/true);
            if (!total_stage_damage.has_value()) {
                total_stage_damage = std::move(stage_damage_sample);
            } else {
                total_stage_damage = total_stage_damage->sum_ys_for_given_x(stage_damage_sample);
            }
        }
        return *total_stage_damage;
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private Threshold
    // ComputeDefaultThreshold(ConvergenceCriteria convergenceCriteria, List<PairedData>
    // damageFrequencyFunctions)` (lines 707-722). `!_SystemResponseFunction.IsNull` (has-a-levee)
    // maps to the established `has_value() && !metadata().is_null()` gate.
    Threshold compute_default_threshold(const ConvergenceCriteria& convergence_criteria,
                                         const std::vector<PairedData>& damage_frequency_functions) {
        bool has_levee =
            system_response_function_.has_value() && !system_response_function_->metadata().is_null();
        if (has_levee) {
            throw std::runtime_error(
                "A default threshold cannot be calculated for an impact area with a levee.");
        }
        PairedData total_stage_damage = compute_total_stage_damage(failure_stage_damage_functions_);
        PairedData total_frequency_damage = damage_frequency_functions[0];
        for (std::size_t i = 1; i < damage_frequency_functions.size(); ++i) {
            total_frequency_damage = total_frequency_damage.sum_ys_for_given_x(damage_frequency_functions[i]);
        }
        double threshold_damage =
            kThresholdDamagePercent * total_frequency_damage.f(kThresholdDamageRecurrenceInterval);
        double threshold_stage = total_stage_damage.f_inverse(threshold_damage);
        return Threshold(kDefaultThresholdId, convergence_criteria, ThresholdEnum::DefaultExteriorStage,
                          threshold_stage);
    }

    // ported from: ImpactAreaScenarioSimulation.cs `private static PairedData
    // EnsureBottomAndTopHaveCorrectProbabilities(PairedData systemResponseFunction)` (lines
    // 737-774). Pads the fragility curve to span 0 -> 1 probability of failure if it doesn't
    // already (adds a `buffer`-offset point just below the first stage at prob 0, and one just
    // above the last stage at prob 1). Public here (see class comment) though private static in
    // C#, matching the rest of this task's access-relaxation convention.
    static PairedData ensure_bottom_and_top_have_correct_probabilities(const PairedData& system_response_function) {
        const std::vector<double>& yvals = system_response_function.yvals();
        bool system_response_is_complete = std::find(yvals.begin(), yvals.end(), 0.0) != yvals.end() &&
                                            std::find(yvals.begin(), yvals.end(), 1.0) != yvals.end();
        if (system_response_is_complete) {
            return system_response_function;
        }
        // Make the fragility function begin with 0 prob failure and end with 1 prob failure.
        std::vector<double> temp_xvals;  // xvals are stages
        std::vector<double> temp_yvals;  // yvals are prob failure

        const double buffer = .001;  // buffer to define point just above and just below the multiplying curve.

        double below_fragility_curve_value = 0.0;
        double stage_to_add_below_fragility = system_response_function.xvals().front() - buffer;
        temp_xvals.push_back(stage_to_add_below_fragility);
        temp_yvals.push_back(below_fragility_curve_value);

        for (std::size_t i = 0; i < system_response_function.xvals().size(); ++i) {
            temp_xvals.push_back(system_response_function.xvals()[i]);
            temp_yvals.push_back(system_response_function.yvals()[i]);
        }

        double above_fragility_curve_value = 1.0;
        double stage_to_add_above_fragility = system_response_function.xvals().back() + buffer;
        temp_xvals.push_back(stage_to_add_above_fragility);
        temp_yvals.push_back(above_fragility_curve_value);

        return PairedData(std::move(temp_xvals), std::move(temp_yvals));
    }

    // Exposed for testing/fixture-dispatch (no C# analog beyond what Compute() returns).
    ImpactAreaScenarioResults& results() { return impact_area_scenario_results_; }
    const ImpactAreaScenarioResults& results() const { return impact_area_scenario_results_; }

   private:
    // ============================================================================================
    // Phase 5 Task 10: the Monte Carlo iteration loop `compute()` drives.
    // ============================================================================================

    // ported from: ImpactAreaScenarioSimulation.cs `private void
    // ComputeIterations(ConvergenceCriteria convergenceCriteria, bool computeIsDeterministic,
    // CancellationToken cancellationToken)` (lines 330-403). The CancellationToken parameter is
    // dropped (see class comment's SEVERANCES note); `completedIterations`/`expectedIterations`
    // and the `ReportProgress` calls that consume them are dropped too (severed `IProgressReport`,
    // no observable effect once progress reporting is gone).
    //
    // `Parallel.For(0, iterationsPerComputeChunk, chunkIteration => {...})` becomes a plain serial
    // `for`: each `chunkIteration` samples strictly by `computeIteration = iterationsStart +
    // chunkIteration` (an explicit index into every seeded `DotNetRandom` stream via
    // `sample_paired_data(this_compute_iteration, ...)`), so which chunk-iteration runs first has
    // no effect on the result -- matches this port's repo-wide "no threading primitives"
    // convention (see class comment) and the identical `Parallel.For`->serial-`for` port already
    // established for `ImpactAreaScenarioResults::parallel_results_are_converged`.
    //
    // The AggregateException/TaskCanceledException catch block (C# lines 358-372) exists solely to
    // unwrap a `Parallel.For` cancellation exception -- with no CancellationToken and no
    // Parallel.For, there is nothing to catch; a real error from inside the loop body propagates
    // as a normal C++ exception straight out of `compute()`, same observable effect.
    //
    // FAITHFUL QUIRK (deliberately reproduced, not "fixed" -- see the class-comment-adjacent
    // faithful-bug list convention established elsewhere in this port): on a NON-convergent pass,
    // the outer `for (int i = 0; i < additional_chunks_needed; ++i)` loop restarts from `i = 0` on
    // every `while` iteration, even though `additional_chunks_needed` was just recomputed from
    // `remaining_iterations()`. Combined with `iterations_start = i * iterations_per_compute_chunk`,
    // this means every chunk from the FIRST while-pass gets resampled and re-accumulated into the
    // histograms again on the second while-pass (not just the newly-needed additional chunks) --
    // transcribed exactly as upstream wrote it. None of this task's DETERMINISTIC fixtures ever
    // take a second while-pass (`max_iterations=1` alone forces
    // `DynamicHistogram::is_histogram_converged`/`SystemPerformanceResults::
    // assurance_test_for_convergence` to report converged after the very first chunk, regardless
    // of variance -- see those methods' own `sample_size_ >= max_iterations()` branch), so this
    // quirk is inert for every oracle this task pins, but is preserved verbatim for future
    // non-deterministic/multi-chunk callers.
    void compute_iterations(const ConvergenceCriteria& convergence_criteria, bool compute_is_deterministic) {
        int iterations_per_compute_chunk = convergence_criteria.iteration_count();
        int additional_chunks_needed = static_cast<int>(
            std::ceil(static_cast<double>(convergence_criteria.min_iterations()) / iterations_per_compute_chunk));
        if (additional_chunks_needed < 1) {
            additional_chunks_needed = 1;
        }
        bool check_consequence_results = has_failure_stage_damage_ || has_failure_stage_life_loss_;
        bool compute_is_not_converged = true;
        while (compute_is_not_converged) {
            for (int i = 0; i < additional_chunks_needed; ++i) {
                long iterations_start = static_cast<long>(i) * iterations_per_compute_chunk;
                for (int chunk_iteration = 0; chunk_iteration < iterations_per_compute_chunk; ++chunk_iteration) {
                    long compute_iteration = iterations_start + chunk_iteration;
                    // Two curves come back for frequency stage: channel and floodplain. They're
                    // both the same object's value if there is no interior-exterior relationship.
                    FrequencyStageCurves curves = get_frequency_stage_sample(compute_is_deterministic, compute_iteration);
                    // Both curves go into the risk compute to use with a levee (levee should
                    // always be channel stage, not interior, but damage should use interior).
                    std::optional<PairedData> system_response_sample = compute_risk_from_stage_frequency(
                        curves, compute_iteration, chunk_iteration, compute_is_deterministic);
                    // std::nullopt checks the system response, matching `systemResponse_sample ==
                    // null`.
                    compute_performance_from_stage_frequency(curves.channel_stage, system_response_sample,
                                                              chunk_iteration);
                }
                impact_area_scenario_results_.consequence_results().put_data_into_histograms();
                impact_area_scenario_results_.put_uncertain_frequency_curves_into_histograms();
                for (Threshold& threshold_entry :
                     impact_area_scenario_results_.performance_by_thresholds().list_of_thresholds()) {
                    threshold_entry.system_performance_results().put_data_into_histograms();
                }
            }
            if (!impact_area_scenario_results_.results_are_converged(.95, .05, check_consequence_results)) {
                // Recalculate compute chunks -- see the FAITHFUL QUIRK note above: this resets
                // `additional_chunks_needed`, but the outer `for` above still restarts at `i = 0`.
                std::int64_t additional_iterations = impact_area_scenario_results_.remaining_iterations(
                    .95, .05, check_consequence_results);
                additional_chunks_needed = static_cast<int>(additional_iterations / iterations_per_compute_chunk);
                if (additional_chunks_needed == 0) {
                    additional_chunks_needed = 1;
                }
            } else {
                compute_is_not_converged = false;
                break;
            }
        }
    }

    // ported from: the `frequencyDischarge` local-variable computation inside
    // GetFrequencyStageSample (lines 410-419), factored into its own method: C++'s PairedData has
    // no default constructor, so the C#'s "declare a reference-typed local, assign it across an
    // if/else" pattern has no direct transcription here (the ternary in
    // get_frequency_stage_sample() needs a single expression to initialize
    // frequency_stage_sample). analytical (`_FrequencyDischargeGraphical.CurveMetaData.IsNull` ==
    // true) uses bootstrap_to_paired_data over the fixed 173-point exceedance-probability grid;
    // graphical uses GraphicalUncertainPairedData::sample_paired_data directly.
    hecfda::model::paired_data::PairedData sample_frequency_discharge(bool compute_is_deterministic,
                                                                        long this_compute_iteration) {
        if (frequency_discharge_graphical_.curve_meta_data().is_null()) {
            return hecfda::statistics::distributions::bootstrap_to_paired_data(
                *frequency_discharge_, this_compute_iteration,
                hecfda::statistics::distributions::required_exceedance_probabilities(), compute_is_deterministic);
        }
        return frequency_discharge_graphical_.sample_paired_data(this_compute_iteration, compute_is_deterministic);
    }

    // Not present in the C# source (see determine_system_response_threshold()'s comment): builds an
    // INDEPENDENT UncertainPairedData standing in for C#'s reference-shared `_SystemResponseFunction`
    // (a single object aliased by both the simulation and the Threshold built from it). This port's
    // UncertainPairedData is move-only with no clone() (unique_ptr<IDistribution> y-members), so
    // literal aliasing isn't possible. Instead this reconstructs a value-equivalent curve using
    // Deterministic y-distributions, each fixed at the ORIGINAL curve's sample_paired_data(0.5)
    // value (median / InverseCDF(0.5), post monotonicity-forcing) -- provably exact for
    // system_performance_results.hpp's ONLY consumer of this field,
    // `SystemPerformanceResults::calculate_assurance_for_levee()`, which calls
    // `system_response_function_->sample_paired_data(0.5)` and NOTHING else (never an
    // iteration-indexed sample, never any other probability): re-sampling a Deterministic(v) curve
    // at probability 0.5 (or any probability) returns exactly `v`, so this clone's
    // sample_paired_data(0.5) reproduces the original's bit-for-bit. The simulation's own
    // system_response_function_ is left fully untouched (same xvals/ys/random_numbers_) for the
    // per-iteration levee sampling Task 10's compute loop still needs from it.
    UncertainPairedData clone_system_response_function_for_threshold() const {
        using hecfda::statistics::distributions::Deterministic;
        using hecfda::statistics::distributions::IDistribution;
        PairedData median = system_response_function_->sample_paired_data(0.5);
        std::vector<std::unique_ptr<IDistribution>> ys;
        ys.reserve(median.yvals().size());
        for (double y : median.yvals()) {
            ys.push_back(std::make_unique<Deterministic>(y));
        }
        return UncertainPairedData(median.xvals(), std::move(ys), system_response_function_->metadata());
    }

    // ported from: ImpactAreaScenarioSimulation.cs `internal ImpactAreaScenarioSimulation(int
    // impactAreaID)`. C#'s `internal` (assembly-visibility) ctor access is reproduced here as
    // `private` reachable only via the `builder()` static factory (mirrors
    // `OccupancyType`'s private-ctor-plus-static-`builder()` precedent) -- there is no direct C++
    // analog of `internal`, and `private` + a same-class static factory is the closest match.
    explicit ImpactAreaScenarioSimulation(int impact_area_id)
        : frequency_discharge_(nullptr),
          frequency_discharge_graphical_(),
          unregulated_regulated_(std::nullopt),
          discharge_stage_(std::nullopt),
          frequency_stage_(),
          channel_stage_floodplain_stage_(std::nullopt),
          system_response_function_(std::nullopt),
          top_of_levee_elevation_(0.0),
          has_failure_stage_damage_(false),
          has_failure_stage_life_loss_(false),
          has_non_failure_stage_life_loss_(false),
          failure_stage_damage_functions_(),
          non_failure_stage_damage_functions_(),
          failure_stage_life_loss_functions_(),
          non_failure_stage_life_loss_functions_(),
          impact_area_id_(impact_area_id),
          impact_area_scenario_results_(impact_area_id),
          has_non_failure_stage_damage_(false),
          convergence_criteria_() {}

    // ported from: ImpactAreaScenarioSimulation.cs `private void
    // CreateEAConsequenceHistograms(ConvergenceCriteria convergenceCriteria,
    // List<UncertainPairedData> stageConsequenceFunctions, ConsequenceType consequenceType,
    // RiskType riskType)`.
    void create_ea_consequence_histograms(const ConvergenceCriteria& convergence_criteria,
                                           const std::vector<UncertainPairedData>& stage_consequence_functions,
                                           ConsequenceType consequence_type, RiskType risk_type) {
        for (const auto& stage_consequence : stage_consequence_functions) {
            impact_area_scenario_results_.consequence_results().add_new_consequence_result_object(
                stage_consequence.metadata().damage_category(), stage_consequence.metadata().asset_category(),
                convergence_criteria, impact_area_id_, consequence_type, risk_type);
        }
    }

    // ported from: ImpactAreaScenarioSimulation.cs field block (lines 34-40).
    static constexpr int kFrequencySeed = 1234;
    static constexpr int kFlowRegulationSeed = 2345;
    static constexpr int kStageFlowSeed = 3456;
    static constexpr int kExteriorInteriorSeed = 4567;
    static constexpr int kSystemResponseSeed = 5678;
    static constexpr int kStageDamageSeed = 6789;
    static constexpr int kStageLifeLossSeed = 7891;
    // ported from: ImpactAreaScenarioSimulation.cs `private const double
    // THRESHOLD_DAMAGE_PERCENT = 0.05` / `THRESHOLD_DAMAGE_RECURRENCE_INTERVAL = 0.99`. Consumed
    // by `compute_default_threshold()` (Task 9).
    static constexpr double kThresholdDamagePercent = 0.05;
    static constexpr double kThresholdDamageRecurrenceInterval = 0.99;

    // ported from: ImpactAreaScenarioSimulation.cs field block (lines 41-60). Nullable
    // UncertainPairedData fields (C# `= new UncertainPairedData(); //defaults to null`) become
    // `std::optional<UncertainPairedData>` -- UncertainPairedData has no default/null-sentinel
    // ctor in this port (see uncertain_paired_data.hpp), matching the established
    // `system_performance_results.hpp` precedent for the same situation.
    // `_FrequencyDischarge` (nullable `ContinuousDistribution`, abstract in this port) becomes
    // `std::unique_ptr<ContinuousDistribution>`, defaulting to nullptr.
    std::unique_ptr<ContinuousDistribution> frequency_discharge_;
    GraphicalUncertainPairedData frequency_discharge_graphical_;
    std::optional<UncertainPairedData> unregulated_regulated_;
    std::optional<UncertainPairedData> discharge_stage_;
    GraphicalUncertainPairedData frequency_stage_;
    std::optional<UncertainPairedData> channel_stage_floodplain_stage_;
    std::optional<UncertainPairedData> system_response_function_;
    double top_of_levee_elevation_;
    bool has_failure_stage_damage_;
    bool has_failure_stage_life_loss_;
    bool has_non_failure_stage_life_loss_;
    std::vector<UncertainPairedData> failure_stage_damage_functions_;
    std::vector<UncertainPairedData> non_failure_stage_damage_functions_;
    std::vector<UncertainPairedData> failure_stage_life_loss_functions_;
    std::vector<UncertainPairedData> non_failure_stage_life_loss_functions_;
    int impact_area_id_;
    ImpactAreaScenarioResults impact_area_scenario_results_;
    bool has_non_failure_stage_damage_;
    // ported from: ImpactAreaScenarioSimulation.cs `private ConvergenceCriteria
    // _ConvergenceCriteria` -- unset (C# null) until Compute() runs; ConvergenceCriteria's
    // all-default-argument ctor stands in for that "unset" state (no optional needed).
    ConvergenceCriteria convergence_criteria_;
};

// ported from: ImpactAreaScenarioSimulation.cs `public class SimulationBuilder` (nested). Move-only
// (holds an ImpactAreaScenarioSimulation by value -- see that class's own move-only rationale).
// Fluent methods return `SimulationBuilder&&` from `std::move(*this)` rather than constructing a
// new builder per call (C#'s `return new SimulationBuilder(_Simulation)` wraps the SAME
// `_Simulation` reference each time -- the reference-type equivalent of this port's "same object,
// stays at the same address for the whole chain, moved out exactly once by build()" approach).
// Mirrors OccupancyType::OccupancyTypeBuilder's established pattern exactly.
class ImpactAreaScenarioSimulation::SimulationBuilder {
   public:
    explicit SimulationBuilder(ImpactAreaScenarioSimulation simulation) : simulation_(std::move(simulation)) {}

    // ported from: SimulationBuilder.cs `public ImpactAreaScenarioSimulation Build()`. Calls the
    // inherited `Validation::validate()` (C#'s `_Simulation.Validate()`, resolved to the
    // `Validation` base since `ImpactAreaScenarioSimulation` has no override) BEFORE moving
    // `simulation_` out -- every registered rule predicate runs while `simulation_` is still at
    // its builder-owned, stable address, so `error_level_`/`errors_` (plain value members) are
    // fully computed and simply carry along correctly through the move. `can_compute()` only
    // READS `error_level()` afterward; it never re-invokes `validate()`, so none of the rule
    // closures registered below are ever called again post-move (see `with_flow_frequency`'s
    // comment for why that matters).
    ImpactAreaScenarioSimulation build() {
        simulation_.validate();
        return std::move(simulation_);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder
    // WithFlowFrequency(ContinuousDistribution continuousDistribution)`. `ContinuousDistribution`
    // is abstract in this port (concrete subtypes: Normal/Uniform/.../Deterministic), so the C#
    // reference parameter becomes `std::unique_ptr<ContinuousDistribution>`.
    //
    // The C# rule predicate is a LAZY closure (`() => { ...Validate(); return !...HasErrors; }`)
    // capturing `_Simulation` and re-run whenever `Validate()` walks the RuleMap. A literal
    // transcription here would need to capture a pointer into `simulation_.frequency_discharge_`
    // -- unsafe, because `build()` MOVES `simulation_` out (see its comment), which would leave
    // any captured pointer dangling for a later `validate()` call. Since `ContinuousDistribution`'s
    // fields never change after construction in this port (no setters), an EAGER call --
    // `validate()`/`has_errors()` invoked once, right here, with the resulting bool captured BY
    // VALUE (no pointer, no `this`) -- is observably identical to the lazy C# version while being
    // safe across every subsequent move. `errors()` (this port's `GetErrors()` analog) is joined
    // into the rule's message string eagerly too, for the same reason.
    SimulationBuilder&& with_flow_frequency(std::unique_ptr<ContinuousDistribution> continuous_distribution) {
        continuous_distribution->validate();
        bool is_valid = !continuous_distribution->has_errors();
        std::string message;
        if (!is_valid) {
            const auto& errors = continuous_distribution->errors();
            for (std::size_t i = 0; i < errors.size(); ++i) {
                if (i != 0) message += "\n";
                message += errors[i];
            }
        }
        simulation_.frequency_discharge_ = std::move(continuous_distribution);
        simulation_.add_single_property_rule(
            "flow frequency", [is_valid]() { return is_valid; }, message,
            hecfda::statistics::ErrorLevel::Info);
        return std::move(*this);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder
    // WithFlowFrequency(GraphicalUncertainPairedData graphicalUncertainPairedData)`. No rule
    // registered in C# either.
    SimulationBuilder&& with_flow_frequency(GraphicalUncertainPairedData graphical_uncertain_paired_data) {
        simulation_.frequency_discharge_graphical_ = std::move(graphical_uncertain_paired_data);
        return std::move(*this);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder
    // WithInflowOutflow(UncertainPairedData uncertainPairedData)`. The C# "inflow outflow" rule
    // is SEVERED -- see class comment (UncertainPairedData has no validate()/has_errors() surface
    // in this port).
    SimulationBuilder&& with_inflow_outflow(UncertainPairedData uncertain_paired_data) {
        simulation_.unregulated_regulated_ = std::move(uncertain_paired_data);
        return std::move(*this);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder
    // WithFlowStage(UncertainPairedData uncertainPairedData)`. Both the "flow stage" HasErrors
    // rule and the "stage range" Fatal rule are SEVERED -- see class comment.
    SimulationBuilder&& with_flow_stage(UncertainPairedData uncertain_paired_data) {
        simulation_.discharge_stage_ = std::move(uncertain_paired_data);
        return std::move(*this);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder
    // WithFrequencyStage(GraphicalUncertainPairedData graphicalUncertainPairedData)`. The C#
    // "frequency_stage" rule is SEVERED -- see class comment.
    SimulationBuilder&& with_frequency_stage(GraphicalUncertainPairedData graphical_uncertain_paired_data) {
        simulation_.frequency_stage_ = std::move(graphical_uncertain_paired_data);
        return std::move(*this);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder
    // WithInteriorExterior(UncertainPairedData uncertainPairedData)`. The C#
    // "channelstage_floodplainstage" rule is SEVERED -- see class comment.
    SimulationBuilder&& with_interior_exterior(UncertainPairedData uncertain_paired_data) {
        simulation_.channel_stage_floodplain_stage_ = std::move(uncertain_paired_data);
        return std::move(*this);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder WithLevee(UncertainPairedData
    // uncertainPairedData, double topOfLeveeElevation)`. No rule registered in C# either.
    SimulationBuilder&& with_levee(UncertainPairedData uncertain_paired_data, double top_of_levee_elevation) {
        simulation_.system_response_function_ = std::move(uncertain_paired_data);
        simulation_.top_of_levee_elevation_ = top_of_levee_elevation;
        return std::move(*this);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder
    // WithStageDamages(List<UncertainPairedData> uncertainPairedDataList)`. The per-item
    // "<DamageCategory> stage damages" rules are SEVERED -- see class comment.
    SimulationBuilder&& with_stage_damages(std::vector<UncertainPairedData> uncertain_paired_data_list) {
        simulation_.failure_stage_damage_functions_ = std::move(uncertain_paired_data_list);
        simulation_.has_failure_stage_damage_ = true;
        return std::move(*this);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder
    // WithStageLifeLoss(List<UncertainPairedData> uncertainPairedDataList)`. The per-item "stage
    // life loss" rules are SEVERED -- see class comment.
    SimulationBuilder&& with_stage_life_loss(std::vector<UncertainPairedData> uncertain_paired_data_list) {
        simulation_.failure_stage_life_loss_functions_ = std::move(uncertain_paired_data_list);
        simulation_.has_failure_stage_life_loss_ = true;
        return std::move(*this);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder
    // WithNonFailureStageLifeLoss(List<UncertainPairedData> uncertainPairedDataList)`. The
    // per-item "stage life loss" rules are SEVERED -- see class comment.
    SimulationBuilder&& with_non_failure_stage_life_loss(std::vector<UncertainPairedData> uncertain_paired_data_list) {
        simulation_.non_failure_stage_life_loss_functions_ = std::move(uncertain_paired_data_list);
        simulation_.has_non_failure_stage_life_loss_ = true;
        return std::move(*this);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder
    // WithAdditionalThreshold(Threshold threshold)`.
    SimulationBuilder&& with_additional_threshold(Threshold threshold) {
        simulation_.impact_area_scenario_results_.performance_by_thresholds().add_threshold(std::move(threshold));
        return std::move(*this);
    }

    // ported from: SimulationBuilder.cs `public SimulationBuilder
    // WithNonFailureStageDamage(List<UncertainPairedData> stageDamageFunctions)`. No rule
    // registered in C# either (matches: this is the one WithX(List<UncertainPairedData>)
    // overload that never added per-item rules upstream).
    SimulationBuilder&& with_non_failure_stage_damage(std::vector<UncertainPairedData> stage_damage_functions) {
        simulation_.non_failure_stage_damage_functions_ = std::move(stage_damage_functions);
        simulation_.has_non_failure_stage_damage_ = true;
        return std::move(*this);
    }

   private:
    ImpactAreaScenarioSimulation simulation_;
};

inline ImpactAreaScenarioSimulation::SimulationBuilder ImpactAreaScenarioSimulation::builder(int impact_area_id) {
    return SimulationBuilder(ImpactAreaScenarioSimulation(impact_area_id));
}

}  // namespace compute
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_COMPUTE_IMPACT_AREA_SCENARIO_SIMULATION_HPP
