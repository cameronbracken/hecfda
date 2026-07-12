// PATCHED LOCAL COPY of HEC.FDA.Model/compute/ImpactAreaScenarioSimulation.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 5 Task 7 (skeleton) + Task 8 (frequency-stage assembly + seeded PopulateRandomNumbers).
// This is the heaviest patched copy of the phase: it keeps the skeleton (fields, seed constants,
// ctor), the fluent Builder/SimulationBuilder (every With* overload, including every
// AddSinglePropertyRule call VERBATIM -- this emitter runs against the REAL
// UncertainPairedData/GraphicalUncertainPairedData/ContinuousDistribution, which DO have working
// Validate()/HasErrors() surfaces, unlike the C++ port's Task-7-scoped severance of those same
// rules -- see impact_area_scenario_simulation.hpp's class comment for why the port severs them
// and why that's safe for every case this fixture actually exercises), CanCompute,
// InitializeConsequenceHistograms/CreateEAConsequenceHistograms, and (Task 8) PopulateRandomNumbers/
// GetFrequencyStageSample/GetStageFreq VERBATIM, plus the FrequencyStageCurves record struct.
// FrequencyStageCurves/GetFrequencyStageSample/GetStageFreq are made `public` here (all `internal`/
// `private` in the real C#) -- same access-relaxation rationale as CanCompute/
// InitializeConsequenceHistograms below (no InternalsVisibleTo-equivalent in this subset-compiled
// emitter project).
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
//    overload it delegates to, `ComputeIterations`, `SetupPerformanceThresholds`,
//    `DetermineSystemResponseThreshold`, `EnsureBottomAndTopHaveCorrectProbabilities`,
//    `CreateHistogramsForAssuranceOfThresholds`, `ComputeRiskFromStageFrequency`,
//    `ComputeDefaultThreshold`, `LogSimulationPropertyRuleErrors`): the rest of the Monte Carlo
//    compute loop, Phase 5 Tasks 9-11's job, not reachable from any of this task's fixture cases
//    (every case either short-circuits at the CanCompute gate, or calls CanCompute/
//    InitializeConsequenceHistograms/PopulateRandomNumbers/GetFrequencyStageSample directly). The
//    public 2-arg `Compute(ConvergenceCriteria, bool)` overload is kept but truncated to the
//    CanCompute gate + InitializeConsequenceHistograms, then throws NotImplementedException --
//    matching the C++ port's `compute()` scope boundary exactly (see that method's header
//    comment) so the real-C#-vs-port comparison stays apples-to-apples for the is_null fixture
//    case.
using System;
using System.Collections.Generic;
using System.Linq;
using HEC.FDA.Model.extensions;
using HEC.FDA.Model.metrics;
using HEC.FDA.Model.paireddata;
using HEC.FDA.Model.utilities;
using HEC.MVVMFramework.Base.Enumerations;
using HEC.MVVMFramework.Base.Implementations;
using Statistics;
using Statistics.Distributions;

namespace HEC.FDA.Model.compute
{
    // ported from: ImpactAreaScenarioSimulation.cs `internal readonly record struct
    // FrequencyStageCurves(PairedData ChannelStage, PairedData FloodplainStage)` (lines 25-28).
    // Made `public` here -- see file header.
    public readonly record struct FrequencyStageCurves(
        PairedData ChannelStage,
        PairedData FloodplainStage
    );

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

        // ported from: ImpactAreaScenarioSimulation.cs `private void
        // PopulateRandomNumbers(ConvergenceCriteria convergenceCriteria)` (lines 202-252),
        // VERBATIM. Made `public` here -- see file header.
        public void PopulateRandomNumbers(ConvergenceCriteria convergenceCriteria)
        {
            int quantityOfRandomNumbers = Convert.ToInt32(convergenceCriteria.MaxIterations * 1.25);

            if (_FrequencyDischarge != null)
            {
                _FrequencyDischarge.GenerateRandomSamplesofNumbers(FREQUENCY_SEED, quantityOfRandomNumbers);
            }
            if (!_FrequencyDischargeGraphical.CurveMetaData.IsNull)
            {
                _FrequencyDischargeGraphical.GenerateRandomNumbers(FREQUENCY_SEED, quantityOfRandomNumbers);
            }
            if (!_UnregulatedRegulated.CurveMetaData.IsNull)
            {
                _UnregulatedRegulated.GenerateRandomNumbers(FLOW_REGULATION_SEED, quantityOfRandomNumbers);
            }
            if (!_DischargeStage.CurveMetaData.IsNull)
            {
                _DischargeStage.GenerateRandomNumbers(STAGE_FLOW_SEED, quantityOfRandomNumbers);
            }
            if (!_FrequencyStage.CurveMetaData.IsNull)
            {
                _FrequencyStage.GenerateRandomNumbers(FREQUENCY_SEED, quantityOfRandomNumbers);
            }
            if (!_ChannelStageFloodplainStage.CurveMetaData.IsNull)
            {
                _ChannelStageFloodplainStage.GenerateRandomNumbers(EXTERIOR_INTERIOR_SEED, quantityOfRandomNumbers);
            }
            if (!_SystemResponseFunction.CurveMetaData.IsNull)
            {
                _SystemResponseFunction.GenerateRandomNumbers(SYSTEM_RESPONSE_SEED, quantityOfRandomNumbers);
            }
            foreach (UncertainPairedData stageDamage in _FailureStageDamageFunctions)
            {
                stageDamage.GenerateRandomNumbers(STAGE_DAMAGE_SEED, quantityOfRandomNumbers);
            }
            foreach (UncertainPairedData stageDamage in _NonFailureStageDamageFunctions)
            {
                stageDamage.GenerateRandomNumbers(STAGE_DAMAGE_SEED, quantityOfRandomNumbers);
            }
            foreach (UncertainPairedData stageLifeLoss in _FailureStageLifeLossFunctions)
            {
                stageLifeLoss.GenerateRandomNumbers(STAGE_LIFELOSS_SEED, quantityOfRandomNumbers);
            }
            foreach (UncertainPairedData stageLifeLoss in _NonFailureStageLifeLossFunctions)
            {
                stageLifeLoss.GenerateRandomNumbers(STAGE_LIFELOSS_SEED, quantityOfRandomNumbers);
            }
        }

        // ported from: ImpactAreaScenarioSimulation.cs `private FrequencyStageCurves
        // GetFrequencyStageSample(bool computeIsDeterministic, long thisComputeIteration)` (lines
        // 405-433), VERBATIM. Made `public` here -- see file header.
        public FrequencyStageCurves GetFrequencyStageSample(bool computeIsDeterministic, long thisComputeIteration)
        {
            PairedData frequency_stage_sample;
            if (_FrequencyStage.CurveMetaData.IsNull)
            {
                PairedData frequencyDischarge;

                if (_FrequencyDischargeGraphical.CurveMetaData.IsNull)
                {
                    frequencyDischarge = _FrequencyDischarge.BootstrapToPairedData(thisComputeIteration, utilities.DoubleGlobalStatics.RequiredExceedanceProbabilities, computeIsDeterministic);
                }
                else
                {
                    frequencyDischarge = _FrequencyDischargeGraphical.SamplePairedData(thisComputeIteration, computeIsDeterministic);
                }
                frequency_stage_sample = GetStageFreq(computeIsDeterministic, thisComputeIteration, frequencyDischarge);
            }
            else
            {
                frequency_stage_sample = _FrequencyStage.SamplePairedData(thisComputeIteration, computeIsDeterministic);
            }
            if (!_ChannelStageFloodplainStage.IsNull)
            {
                PairedData channelstage_floodplainstage_sample = _ChannelStageFloodplainStage.SamplePairedData(thisComputeIteration, computeIsDeterministic);
                PairedData frequency_floodplainstage = channelstage_floodplainstage_sample.compose(frequency_stage_sample);
                return new FrequencyStageCurves(ChannelStage: frequency_stage_sample, FloodplainStage: frequency_floodplainstage);
            }
            return new FrequencyStageCurves(ChannelStage: frequency_stage_sample, FloodplainStage: frequency_stage_sample);
        }

        // ported from: ImpactAreaScenarioSimulation.cs `private PairedData GetStageFreq(bool
        // computeIsDeterministic, long thisComputeIteration, PairedData frequencyDischarge)`
        // (lines 435-451), VERBATIM. Made `public` here -- see file header.
        public PairedData GetStageFreq(bool computeIsDeterministic, long thisComputeIteration, PairedData frequencyDischarge)
        {
            PairedData frequency_stage;
            if (_UnregulatedRegulated.CurveMetaData.IsNull)
            {
                PairedData discharge_stage_sample = _DischargeStage.SamplePairedData(thisComputeIteration, computeIsDeterministic);
                frequency_stage = discharge_stage_sample.compose(frequencyDischarge);
            }
            else
            {
                PairedData inflow_outflow_sample = _UnregulatedRegulated.SamplePairedData(thisComputeIteration, computeIsDeterministic);
                PairedData transformff = inflow_outflow_sample.compose(frequencyDischarge);
                PairedData discharge_stage_sample = _DischargeStage.SamplePairedData(thisComputeIteration, computeIsDeterministic);
                frequency_stage = discharge_stage_sample.compose(transformff);
            }
            return frequency_stage;
        }

        // ============================================================================================
        // Phase 5 Task 9: risk/consequence integration + performance/threshold/assurance, VERBATIM
        // from the real ImpactAreaScenarioSimulation.cs (lines 136-774), made `public` where the real
        // C# has them `private`/`internal` -- same access-relaxation rationale as CanCompute/
        // InitializeConsequenceHistograms/PopulateRandomNumbers/GetFrequencyStageSample above (this
        // subset-compiled emitter has no [InternalsVisibleTo] test-assembly equivalent). Dropped only
        // what the file header already documents as out of scope: ComputeIterations (the Monte Carlo
        // loop itself, Task 10) is not needed here -- SetupPerformanceThresholds runs its own
        // self-contained deterministic iteration-1 pass to derive the default threshold, independent
        // of ComputeIterations.
        // ============================================================================================

        public void SetupPerformanceThresholds(ConvergenceCriteria convergenceCriteria)
        {
            bool defaultThresholdExists = _ImpactAreaScenarioResults.PerformanceByThresholds.ListOfThresholds.Select(x => x.ThresholdID).Contains(0);

            if (_SystemResponseFunction.Xvals != null)
            {
                Threshold systemResponseThreshold = DetermineSystemResponseThreshold(convergenceCriteria);
                _ImpactAreaScenarioResults.PerformanceByThresholds.AddThreshold(systemResponseThreshold);
            }
            else if (!defaultThresholdExists)
            {
                FrequencyStageCurves curves = GetFrequencyStageSample(computeIsDeterministic: true, 1);
                ComputeRiskFromStageFrequency(curves.FloodplainStage, 1, 1, computeIsDeterministic: true, _FailureStageDamageFunctions, ConsequenceType.Damage, false, true);
                Threshold defaultThreshold = ComputeDefaultThreshold(convergenceCriteria, damageFrequencyFunctions: _ImpactAreaScenarioResults.ConsequenceFrequencyFunctions.Select((c) => c.FrequencyCurve).ToList());
                _ImpactAreaScenarioResults.PerformanceByThresholds.AddThreshold(defaultThreshold);
            }

            CreateHistogramsForAssuranceOfThresholds();
        }

        public Threshold DetermineSystemResponseThreshold(ConvergenceCriteria convergenceCriteria)
        {
            ThresholdEnum thresholdEnum;
            if (_SystemResponseFunction.Xvals.Length <= 2)
            {
                thresholdEnum = ThresholdEnum.TopOfLevee;
            }
            else
            {
                thresholdEnum = ThresholdEnum.LeveeSystemResponse;
            }
            return new Threshold(thresholdID: 0, _SystemResponseFunction, convergenceCriteria, thresholdEnum, _TopOfLeveeElevation);
        }

        public PairedData ComputeRiskFromStageFrequency(FrequencyStageCurves curves, long thisComputeIteration, long thisChunkIteration, bool computeIsDeterministic)
        {
            if (_SystemResponseFunction.CurveMetaData.IsNull)
            {
                if (_HasFailureStageDamage)
                {
                    ComputeRiskFromStageFrequency(curves.FloodplainStage, thisComputeIteration, thisChunkIteration, computeIsDeterministic, _FailureStageDamageFunctions, ConsequenceType.Damage);
                }
                if (_HasFailureStageLifeLoss)
                {
                    ComputeRiskFromStageFrequency(curves.FloodplainStage, thisComputeIteration, thisChunkIteration, computeIsDeterministic, _FailureStageLifeLossFunctions, ConsequenceType.LifeLoss);
                }
                return null;
            }
            else
            {
                PairedData systemResponse_sample = _SystemResponseFunction.SamplePairedData(thisComputeIteration, computeIsDeterministic);
                if (_HasFailureStageDamage)
                {
                    ComputeRiskFromStageFrequency_WithLevee(curves, systemResponse_sample, thisComputeIteration, thisChunkIteration, computeIsDeterministic, ConsequenceType.Damage);
                }
                if (_HasFailureStageLifeLoss)
                {
                    ComputeRiskFromStageFrequency_WithLevee(curves, systemResponse_sample, thisComputeIteration, thisChunkIteration, computeIsDeterministic, ConsequenceType.LifeLoss);
                }
                return systemResponse_sample;
            }
        }

        public void ComputePerformanceFromStageFrequency(PairedData channelStageFreq, PairedData systemResponse_sample, int thisChunkIteration)
        {
            if (systemResponse_sample == null || systemResponse_sample.Xvals.Count <= 2)
            {
                ComputePerformance(channelStageFreq, thisChunkIteration);
            }
            else
            {
                ComputeLeveePerformance(channelStageFreq, systemResponse_sample, thisChunkIteration);
            }
        }

        public void ComputeRiskFromStageFrequency(PairedData frequency_stage, long thisComputeIteration, long thisChunkIteration, bool computeIsDeterministic, List<UncertainPairedData> consequenceFunctions, ConsequenceType consequenceType, bool saveAnnualizedResult = true, bool saveFreqCurves = false)
        {
            foreach (UncertainPairedData stageUncertainConsequences in consequenceFunctions)
            {
                PairedData _stage_consequences_sample = stageUncertainConsequences.SamplePairedData(thisComputeIteration, computeIsDeterministic);
                PairedData frequency_consequences = _stage_consequences_sample.compose(frequency_stage);
                if (saveFreqCurves)
                {
                    _ImpactAreaScenarioResults.ConsequenceFrequencyFunctions.Add(new(frequency_consequences, stageUncertainConsequences.CurveMetaData.DamageCategory, stageUncertainConsequences.CurveMetaData.AssetCategory, consequenceType, RiskType.Fail));
                }
                if (!saveAnnualizedResult)
                {
                    continue;
                }
                CategoriedUncertainPairedData uncertainCurve = _ImpactAreaScenarioResults.GetOrCreateUncertainConsequenceFrequencyCurve(
                    frequency_consequences.Xvals.ToArray(),
                    stageUncertainConsequences.CurveMetaData.DamageCategory,
                    stageUncertainConsequences.CurveMetaData.AssetCategory,
                    consequenceType,
                    RiskType.Fail,
                    _ConvergenceCriteria);
                uncertainCurve.AddCurveRealization(frequency_consequences, thisChunkIteration);

                double eaConsequencesEstimate = frequency_consequences.Integrate();
                _ImpactAreaScenarioResults.ConsequenceResults.AddConsequenceRealization(eaConsequencesEstimate, stageUncertainConsequences.CurveMetaData.DamageCategory, stageUncertainConsequences.CurveMetaData.AssetCategory, _ImpactAreaID, thisChunkIteration, consequenceType, RiskType.Fail);
            }
        }

        public void ComputeRiskFromStageFrequency_WithLevee(FrequencyStageCurves curves, PairedData systemResponse, long thisComputeIteration, long thisChunkIteration, bool computeIsDeterministic, ConsequenceType type)
        {
            List<UncertainPairedData> failureStageDamageFunctions;
            List<UncertainPairedData> nonfailureStageDamageFunctions;

            if (type == ConsequenceType.LifeLoss)
            {
                failureStageDamageFunctions = _FailureStageLifeLossFunctions;
                nonfailureStageDamageFunctions = _NonFailureStageLifeLossFunctions;
            }
            else
            {
                failureStageDamageFunctions = _FailureStageDamageFunctions;
                nonfailureStageDamageFunctions = _NonFailureStageDamageFunctions;
            }

            PairedData validatedSystemResponse = EnsureBottomAndTopHaveCorrectProbabilities(systemResponse);
            ComputeAnnualizedConsequence(curves, thisComputeIteration, thisChunkIteration, computeIsDeterministic, type, failureStageDamageFunctions, validatedSystemResponse, RiskType.Fail);

            if (nonfailureStageDamageFunctions.Count > 0)
            {
                PairedData complementSystemResponse = CalculateFailureProbComplement(validatedSystemResponse);
                ComputeAnnualizedConsequence(curves, thisComputeIteration, thisChunkIteration, computeIsDeterministic, type, nonfailureStageDamageFunctions, complementSystemResponse, RiskType.Non_Fail);
            }
        }

        public void ComputeAnnualizedConsequence(FrequencyStageCurves curves, long thisComputeIteration, long thisChunkIteration, bool computeIsDeterministic, ConsequenceType type, List<UncertainPairedData> stageConsequenceFuncs, PairedData validatedSystemResponse, RiskType riskType)
        {
            foreach (UncertainPairedData stageUncertainCons in stageConsequenceFuncs)
            {
                PairedData stageDamageSample = stageUncertainCons.SamplePairedData(thisComputeIteration, computeIsDeterministic);
                PairedData damFreq;
                if (ReferenceEquals(curves.ChannelStage, curves.FloodplainStage))
                {
                    stageDamageSample = stageDamageSample.multiply(validatedSystemResponse);
                    damFreq = stageDamageSample.compose(curves.FloodplainStage);
                }
                else
                {
                    PairedData freqDamage = stageDamageSample.compose(curves.FloodplainStage);
                    PairedData freqFailure = validatedSystemResponse.compose(curves.ChannelStage);
                    damFreq = freqDamage.multiply(freqFailure);
                }

                CategoriedUncertainPairedData uncertainCurve = _ImpactAreaScenarioResults.GetOrCreateUncertainConsequenceFrequencyCurve(
                    damFreq.Xvals.ToArray(),
                    stageUncertainCons.CurveMetaData.DamageCategory,
                    stageUncertainCons.CurveMetaData.AssetCategory,
                    type,
                    riskType,
                    _ConvergenceCriteria);
                uncertainCurve.AddCurveRealization(damFreq, thisChunkIteration);

                double eadOraal = damFreq.Integrate();
                _ImpactAreaScenarioResults.ConsequenceResults.AddConsequenceRealization(eadOraal, stageUncertainCons.CurveMetaData.DamageCategory, stageUncertainCons.CurveMetaData.AssetCategory, _ImpactAreaID, thisChunkIteration, type, riskType);
            }
        }

        public static PairedData CalculateFailureProbComplement(PairedData validatedSystemResponse)
        {
            double[] probabilityOfNonFailure = new double[validatedSystemResponse.Yvals.Count];
            for (int i = 0; i < probabilityOfNonFailure.Length; i++)
            {
                probabilityOfNonFailure[i] = 1 - (validatedSystemResponse.Yvals[i]);
            }
            PairedData complementOfSystemResponse = new(validatedSystemResponse.Xvals.ToArray(), probabilityOfNonFailure);
            return complementOfSystemResponse;
        }

        public void ComputePerformance(PairedData frequency_stage, int thisChunkIteration)
        {
            foreach (var thresholdEntry in _ImpactAreaScenarioResults.PerformanceByThresholds.ListOfThresholds)
            {
                double thresholdValue = thresholdEntry.ThresholdValue;
                double aep = 1 - frequency_stage.f_inverse(thresholdValue);
                thresholdEntry.SystemPerformanceResults.AddAEPForAssurance(aep, thisChunkIteration);
                GetStageForNonExceedanceProbability(frequency_stage, thresholdEntry, thisChunkIteration);
            }
        }

        public void ComputeLeveePerformance(PairedData frequency_stage, PairedData levee_curve_sample, int thisChunkIteration)
        {
            PairedData levee_frequency_stage = levee_curve_sample.compose(frequency_stage);
            double aep = 0;
            if (levee_frequency_stage.Xvals[0] != 0)
            {
                double initialProbOfStageInRange = levee_frequency_stage.Xvals[0] - 0;
                double initialProbFailure = (levee_frequency_stage.Yvals[0] + 0) / 2;
                aep += initialProbOfStageInRange * initialProbFailure;
            }
            for (int i = 1; i < levee_frequency_stage.Xvals.Count; i++)
            {
                double probabilityOfStageInRange = levee_frequency_stage.Xvals[i] - levee_frequency_stage.Xvals[i - 1];
                double averageProbFailure = (levee_frequency_stage.Yvals[i] + levee_frequency_stage.Yvals[i - 1]) / 2;
                aep += probabilityOfStageInRange * averageProbFailure;
            }
            double finalProbOfStageInRange = 1 - levee_frequency_stage.Xvals[^1];
            double finalAvgProbFailure = levee_frequency_stage.Yvals[^1];
            aep += finalProbOfStageInRange * finalAvgProbFailure;
            foreach (var thresholdEntry in _ImpactAreaScenarioResults.PerformanceByThresholds.ListOfThresholds)
            {
                thresholdEntry.SystemPerformanceResults.AddAEPForAssurance(aep, thisChunkIteration);
                GetStageForNonExceedanceProbability(frequency_stage, thresholdEntry, thisChunkIteration);
            }
        }

        public static void GetStageForNonExceedanceProbability(PairedData frequency_stage, Threshold threshold, int thisChunkIteration)
        {
            double[] er101RequiredNonExceedanceProbabilities = new double[] { .9, .96, .98, .99, .996, .998 };
            foreach (double nonExceedanceProbability in er101RequiredNonExceedanceProbabilities)
            {
                double stageOfEvent = frequency_stage.f(nonExceedanceProbability);
                threshold.SystemPerformanceResults.AddStageForAssurance(nonExceedanceProbability, stageOfEvent, thisChunkIteration);
            }
        }

        public void CreateHistogramsForAssuranceOfThresholds()
        {
            double[] er101RequiredNonExceedanceProbabilities = new double[] { .9, .96, .98, .99, .996, .998 };
            foreach (var thresholdEntry in _ImpactAreaScenarioResults.PerformanceByThresholds.ListOfThresholds)
            {
                for (int i = 0; i < er101RequiredNonExceedanceProbabilities.Length; i++)
                {
                    thresholdEntry.SystemPerformanceResults.AddStageAssuranceHistogram(er101RequiredNonExceedanceProbabilities[i]);
                }
            }
        }

        internal static PairedData ComputeTotalStageDamage(List<UncertainPairedData> failStageDamages)
        {
            CurveMetaData metadata = new("Total", "Total");
            PairedData totalStageDamage = new(null, null, metadata);
            long iteration = 1;
            foreach (UncertainPairedData uncertainPairedData in failStageDamages)
            {
                PairedData stageDamageSample = uncertainPairedData.SamplePairedData(iteration, retrieveDeterministicRepresentation: true);
                totalStageDamage = totalStageDamage.SumYsForGivenX(stageDamageSample);
            }
            return totalStageDamage;
        }

        public Threshold ComputeDefaultThreshold(ConvergenceCriteria convergenceCriteria, List<PairedData> damageFrequencyFunctions)
        {
            if (!_SystemResponseFunction.IsNull)
            {
                throw new Exception("A default threshold cannot be calculated for an impact area with a levee.");
            }
            PairedData totalStageDamage = ComputeTotalStageDamage(_FailureStageDamageFunctions);
            PairedData totalFrequencyDamage = damageFrequencyFunctions[0];
            for (int i = 1; i < damageFrequencyFunctions.Count; i++)
            {
                totalFrequencyDamage = totalFrequencyDamage.SumYsForGivenX(damageFrequencyFunctions[i]);
            }
            double thresholdDamage = THRESHOLD_DAMAGE_PERCENT * totalFrequencyDamage.f(THRESHOLD_DAMAGE_RECURRENCE_INTERVAL);
            double thresholdStage = totalStageDamage.f_inverse(thresholdDamage);
            return new Threshold(DEFAULT_THRESHOLD_ID, convergenceCriteria, ThresholdEnum.DefaultExteriorStage, thresholdStage);
        }

        private static PairedData EnsureBottomAndTopHaveCorrectProbabilities(PairedData systemResponseFunction)
        {
            bool systemResponseIsComplete = (systemResponseFunction.Yvals.Contains(0) && systemResponseFunction.Yvals.Contains(1));
            if (systemResponseIsComplete)
            {
                return systemResponseFunction;
            }
            else
            {
                List<double> tempXvals = new();
                List<double> tempYvals = new();

                double buffer = .001;

                double belowFragilityCurveValue = 0.0;
                double stageToAddBelowFragility = systemResponseFunction.Xvals[0] - buffer;

                tempXvals.Add(stageToAddBelowFragility);
                tempYvals.Add(belowFragilityCurveValue);

                for (int i = 0; i < systemResponseFunction.Xvals.Count; i++)
                {
                    tempXvals.Add(systemResponseFunction.Xvals[i]);
                    tempYvals.Add(systemResponseFunction.Yvals[i]);
                }

                double aboveFragilityCurveValue = 1.0;
                double stageToAddAboveFragility = systemResponseFunction.Xvals[^1] + buffer;

                tempXvals.Add(stageToAddAboveFragility);
                tempYvals.Add(aboveFragilityCurveValue);

                PairedData newSystemREsponse = new(tempXvals.ToArray(), tempYvals.ToArray());
                return newSystemREsponse;
            }
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
