// PATCHED LOCAL COPY of HEC.FDA.Model/compute/ImpactAreaScenarioSimulation.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 5 Task 7. This is the heaviest patched copy of the phase: it keeps the skeleton (fields,
// seed constants, ctor), the fluent Builder/SimulationBuilder (every With* overload, including
// every AddSinglePropertyRule call VERBATIM -- this emitter runs against the REAL
// UncertainPairedData/GraphicalUncertainPairedData/ContinuousDistribution, which DO have working
// Validate()/HasErrors() surfaces, unlike the C++ port's Task-7-scoped severance of those same
// rules -- see impact_area_scenario_simulation.hpp's class comment for why the port severs them
// and why that's safe for every case this fixture actually exercises), CanCompute, and
// InitializeConsequenceHistograms/CreateEAConsequenceHistograms VERBATIM.
//
// Dropped (mirrors the C++ port's SEVERANCES list in impact_area_scenario_simulation.hpp):
//  - `: ValidationErrorLogger, IProgressReport` base -> plain `: Validation` (ValidationErrorLogger
//    is a thin messaging layer over Validation with nothing this emitter needs; matches
//    patched/SystemPerformanceResults.cs's identical choice).
//  - `ProgressReport` event / `ReportProgress`: IProgressReport surface, no analog.
//  - `ReportMessage`/`ErrorMessage`/`MessageEventArgs` calls in CanCompute: MVVM messaging, no
//    analog (the dummy-fallback-free CanCompute logic itself is kept verbatim).
//  - `[StoredProperty("ImpactAreaScenarioSimulation")]`: reflection-driven serialization metadata.
//  - `System.Threading`/`System.Threading.Tasks` (`CancellationToken`, the 3-arg `Compute`
//    overload it delegates to, `ComputeIterations`, `PopulateRandomNumbers`,
//    `SetupPerformanceThresholds`, `DetermineSystemResponseThreshold`,
//    `EnsureBottomAndTopHaveCorrectProbabilities`, `CreateHistogramsForAssuranceOfThresholds`,
//    `GetFrequencyStageSample`, `ComputeRiskFromStageFrequency`, `ComputeDefaultThreshold`,
//    `LogSimulationPropertyRuleErrors`): the Monte Carlo compute loop, Phase 5 Tasks 8-11's job,
//    not reachable from any of this task's fixture cases (every case either short-circuits at the
//    CanCompute gate or calls CanCompute/InitializeConsequenceHistograms directly). The public
//    2-arg `Compute(ConvergenceCriteria, bool)` overload is kept but truncated to the CanCompute
//    gate + InitializeConsequenceHistograms, then throws NotImplementedException -- matching the
//    C++ port's `compute()` scope boundary exactly (see that method's header comment) so the
//    real-C#-vs-port comparison stays apples-to-apples for the is_null fixture case.
using System;
using System.Collections.Generic;
using HEC.FDA.Model.metrics;
using HEC.FDA.Model.paireddata;
using HEC.MVVMFramework.Base.Enumerations;
using HEC.MVVMFramework.Base.Implementations;
using Statistics;
using Statistics.Distributions;

namespace HEC.FDA.Model.compute
{
    public class ImpactAreaScenarioSimulation : Validation
    {
        private const int FREQUENCY_SEED = 1234;
        private const int FLOW_REGULATION_SEED = 2345;
        private const int STAGE_FLOW_SEED = 3456;
        private const int EXTERIOR_INTERIOR_SEED = 4567;
        private const int SYSTEM_RESPONSE_SEED = 5678;
        private const int STAGE_DAMAGE_SEED = 6789;
        private const int STAGE_LIFELOSS_SEED = 7891;
        public const int IMPACT_AREA_SIM_COMPLETED = -1001;
        private const double THRESHOLD_DAMAGE_PERCENT = 0.05;
        private const double THRESHOLD_DAMAGE_RECURRENCE_INTERVAL = 0.99;
        public const int DEFAULT_THRESHOLD_ID = 0;
        private ContinuousDistribution _FrequencyDischarge;
        private GraphicalUncertainPairedData _FrequencyDischargeGraphical;
        private UncertainPairedData _UnregulatedRegulated;
        private UncertainPairedData _DischargeStage;
        private GraphicalUncertainPairedData _FrequencyStage;
        private UncertainPairedData _ChannelStageFloodplainStage;
        private UncertainPairedData _SystemResponseFunction;
        private double _TopOfLeveeElevation;
        private bool _HasFailureStageDamage;
        private bool _HasFailureStageLifeLoss;
        private bool _HasNonFailureStageLifeLoss;
        private List<UncertainPairedData> _FailureStageDamageFunctions;
        private List<UncertainPairedData> _NonFailureStageDamageFunctions;
        private List<UncertainPairedData> _FailureStageLifeLossFunctions;
        private List<UncertainPairedData> _NonFailureStageLifeLossFunctions;
        private int _ImpactAreaID;
        private ImpactAreaScenarioResults _ImpactAreaScenarioResults;
        private bool _HasNonFailureStageDamage;
        private ConvergenceCriteria _ConvergenceCriteria;

        public int ImpactAreaID
        {
            get { return _ImpactAreaID; }
        }

        // Exposed for testing (no MVVM-internal-access equivalent in this subset-compiled
        // project) -- mirrors the C++ port's `results()` accessor.
        public ImpactAreaScenarioResults ImpactAreaScenarioResultsForTest
        {
            get { return _ImpactAreaScenarioResults; }
        }

        internal ImpactAreaScenarioSimulation(int impactAreaID)
        {
            _FrequencyDischarge = null;
            _FrequencyDischargeGraphical = new GraphicalUncertainPairedData();
            _UnregulatedRegulated = new UncertainPairedData();
            _DischargeStage = new UncertainPairedData();
            _FrequencyStage = new GraphicalUncertainPairedData();
            _ChannelStageFloodplainStage = new UncertainPairedData();
            _SystemResponseFunction = new UncertainPairedData();
            _FailureStageDamageFunctions = new List<UncertainPairedData>();
            _FailureStageLifeLossFunctions = new List<UncertainPairedData>();
            _NonFailureStageLifeLossFunctions = new List<UncertainPairedData>();
            _NonFailureStageDamageFunctions = new List<UncertainPairedData>();
            _ImpactAreaID = impactAreaID;
            _ImpactAreaScenarioResults = new ImpactAreaScenarioResults(_ImpactAreaID);
        }

        // Truncated to the CanCompute gate + InitializeConsequenceHistograms -- see file header.
        public ImpactAreaScenarioResults Compute(ConvergenceCriteria convergenceCriteria, bool computeIsDeterministic = false)
        {
            if (!CanCompute(convergenceCriteria))
            {
                _ImpactAreaScenarioResults = new ImpactAreaScenarioResults(_ImpactAreaID, true);
                return _ImpactAreaScenarioResults;
            }
            _ConvergenceCriteria = convergenceCriteria;
            InitializeConsequenceHistograms(convergenceCriteria);
            throw new NotImplementedException(
                "SetupPerformanceThresholds and the Monte Carlo iteration loop are not implemented until Phase 5 Tasks 8-11");
        }

        // Public here (private in the real C#) for the same reason CanCompute is public below and
        // ImpactAreaScenarioResultsForTest exists above: this emitter has no [InternalsVisibleTo]
        // test-assembly equivalent, matching the C++ port's identical access relaxation
        // (impact_area_scenario_simulation.hpp's class comment).
        public void InitializeConsequenceHistograms(ConvergenceCriteria convergenceCriteria)
        {
            if (_HasFailureStageDamage)
            {
                CreateEAConsequenceHistograms(convergenceCriteria, _FailureStageDamageFunctions, ConsequenceType.Damage, RiskType.Fail);
            }
            if (_HasNonFailureStageDamage)
            {
                CreateEAConsequenceHistograms(convergenceCriteria, _NonFailureStageDamageFunctions, ConsequenceType.Damage, RiskType.Non_Fail);
            }
            if (_HasNonFailureStageLifeLoss)
            {
                CreateEAConsequenceHistograms(convergenceCriteria, _NonFailureStageLifeLossFunctions, ConsequenceType.LifeLoss, RiskType.Non_Fail);
            }
            if (_HasFailureStageLifeLoss)
            {
                CreateEAConsequenceHistograms(convergenceCriteria, _FailureStageLifeLossFunctions, ConsequenceType.LifeLoss, RiskType.Fail);
            }
        }

        private void CreateEAConsequenceHistograms(ConvergenceCriteria convergenceCriteria, List<UncertainPairedData> stageConsequenceFunctions, ConsequenceType consequenceType, RiskType riskType)
        {
            foreach (UncertainPairedData stageConsequence in stageConsequenceFunctions)
            {
                _ImpactAreaScenarioResults.ConsequenceResults.AddNewConsequenceResultObject(
                    stageConsequence.CurveMetaData.DamageCategory,
                    stageConsequence.CurveMetaData.AssetCategory,
                    convergenceCriteria, _ImpactAreaID, consequenceType, riskType);
            }
        }

        public bool CanCompute(ConvergenceCriteria convergenceCriteria)
        {
            bool canCompute = true;
            if (ErrorLevel >= ErrorLevel.Fatal)
            {
                canCompute = false;
            }
            convergenceCriteria.Validate();
            if (convergenceCriteria.HasErrors)
            {
                canCompute = false;
            }
            return canCompute;
        }

        public static SimulationBuilder Builder(int impactAreaID)
        {
            return new SimulationBuilder(new ImpactAreaScenarioSimulation(impactAreaID));
        }

        public class SimulationBuilder
        {
            private readonly ImpactAreaScenarioSimulation _Simulation;
            internal SimulationBuilder(ImpactAreaScenarioSimulation sim)
            {
                _Simulation = sim;
            }
            public ImpactAreaScenarioSimulation Build()
            {
                _Simulation.Validate();
                return _Simulation;
            }
            public SimulationBuilder WithFlowFrequency(ContinuousDistribution continuousDistribution)
            {
                _Simulation._FrequencyDischarge = continuousDistribution;
                _Simulation.AddSinglePropertyRule("flow frequency", new Rule(() => { _Simulation._FrequencyDischarge.Validate(); return !_Simulation._FrequencyDischarge.HasErrors; }, string.Join(Environment.NewLine, _Simulation._FrequencyDischarge.GetErrors())));
                return new SimulationBuilder(_Simulation);
            }
            public SimulationBuilder WithFlowFrequency(GraphicalUncertainPairedData graphicalUncertainPairedData)
            {
                _Simulation._FrequencyDischargeGraphical = graphicalUncertainPairedData;
                return new SimulationBuilder(_Simulation);
            }
            public SimulationBuilder WithInflowOutflow(UncertainPairedData uncertainPairedData)
            {
                _Simulation._UnregulatedRegulated = uncertainPairedData;
                _Simulation.AddSinglePropertyRule("inflow outflow", new Rule(() => { _Simulation._UnregulatedRegulated.Validate(); return !_Simulation._UnregulatedRegulated.HasErrors; }, $"Inflow-Outflow has errors for the impact area with ID {_Simulation._ImpactAreaID}."));

                return new SimulationBuilder(_Simulation);
            }
            public SimulationBuilder WithFlowStage(UncertainPairedData uncertainPairedData)
            {
                _Simulation._DischargeStage = uncertainPairedData;
                _Simulation.AddSinglePropertyRule("flow stage", new Rule(() => { _Simulation._DischargeStage.Validate(); return !_Simulation._DischargeStage.HasErrors; }, $"Flow-Stage has errors  for the impact area with ID {_Simulation._ImpactAreaID}."));
                double stageMin = uncertainPairedData.Yvals[0].InverseCDF(p: 0.001);
                double stageMax = uncertainPairedData.Yvals[^1].InverseCDF(p: 0.999);
                _Simulation.AddSinglePropertyRule("stage range", new Rule(() => (stageMax - stageMin) < 1000, "The range of stages must be less than 1000. Ranges larger than this will cause memory problems", ErrorLevel.Fatal));
                return new SimulationBuilder(_Simulation);
            }
            public SimulationBuilder WithFrequencyStage(GraphicalUncertainPairedData graphicalUncertainPairedData)
            {
                _Simulation._FrequencyStage = graphicalUncertainPairedData;
                _Simulation.AddSinglePropertyRule("frequency_stage", new Rule(() => { _Simulation._FrequencyStage.Validate(); return !_Simulation._FrequencyStage.HasErrors; }, $"Frequency-Stage has errors  for the impact area with ID {_Simulation._ImpactAreaID}."));
                return new SimulationBuilder(_Simulation);
            }
            public SimulationBuilder WithInteriorExterior(UncertainPairedData uncertainPairedData)
            {
                _Simulation._ChannelStageFloodplainStage = uncertainPairedData;
                _Simulation.AddSinglePropertyRule("channelstage_floodplainstage", new Rule(() =>
                {
                    _Simulation._ChannelStageFloodplainStage.Validate();
                    return !_Simulation._ChannelStageFloodplainStage.HasErrors;
                }
                , $"There are errors in the InteriorExterior relationship for the impact area with ID {_Simulation._ImpactAreaID}."));
                return new SimulationBuilder(_Simulation);
            }
            public SimulationBuilder WithLevee(UncertainPairedData uncertainPairedData, double topOfLeveeElevation)
            {
                _Simulation._SystemResponseFunction = uncertainPairedData;
                _Simulation._TopOfLeveeElevation = topOfLeveeElevation;
                return new SimulationBuilder(_Simulation);
            }
            public SimulationBuilder WithStageDamages(List<UncertainPairedData> uncertainPairedDataList)
            {
                _Simulation._FailureStageDamageFunctions = uncertainPairedDataList;
                foreach (UncertainPairedData uncertainPairedData in _Simulation._FailureStageDamageFunctions)
                {
                    _Simulation.AddSinglePropertyRule(
                        uncertainPairedData.CurveMetaData.DamageCategory + " stage damages",
                        new Rule(
                            () => { uncertainPairedData.Validate(); return !uncertainPairedData.HasErrors; },
                            $"Stage-damage errors ror the impact area with ID {_Simulation._ImpactAreaID}: " + uncertainPairedData.GetErrorMessages()));
                }
                _Simulation._HasFailureStageDamage = true;
                return new SimulationBuilder(_Simulation);
            }

            public SimulationBuilder WithStageLifeLoss(List<UncertainPairedData> uncertainPairedDataList)
            {
                _Simulation._FailureStageLifeLossFunctions = uncertainPairedDataList;
                foreach (var upd in _Simulation._FailureStageLifeLossFunctions)
                {
                    _Simulation.AddSinglePropertyRule(
                        upd.CurveMetaData.DamageCategory + " stage life loss",
                        new Rule(
                            () => { upd.Validate(); return !upd.HasErrors; },
                            $"Stage life loss errors for the impact area with ID {_Simulation._ImpactAreaID}: " + upd.GetErrorMessages()));
                }
                _Simulation._HasFailureStageLifeLoss = true;
                return new SimulationBuilder(_Simulation);
            }

            public SimulationBuilder WithNonFailureStageLifeLoss(List<UncertainPairedData> uncertainPairedDataList)
            {
                _Simulation._NonFailureStageLifeLossFunctions = uncertainPairedDataList;
                foreach (var upd in _Simulation._NonFailureStageLifeLossFunctions)
                {
                    _Simulation.AddSinglePropertyRule(
                        upd.CurveMetaData.DamageCategory + " stage life loss",
                        new Rule(
                            () => { upd.Validate(); return !upd.HasErrors; },
                            $"Stage life loss errors for the impact area with ID {_Simulation._ImpactAreaID}: " + upd.GetErrorMessages()));
                }
                _Simulation._HasNonFailureStageLifeLoss = true;
                return new SimulationBuilder(_Simulation);
            }

            public SimulationBuilder WithAdditionalThreshold(Threshold threshold)
            {
                _Simulation._ImpactAreaScenarioResults.PerformanceByThresholds.AddThreshold(threshold);
                return new SimulationBuilder(_Simulation);
            }

            public SimulationBuilder WithNonFailureStageDamage(List<UncertainPairedData> stageDamageFunctions)
            {
                _Simulation._NonFailureStageDamageFunctions = stageDamageFunctions;
                _Simulation._HasNonFailureStageDamage = true;
                return new SimulationBuilder(_Simulation);
            }
        }
    }
}
