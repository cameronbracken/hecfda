// ported from: HEC.FDA.Model/scenarios/Scenario.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_SCENARIOS_SCENARIO_HPP
#define HECFDA_MODEL_SCENARIOS_SCENARIO_HPP
#include <utility>
#include <vector>
#include "hecfda/model/compute/impact_area_scenario_simulation.hpp"
#include "hecfda/model/metrics/scenario_results.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
namespace hecfda {
namespace model {
namespace scenarios {

// ported from: Scenario.cs `public class Scenario : IReportMessage` (51 lines) -- the top-level
// fan-out over `ImpactAreaScenarioSimulation` (Phase 5) that produces a `ScenarioResults` (Task
// 5). One level above the Phase-5 EAD engine: `compute()` just loops every impact area's own
// `ImpactAreaScenarioSimulation::compute()` and folds each result into a fresh `ScenarioResults`
// via `add_results()`. `Alternative::annualization_compute` (Task 9, not yet ported) is the
// eventual consumer of the `ScenarioResults` this produces (one base-year instance, one
// future-year instance).
//
// Move-only: holds `std::vector<ImpactAreaScenarioSimulation>`, and `ImpactAreaScenarioSimulation`
// is itself move-only (see that header's class comment: it holds `GraphicalUncertainPairedData`/
// `UncertainPairedData`/`ImpactAreaScenarioResults` members, none copyable) -- matches the
// established "fresh construction per use" convention already applied to every Phase 5/6
// Model-layer container in this port. C#'s `IList<ImpactAreaScenarioSimulation>` ctor parameter is
// a reference type -- the C# `_impactAreaSimulations` field ends up ALIASING the exact list object
// the caller passed in. This port's ctor takes `std::vector<ImpactAreaScenarioSimulation>` BY
// VALUE and moves it into the member (the same "moved-into-member" pattern
// `ScenarioStageDamage`'s ctor already establishes for its own move-only
// `std::vector<ImpactAreaStageDamage>` parameter): a caller that wants the vector to keep living
// simply doesn't move from it before construction, and no aliasing-vs-copy distinction is
// observable from `compute()`'s own behavior either way (only one caller in this port -- fixture
// dispatch/tests -- ever constructs a `Scenario`, and none of them touch the vector after handing
// it to the ctor).
//
// SEVERANCES (present in the C# file, deliberately NOT ported):
//  - `IReportMessage`/`event MessageReportedEventHandler MessageReport`/`ReportMessage(object,
//    MessageEventArgs)`: MVVM messaging, no analog (repo-wide messaging severance, see e.g.
//    `ImpactAreaScenarioSimulation`'s own class comment).
//  - `internal Scenario()` (empty-list default ctor): C#'s own doc context marks this
//    `internal`-only; every real caller (`ScenarioProgressManager`, `AlternativeTest`,
//    `AlternativeComparisonReportTests`, `ScenarioShould`, `DefaultDataComputeOutcomes`) uses the
//    public `Scenario(IList<ImpactAreaScenarioSimulation>)` ctor exclusively (verified against
//    upstream), so there is no observable behavior to reproduce here; adding an empty-vector
//    default ctor with no real caller would be speculative generality (YAGNI).
//  - `Compute(ConvergenceCriteria, CancellationToken, bool)` (the 3-arg overload) +
//    `System.Threading.CancellationToken`: this port's `ImpactAreaScenarioSimulation::compute()`
//    (Phase 5 Task 10) already dropped its own `CancellationToken` parameter for the identical
//    repo-wide "no threading primitives" reason -- the 2-arg `compute()` below inlines the 3-arg
//    overload's body directly, exactly mirroring how `ImpactAreaScenarioSimulation::compute()`
//    itself inlines ITS 3-arg overload.
//  - `scenarioResults.ComputeDate = DateTime.Now.ToString("G")` /
//    `scenarioResults.SoftwareVersion = Assembly.GetExecutingAssembly().GetName().Version.ToString()`:
//    wall-clock timestamp + reflection-read assembly version, both non-deterministic/environment-
//    dependent and explicitly called out as never-to-be-stamped by `ScenarioResults`'s own class
//    comment (see `scenario_results.hpp`: "never add a `DateTime`/`Assembly`-reading default to
//    these two fields when `Scenario::compute` is ported later"). `compute_date()`/
//    `software_version()` are left at their default-constructed empty `std::string` here,
//    matching that documented invariant exactly -- a caller that wants a timestamp can call
//    `set_compute_date`/`set_software_version` itself.
class Scenario {
   public:
    using ImpactAreaScenarioSimulation = hecfda::model::compute::ImpactAreaScenarioSimulation;
    using ScenarioResults = hecfda::model::metrics::ScenarioResults;
    using ConvergenceCriteria = hecfda::statistics::ConvergenceCriteria;

    // ported from: Scenario.cs `public Scenario(IList<ImpactAreaScenarioSimulation>
    // impactAreaSimulations)`. See class comment: moved into the member (by-value parameter),
    // rather than C#'s reference-aliasing assignment.
    explicit Scenario(std::vector<ImpactAreaScenarioSimulation> impact_area_simulations)
        : impact_area_simulations_(std::move(impact_area_simulations)) {}

    // Move-only (see class comment).
    Scenario(Scenario&&) = default;
    Scenario& operator=(Scenario&&) = default;
    Scenario(const Scenario&) = delete;
    Scenario& operator=(const Scenario&) = delete;

    // ported from: Scenario.cs `public ScenarioResults Compute(ConvergenceCriteria
    // convergenceCriteria, bool computeIsDeterministic = false)`, with the 3-arg
    // `Compute(ConvergenceCriteria, CancellationToken, bool)` overload's body inlined directly
    // (see class comment's SEVERANCES note -- CancellationToken dropped, no Parallel/threading
    // surface here to begin with since the fan-out loop itself was always serial in C#). The loop
    // -- construct a fresh `ScenarioResults`, call each impact area's own `compute()`, fold the
    // result in via `add_results()` -- is transcribed verbatim; the `ComputeDate`/`SoftwareVersion`
    // stamp that followed it in C# is severed (see class comment).
    ScenarioResults compute(ConvergenceCriteria convergence_criteria, bool compute_is_deterministic = false) {
        ScenarioResults scenario_results;
        for (ImpactAreaScenarioSimulation& impact_area : impact_area_simulations_) {
            auto result = impact_area.compute(convergence_criteria, compute_is_deterministic);
            scenario_results.add_results(std::move(result));
        }
        return scenario_results;
    }

    // ported from: Scenario.cs's implicit `_impactAreaSimulations` field -- no public getter in
    // C#, but exposed here (const ref) matching the established access-relaxation convention (see
    // e.g. `ImpactAreaScenarioSimulation::results()`) so fixture dispatch/tests can inspect the
    // held simulations without a full `compute()` pass.
    const std::vector<ImpactAreaScenarioSimulation>& impact_area_simulations() const {
        return impact_area_simulations_;
    }

   private:
    std::vector<ImpactAreaScenarioSimulation> impact_area_simulations_;
};

}  // namespace scenarios
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_SCENARIOS_SCENARIO_HPP
