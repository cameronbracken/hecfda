// PATCHED LOCAL COPY of HEC.FDA.Model/scenarios/Scenario.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
// Phase 6 Task 8 (this patch). The real source implements `IReportMessage` (MVVM messaging:
// `event MessageReportedEventHandler MessageReport` + `ReportMessage(object, MessageEventArgs)`),
// neither reachable from / needed by this subset-compiled project (same rationale as
// patched/ScenarioStageDamage.cs's header). This patch:
//   - Drops the `IReportMessage` interface, the `MessageReport` event, and `ReportMessage(object,
//     MessageEventArgs)` -- matches the C++ port's scenario.hpp severance (see its class comment).
//   - Drops the 3-arg `Compute(ConvergenceCriteria, CancellationToken, bool)` overload +
//     `System.Threading` entirely; the 2-arg `Compute(ConvergenceCriteria, bool)` overload's body
//     is inlined directly here (no CancellationToken to thread through -- same "no threading
//     primitives" rationale already applied to patched/ImpactAreaScenarioSimulation.cs's own
//     Compute()).
//   - Drops the `internal Scenario()` empty-list default ctor (no real caller uses it upstream;
//     the public `Scenario(IList<ImpactAreaScenarioSimulation>)` ctor is the only one exercised by
//     this project's fixture/emitter surface).
// The fan-out loop itself -- construct a fresh ScenarioResults, call each impact area's own
// Compute(), AddResults() the result in, then stamp ComputeDate/SoftwareVersion -- is kept
// VERBATIM (including the ComputeDate/SoftwareVersion stamp: the real C# is the oracle here, so
// this patched copy reproduces its exact side effects; the C++ port severs that stamp per its own
// class comment, but the emitter's job is to match the REAL C#, not the port).
using System;
using System.Collections.Generic;
using System.Reflection;
using HEC.FDA.Model.compute;
using HEC.FDA.Model.metrics;
using Statistics;

namespace HEC.FDA.Model.scenarios
{
    public class Scenario
    {
        private readonly IList<ImpactAreaScenarioSimulation> _impactAreaSimulations;

        public Scenario(IList<ImpactAreaScenarioSimulation> impactAreaSimulations)
        {
            _impactAreaSimulations = impactAreaSimulations;
        }

        #region Methods
        public ScenarioResults Compute(ConvergenceCriteria convergenceCriteria, bool computeIsDeterministic = false)
        {
            ScenarioResults scenarioResults = new();
            foreach (ImpactAreaScenarioSimulation impactArea in _impactAreaSimulations)
            {
                ImpactAreaScenarioResults res = impactArea.Compute(convergenceCriteria, computeIsDeterministic);
                scenarioResults.AddResults(res);
            }
            scenarioResults.ComputeDate = DateTime.Now.ToString("G");
            scenarioResults.SoftwareVersion = Assembly.GetExecutingAssembly().GetName().Version.ToString();
            return scenarioResults;
        }
        #endregion
    }
}
