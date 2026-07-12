// ported from: HEC.FDA.Model/compute/ImpactAreaScenarioSimulation.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_COMPUTE_IMPACT_AREA_SCENARIO_SIMULATION_HPP
#define HECFDA_MODEL_COMPUTE_IMPACT_AREA_SCENARIO_SIMULATION_HPP
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/metrics/impact_area_scenario_results.hpp"
#include "hecfda/model/metrics/threshold.hpp"
#include "hecfda/model/paired_data/graphical_uncertain_paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/validation.hpp"
namespace hecfda {
namespace model {
namespace compute {

// ported from: ImpactAreaScenarioSimulation.cs `public class ImpactAreaScenarioSimulation :
// ValidationErrorLogger, IProgressReport` -- the EAD (expected annual damage) Monte Carlo compute
// engine. Phase 5 Task 7 ports the SKELETON: fields, the seed constants, the fluent
// `SimulationBuilder`, `can_compute`/the inherited `validate()` gate, and
// `initialize_consequence_histograms`. `compute()`'s actual Monte Carlo iteration loop is a STUB
// (throws) -- Phase 5 Tasks 8-11 fill it in.
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
//    layer in this port.
//  - `ReportMessage`/`ErrorMessage`/`MessageEventArgs`/`ComputeCompleteMessage`/
//    `FrequencyDamageMessage`: MVVM messaging, no analog (repo-wide messaging severance).
//  - `[StoredProperty("ImpactAreaScenarioSimulation")]`: reflection-driven serialization metadata,
//    no analog (repo-wide `[StoredProperty]` severance).
//  - `System.Threading`/`System.Threading.Tasks` (`CancellationToken`, the `Compute(criteria,
//    cancellationToken, ...)` overload, `TaskCanceledException`): this port's Monte Carlo loop
//    (Tasks 8-11) is serial, matching the repo-wide "no threading primitives" convention already
//    established for `ImpactAreaScenarioResults::get_or_create_uncertain_consequence_frequency_curve`
//    (its own header documents the same choice for the `_uncertainCurveLock` C# field). Only the
//    2-arg `Compute(ConvergenceCriteria, bool)` overload ("this code path currently only used by
//    tests" per the C# doc comment) is ported; the 3-arg cancellation-token overload is dropped
//    entirely.
//  - `LogSimulationPropertyRuleErrors()`: builds intro-message strings solely for the severed
//    `ReportMessage` call -- no observable effect once messaging is gone, so dropped rather than
//    kept as a silent no-op.
//  - Everything downstream of the `can_compute` gate inside `Compute()` -- `SetupPerformanceThresholds`,
//    `PopulateRandomNumbers`, `ComputeIterations` (the actual Monte Carlo loop),
//    `DetermineSystemResponseThreshold`, `EnsureBottomAndTopHaveCorrectProbabilities`,
//    `CreateHistogramsForAssuranceOfThresholds`, `GetFrequencyStageSample`,
//    `ComputeRiskFromStageFrequency`, `ComputeDefaultThreshold` -- is Phase 5 Tasks 8-11's job.
//    None of them are declared here (not even as stubs): nothing in this task's surface
//    (`SimulationBuilder`, `can_compute`, `initialize_consequence_histograms`) calls them, and
//    `compute()`'s success path throws immediately after `initialize_consequence_histograms`
//    (see that method's comment) rather than call into not-yet-existing methods.
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
    using ConsequenceType = hecfda::model::metrics::ConsequenceType;
    using RiskType = hecfda::model::metrics::RiskType;

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
    // "This code path currently only used by tests" per the C# doc comment; the 3-arg
    // CancellationToken overload it delegates to in C# is not ported (see class comment).
    ImpactAreaScenarioResults compute(ConvergenceCriteria convergence_criteria,
                                       bool compute_is_deterministic = false) {
        (void)compute_is_deterministic;  // consumed only by the not-yet-ported Monte Carlo loop
        if (!can_compute(convergence_criteria)) {
            impact_area_scenario_results_ = ImpactAreaScenarioResults(impact_area_id_, /*is_null=*/true);
            return std::move(impact_area_scenario_results_);
        }
        convergence_criteria_ = convergence_criteria;
        initialize_consequence_histograms(convergence_criteria_);
        // STUB (Phase 5 Task 7 scope boundary): SetupPerformanceThresholds, PopulateRandomNumbers,
        // and the Monte Carlo ComputeIterations loop are Tasks 8-11's job (see class comment).
        // Throwing here -- rather than returning a half-initialized ImpactAreaScenarioResults that
        // looks complete but isn't -- makes the boundary loud.
        throw std::logic_error(
            "ImpactAreaScenarioSimulation::compute: SetupPerformanceThresholds and the Monte Carlo "
            "iteration loop are not implemented until Phase 5 Tasks 8-11");
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

    // Exposed for testing/fixture-dispatch (no C# analog beyond what Compute() returns).
    ImpactAreaScenarioResults& results() { return impact_area_scenario_results_; }
    const ImpactAreaScenarioResults& results() const { return impact_area_scenario_results_; }

   private:
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
    // THRESHOLD_DAMAGE_PERCENT = 0.05` / `THRESHOLD_DAMAGE_RECURRENCE_INTERVAL = 0.99`. Unused
    // until Tasks 8-11 wire SetupPerformanceThresholds/ComputeDefaultThreshold; kept here (rather
    // than deferred) since they are simple constants, not methods, and cost nothing to declare
    // now alongside the rest of the field block they belong to.
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
