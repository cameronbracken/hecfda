// PATCHED LOCAL COPY of HEC.FDA.Model/stageDamage/ScenarioStageDamage.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
// Phase 4 Task 8 (this patch). The real source pulls in Utility.Progress (ProgressReporter) and
// System.Diagnostics.Stopwatch, neither reachable from / needed by this subset-compiled project
// (same rationale as patched/ImpactAreaStageDamage.cs's header). This patch:
//   - Drops the `ProgressReporter reporter = null` parameter and the `reporter ??=
//     ProgressReporter.None()`/`Stopwatch sw`/`SubTask`/`ReportMessage`/
//     `ReportTimestampedMessage`/`ReportProgress`/`ReportProgressFraction`/`ReportTaskCompleted`
//     calls around Compute()'s loop body -- matches the C++ port's scenario_stage_damage.hpp
//     severance (see its class comment). The `ImpactAreaStageDamage.Compute(computeIsDeterministic,
//     subPr, sw)` call becomes `ImpactAreaStageDamage.Compute(computeIsDeterministic)` (this
//     project's patched/ImpactAreaStageDamage.cs already dropped its own ProgressReporter/Stopwatch
//     parameters the same way).
//   - Drops ProduceStructureDetails (CSV, depends on the dropped
//     ProduceImpactAreaStructureDetails) and GetErrorMessages (optional per the task brief; not
//     needed by this project's fixture/emitter surface).
// Compute()'s loop itself -- construct the (List<UncertainPairedData>, List<UncertainPairedData>)
// accumulator, iterate _ImpactAreaStageDamage by index, call each ImpactAreaStageDamage.Compute(),
// AddRange both result lists -- is kept VERBATIM.
using System.Collections.Generic;
using HEC.FDA.Model.paireddata;

namespace HEC.FDA.Model.stageDamage
{
    public class ScenarioStageDamage
    {
        #region Fields
        private readonly List<ImpactAreaStageDamage> _ImpactAreaStageDamage;
        #endregion
        public List<ImpactAreaStageDamage> ImpactAreaStageDamages
        {
            get { return _ImpactAreaStageDamage; }
        }
        #region Constructor
        public ScenarioStageDamage(List<ImpactAreaStageDamage> impactAreaStageDamages)
        {
            _ImpactAreaStageDamage = impactAreaStageDamages;
        }
        #endregion

        #region Methods
        // PATCHED: ProgressReporter/Stopwatch parameters and all reporter/sw calls dropped (see
        // file header) -- the loop body (construct accumulator, iterate, Compute(), AddRange) is
        // otherwise verbatim.
        public (List<UncertainPairedData>, List<UncertainPairedData>) Compute(bool computeIsDeterministic = false)
        {
            (List<UncertainPairedData>, List<UncertainPairedData>) scenarioStageDamageResults = new(new List<UncertainPairedData>(), new List<UncertainPairedData>());
            int countImpactAreas = _ImpactAreaStageDamage.Count;
            for (int i = 0; i < countImpactAreas; i++)
            {
                ImpactAreaStageDamage impactAreaStageDamage = _ImpactAreaStageDamage[i];
                (List<UncertainPairedData>, List<UncertainPairedData>) impactAreaStageDamageResults = impactAreaStageDamage.Compute(computeIsDeterministic);
                scenarioStageDamageResults.Item1.AddRange(impactAreaStageDamageResults.Item1);
                scenarioStageDamageResults.Item2.AddRange(impactAreaStageDamageResults.Item2);
            }
            return scenarioStageDamageResults;
        }
        #endregion
    }
}
