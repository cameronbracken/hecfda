// ported from: upstream/HEC-FDA/HEC.FDA.Model/stageDamage/ScenarioStageDamage.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_STAGE_DAMAGE_SCENARIO_STAGE_DAMAGE_HPP
#define HECFDA_MODEL_STAGE_DAMAGE_SCENARIO_STAGE_DAMAGE_HPP
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/model/stage_damage/impact_area_stage_damage.hpp"
namespace hecfda {
namespace model {
namespace stage_damage {

// ported from: ScenarioStageDamage.cs. The thin outer loop of the stage-damage compute hierarchy
// (per the C# class doc comment on Compute(): Scenario SD <- Impact Area SD <- Damage Category <-
// Compute Chunk <- Iteration <- Structure <- W.S. Profile) -- loops
// impact_area_stage_damages_ and AddRange's (move-appends) each ImpactAreaStageDamage::compute()
// result pair into the scenario's own result pair. This is Phase 4 Task 8; it consumes Task 7's
// ImpactAreaStageDamage::compute() without adding any new numerical logic of its own.
//
// SEVERANCES (present in the C# file, deliberately NOT ported):
//   - ProduceStructureDetails(Dictionary<int,string>): CSV text-report generation, delegating to
//     ImpactAreaStageDamage::ProduceImpactAreaStructureDetails, which is itself CSV-severed (see
//     impact_area_stage_damage.hpp's class comment). Text/CSV formatting is out of scope
//     repo-wide per CLAUDE.md.
//   - GetErrorMessages(): optional per the task brief, not ported. has_errors()/
//     get_errors_from_properties() are already public on each ImpactAreaStageDamage (Task 7); a
//     caller needing the aggregated error-message list can trivially loop
//     impact_area_stage_damages() itself, so a redundant one-line wrapper here would be pure
//     speculative generality (YAGNI).
//   - `ProgressReporter reporter = null` parameter + the `reporter ??= ProgressReporter.None()`/
//     `Stopwatch sw`/`SubTask`/`ReportMessage`/`ReportTimestampedMessage`/`ReportProgress`/
//     `ReportProgressFraction`/`ReportTaskCompleted` calls threaded through Compute(): MVVM/UI
//     progress plumbing, dropped -- the same severance already applied to every other
//     Monte-Carlo-loop task in this port (e.g. ImpactAreaStageDamage::compute()).
class ScenarioStageDamage {
   public:
    using UncertainPairedData = hecfda::model::paired_data::UncertainPairedData;

    // ported from: ScenarioStageDamage.cs's ctor
    // `ScenarioStageDamage(List<ImpactAreaStageDamage> impactAreaStageDamages)`.
    explicit ScenarioStageDamage(std::vector<ImpactAreaStageDamage> impact_area_stage_damages)
        : impact_area_stage_damages_(std::move(impact_area_stage_damages)) {}

    // ported from: ScenarioStageDamage.cs's `public List<ImpactAreaStageDamage>
    // ImpactAreaStageDamages` getter.
    const std::vector<ImpactAreaStageDamage>& impact_area_stage_damages() const {
        return impact_area_stage_damages_;
    }

    // ported from: ScenarioStageDamage.cs public (List<UncertainPairedData>,
    // List<UncertainPairedData>) Compute(bool computeIsDeterministic, ProgressReporter reporter).
    // ProgressReporter/Stopwatch parameters SEVERED (see class comment). Loops every impact area's
    // ImpactAreaStageDamage::compute() and AddRange's both result lists into the scenario-level
    // result pair, matching the C# loop verbatim (index-based `for`, matching the C# `for (int i =
    // 0; i < countImpactAreas; i++)` rather than a range-for, for line-for-line provenance).
    //
    // MOVE-vs-AddRange NOTE: C#'s `List<T>.AddRange` copies element REFERENCES (UncertainPairedData
    // is a reference type in C#) into the target list -- cheap, no deep copy, and the source list is
    // left untouched but no longer used afterward either way. This port's UncertainPairedData is
    // move-only (owns vector<unique_ptr<IDistribution>> histograms, see uncertain_paired_data.hpp),
    // so `insert` with move iterators is the faithful C++ analog: it transfers ownership of each
    // element exactly once, matching AddRange's "each result object ends up in the scenario list,
    // used from nowhere else" effect without requiring UncertainPairedData to be copyable.
    std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>> compute(
        bool compute_is_deterministic = false) {
        std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>> scenario_stage_damage_results;
        std::size_t count_impact_areas = impact_area_stage_damages_.size();
        for (std::size_t i = 0; i < count_impact_areas; ++i) {
            ImpactAreaStageDamage& impact_area_stage_damage = impact_area_stage_damages_[i];
            std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>>
                impact_area_stage_damage_results = impact_area_stage_damage.compute(compute_is_deterministic);
            scenario_stage_damage_results.first.insert(
                scenario_stage_damage_results.first.end(),
                std::make_move_iterator(impact_area_stage_damage_results.first.begin()),
                std::make_move_iterator(impact_area_stage_damage_results.first.end()));
            scenario_stage_damage_results.second.insert(
                scenario_stage_damage_results.second.end(),
                std::make_move_iterator(impact_area_stage_damage_results.second.begin()),
                std::make_move_iterator(impact_area_stage_damage_results.second.end()));
        }
        return scenario_stage_damage_results;
    }

   private:
    std::vector<ImpactAreaStageDamage> impact_area_stage_damages_;
};

}  // namespace stage_damage
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_STAGE_DAMAGE_SCENARIO_STAGE_DAMAGE_HPP
