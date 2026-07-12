// PATCHED LOCAL COPY of HEC.FDA.Model/stageDamage/ImpactAreaStageDamage.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
// Phase 4 Task 6 -- GEOMETRY ONLY (the ctor + EstablishAggregationStages and the pure static/
// instance stage-interval helpers). The real source pulls in HEC.FDA.Model.hydraulics
// (HydraulicDataset, disk-backed IHydraulicProfile), HEC.MVVMFramework (PropertyValidationHelper/
// IDontImplementValidationButMyPropertiesDo base, MessageReport/ReportMessage), Utility.Progress
// (ProgressReporter), and System.Diagnostics.Stopwatch -- none reachable from / needed by this
// subset-compiled project for the geometry surface this task exercises. This patch:
//   - Drops the `: PropertyValidationHelper, IDontImplementValidationButMyPropertiesDo` base
//     entirely (Validate()/GetErrorsFromProperties() are dropped along with it -- no rules/
//     validation infrastructure needed for geometry, matching the C++ port's severance).
//   - Replaces `HydraulicDataset _HydraulicDataset` with a plain `List<double>
//     _ProfileProbabilities` field (the hydraulics-as-arrays replacement, matching the C++ port's
//     HydraulicProfiles.profile_probabilities()): every `_HydraulicDataset.HydraulicProfiles.
//     First()/Last().Probability`/`.Count()` reference becomes `_ProfileProbabilities[0]`/`[^1]`/
//     `.Count`. `hydroParentDirectory` (disk path, used only by the dropped CSV-detail methods) is
//     dropped from the ctor signature too.
//   - Replaces both `ErrorMessage`/`ReportMessage` "no frequency function found" branches with
//     `throw new Exception(message)` (same message text) -- matches the C++ port's
//     throw_missing_discharge_stage_error()/throw_no_frequency_function_error().
//   - Drops Compute() and everything it exclusively calls
//     (ComputeDamageWithUncertaintyAllCoordinates, ComputeLower/Middle/UpperStageDamage +
//     InterpolateBetweenProfiles, ProduceZeroDamageFunctions, CreateConsequenceDistributionResults,
//     DumpDataIntoDistributions, IsTheFunctionNotConverged) and the CSV detail methods
//     (ProduceImpactAreaStructureDetails, DamagesToStrings, DepthsToStrings, StagesToStrings) --
//     Task 7+ scope, not needed for this task's dispatch (see the C++ header's SEVERANCES note for
//     the matching decision not to stub them).
// Everything else -- the ctor, EstablishAggregationStages, IdentifyCentralStageFrequencyAtIndexLocation,
// IdentifyMinAndMaxStageWithUncertainty, SetCoordinateQuantity, ComputeStagesAtIndexLocation,
// ExtrapolateFromAboveAtIndexLocation, ExtrapolateFromBelowStagesAtIndexLocation, CalculateIntervals,
// CalculateIncrementOfStages, CalculateLowerIncrementOfStages -- is kept VERBATIM (minus the
// _HydraulicDataset -> _ProfileProbabilities substitution described above).
using System;
using System.Collections.Generic;
using System.Linq;
using HEC.FDA.Model.paireddata;
using HEC.FDA.Model.structures;
using Statistics;

namespace HEC.FDA.Model.stageDamage
{
    public class ImpactAreaStageDamage
    {
        #region Hard Coded Compute Settings
        private const double MIN_PROBABILITY = 0.0001;
        private const double MAX_PROBABILITY = 0.9999;
        private const double FEET_PER_COORDINATE = 0.25;
        private const int MINIMUM_EXTRAPOLATION_COORDINATES = 4;
        private const int MINIMUM_INTERPOLATION_COORDINATES = 2;
        #endregion

        #region Fields
        private readonly ContinuousDistribution _AnalyticalFlowFrequency;
        private readonly GraphicalUncertainPairedData _GraphicalFrequency;
        private readonly UncertainPairedData _DischargeStage;
        private readonly UncertainPairedData _UnregulatedRegulated;
        private readonly int _AnalysisYear;
        // PATCHED: replaces HydraulicDataset _HydraulicDataset -- see file header.
        private readonly List<double> _ProfileProbabilities;

        private double _MinStageForArea;
        private double _MaxStageForArea;

        private int _TopExtrapolationPoints;
        private int _CentralInterpolationPoints;
        private int _BottomExtrapolationPoints;

        private PairedData _StageFrequency;
        #endregion

        #region Properties
        public Inventory Inventory { get; }
        public int ImpactAreaID { get; }
        public double MinStageForArea => _MinStageForArea;
        public double MaxStageForArea => _MaxStageForArea;
        public int BottomExtrapolationPoints => _BottomExtrapolationPoints;
        public int CentralInterpolationPoints => _CentralInterpolationPoints;
        public int TopExtrapolationPoints => _TopExtrapolationPoints;
        #endregion

        #region Constructor
        public ImpactAreaStageDamage(int impactAreaID, Inventory inventory, List<double> profileProbabilities, int analysisYear = 9999, ContinuousDistribution analyticalFlowFrequency = null,
            GraphicalUncertainPairedData graphicalFrequency = null, UncertainPairedData dischargeStage = null, UncertainPairedData unregulatedRegulated = null, bool usingMockData = false)
        {
            _AnalyticalFlowFrequency = analyticalFlowFrequency;
            _GraphicalFrequency = graphicalFrequency;
            _DischargeStage = dischargeStage;
            _UnregulatedRegulated = unregulatedRegulated;
            ImpactAreaID = impactAreaID;
            _AnalysisYear = analysisYear;
            if (usingMockData)
            {
                Inventory = inventory;
            }
            else
            {
                Inventory = inventory.GetInventoryTrimmedToImpactArea(impactAreaID);
            }
            _ProfileProbabilities = profileProbabilities;
            EstablishAggregationStages();
        }
        #endregion

        #region Methods
        private void EstablishAggregationStages()
        {
            _StageFrequency = IdentifyCentralStageFrequencyAtIndexLocation();
            IdentifyMinAndMaxStageWithUncertainty();
            SetCoordinateQuantity();
        }

        private void SetCoordinateQuantity()
        {
            double stageAtAEPofMostFrequentHydraulicsProfile = _StageFrequency.f(1 - _ProfileProbabilities[0]);
            double rangeOfStagesAtBottom = stageAtAEPofMostFrequentHydraulicsProfile - _MinStageForArea;
            _BottomExtrapolationPoints = Convert.ToInt32(Math.Ceiling(rangeOfStagesAtBottom / FEET_PER_COORDINATE));
            if (_BottomExtrapolationPoints < MINIMUM_EXTRAPOLATION_COORDINATES) { _BottomExtrapolationPoints = MINIMUM_EXTRAPOLATION_COORDINATES; }

            double stageAtAEPofLeastFrequentHydraulicsProfile = _StageFrequency.f(1 - _ProfileProbabilities[^1]);
            double middleRange = stageAtAEPofLeastFrequentHydraulicsProfile - stageAtAEPofMostFrequentHydraulicsProfile;
            _CentralInterpolationPoints = Convert.ToInt32(Math.Ceiling((middleRange / FEET_PER_COORDINATE) / (_ProfileProbabilities.Count - 1)));
            if (_CentralInterpolationPoints < MINIMUM_INTERPOLATION_COORDINATES) { _CentralInterpolationPoints = MINIMUM_INTERPOLATION_COORDINATES; }

            double rangeOfStagesAtTop = _MaxStageForArea - stageAtAEPofLeastFrequentHydraulicsProfile;
            _TopExtrapolationPoints = Convert.ToInt32(Math.Ceiling(rangeOfStagesAtTop / FEET_PER_COORDINATE));
            if (_TopExtrapolationPoints < MINIMUM_EXTRAPOLATION_COORDINATES) { _TopExtrapolationPoints = MINIMUM_EXTRAPOLATION_COORDINATES; }
        }

        private void IdentifyMinAndMaxStageWithUncertainty()
        {
            if (_AnalyticalFlowFrequency != null)
            {
                if (_DischargeStage != null)
                {
                    IPairedData minStagesOnRating = _DischargeStage.SamplePairedData(MIN_PROBABILITY);
                    IPairedData maxStagesOnRating = _DischargeStage.SamplePairedData(MAX_PROBABILITY);

                    double minFLow = _AnalyticalFlowFrequency.InverseCDF(MIN_PROBABILITY);
                    double maxFLow = _AnalyticalFlowFrequency.InverseCDF(MAX_PROBABILITY);

                    if (_UnregulatedRegulated != null)
                    {
                        minFLow = _UnregulatedRegulated.SamplePairedData(MIN_PROBABILITY).f(minFLow);
                        maxFLow = _UnregulatedRegulated.SamplePairedData(MAX_PROBABILITY).f(maxFLow);
                    }

                    _MinStageForArea = minStagesOnRating.f(minFLow);
                    _MaxStageForArea = maxStagesOnRating.f(maxFLow);
                }
                else
                {
                    // PATCHED: ReportMessage severed -- throw instead (see file header).
                    throw new Exception("A stage-discharge function must accompany a flow-frequency function but no such function was found. Stage-damage compute aborted");
                }
            }
            else if (_GraphicalFrequency != null)
            {
                if (_GraphicalFrequency.GraphicalDistributionWithLessSimple.UsingStagesNotFlows)
                {
                    PairedData minStages = _GraphicalFrequency.SamplePairedData(MIN_PROBABILITY);
                    _MinStageForArea = minStages.Yvals[0];
                    PairedData maxStages = _GraphicalFrequency.SamplePairedData(MAX_PROBABILITY);
                    _MaxStageForArea = maxStages.Yvals[^1];
                }
                else
                {
                    if (_DischargeStage != null)
                    {
                        PairedData minFlows = _GraphicalFrequency.SamplePairedData(MIN_PROBABILITY);
                        double minFlow = minFlows.Yvals[0];
                        PairedData maxFlows = _GraphicalFrequency.SamplePairedData(MAX_PROBABILITY);
                        double maxFlow = maxFlows.Yvals[^1];

                        if (_UnregulatedRegulated != null)
                        {
                            minFlow = _UnregulatedRegulated.SamplePairedData(MIN_PROBABILITY).f(minFlow);
                            maxFlow = _UnregulatedRegulated.SamplePairedData(MAX_PROBABILITY).f(maxFlow);
                        }

                        PairedData minStages = _DischargeStage.SamplePairedData(MIN_PROBABILITY);
                        PairedData maxStages = _DischargeStage.SamplePairedData(MAX_PROBABILITY);

                        _MinStageForArea = minStages.f(minFlow);
                        _MaxStageForArea = maxStages.f(maxFlow);
                    }
                    else
                    {
                        // PATCHED: ReportMessage severed -- throw instead (see file header).
                        throw new Exception("A stage-discharge function must accompany a flow-frequency function but no such function was found. Stage-damage compute aborted");
                    }
                }
            }
            else
            {
                // PATCHED: ReportMessage severed -- throw instead (see file header).
                throw new Exception("At this time, HEC-FDA does not allow a stage-damage compute without a frequency function. Stage-damage compute aborted");
            }
        }

        private PairedData IdentifyCentralStageFrequencyAtIndexLocation()
        {
            int fakeIterationNumberNotUsedInThisPartOfTheComputeBecauseItIsDeterministic = 1;
            bool deterministic = true;
            if (_AnalyticalFlowFrequency != null)
            {
                if (_DischargeStage != null)
                {
                    Tuple<double[], double[]> flowFreqAsTuple = _AnalyticalFlowFrequency.ToCoordinates(exceedence: false);
                    PairedData flowFrequencyPairedData = new(flowFreqAsTuple.Item1, flowFreqAsTuple.Item2);
                    if (_UnregulatedRegulated != null)
                    {
                        flowFrequencyPairedData = _UnregulatedRegulated.SamplePairedData(fakeIterationNumberNotUsedInThisPartOfTheComputeBecauseItIsDeterministic, deterministic).compose(flowFrequencyPairedData) as PairedData;
                    }
                    return _DischargeStage.SamplePairedData(fakeIterationNumberNotUsedInThisPartOfTheComputeBecauseItIsDeterministic, deterministic).compose(flowFrequencyPairedData) as PairedData;
                }
            }
            else if (_GraphicalFrequency != null)
            {
                if (_GraphicalFrequency.GraphicalDistributionWithLessSimple.UsingStagesNotFlows)
                {
                    return _GraphicalFrequency.SamplePairedData(0.5) as PairedData;
                }
                else
                {
                    if (_DischargeStage != null)
                    {
                        PairedData flowFrequencyPairedData = _GraphicalFrequency.SamplePairedData(fakeIterationNumberNotUsedInThisPartOfTheComputeBecauseItIsDeterministic, deterministic) as PairedData;
                        if (_UnregulatedRegulated != null)
                        {
                            flowFrequencyPairedData = _UnregulatedRegulated.SamplePairedData(fakeIterationNumberNotUsedInThisPartOfTheComputeBecauseItIsDeterministic, deterministic).compose(flowFrequencyPairedData) as PairedData;
                        }
                        return _DischargeStage.SamplePairedData(fakeIterationNumberNotUsedInThisPartOfTheComputeBecauseItIsDeterministic, deterministic).compose(flowFrequencyPairedData) as PairedData;
                    }
                }
            }
            return null;
        }

        public double[] ComputeStagesAtIndexLocation(List<double> profileProbabilities)
        {
            int quantityStages = _BottomExtrapolationPoints + _TopExtrapolationPoints + (profileProbabilities.Count - 1) * _CentralInterpolationPoints;
            double[] stages = new double[quantityStages];
            double stageAtProbabilityOfLowestProfile = _StageFrequency.f(1 - profileProbabilities.Max());
            float indexStationLowerStageDelta = (float)(stageAtProbabilityOfLowestProfile - _MinStageForArea);
            float interval = indexStationLowerStageDelta / _BottomExtrapolationPoints;
            int stageIndex = 0;
            for (int i = 0; i < _BottomExtrapolationPoints + 1; i++)
            {
                stages[i] = (_MinStageForArea + i * interval);
                stageIndex++;
            }

            int numProfiles = profileProbabilities.Count;
            for (int i = 1; i < numProfiles; i++)
            {
                double previousProbability = profileProbabilities[i - 1];
                double currentProbability = profileProbabilities[i];

                for (int j = 0; j < _CentralInterpolationPoints; j++)
                {
                    double previousStageAtIndexLocation = _StageFrequency.f(1 - previousProbability);
                    double currentStageAtIndexLocation = _StageFrequency.f(1 - currentProbability);
                    double stageDeltaAtIndexLocation = currentStageAtIndexLocation - previousStageAtIndexLocation;
                    double intervalAtIndexLocation = stageDeltaAtIndexLocation / _CentralInterpolationPoints;
                    double stageAtIndexLocation = previousStageAtIndexLocation + intervalAtIndexLocation * (j + 1);
                    stages[stageIndex] = stageAtIndexLocation;
                    stageIndex++;
                }
            }

            double stageAtProbabilityOfHighestProfile = _StageFrequency.f(1 - profileProbabilities.Min());
            float indexStationUpperStageDelta = (float)(_MaxStageForArea - stageAtProbabilityOfHighestProfile);
            float upperInterval = indexStationUpperStageDelta / _TopExtrapolationPoints;
            for (int i = 1; i < _TopExtrapolationPoints; i++)
            {
                stages[stageIndex] = (_MaxStageForArea - upperInterval * (_TopExtrapolationPoints - i));
                stageIndex++;
            }

            return stages;
        }

        private float CalculateLowerIncrementOfStages(List<double> profileProbabilities)
        {
            double stageAtProbabilityOfLowestProfile = _StageFrequency.f(1 - profileProbabilities.Max());
            float indexStationLowerStageDelta = (float)(stageAtProbabilityOfLowestProfile - _MinStageForArea);
            float interval = indexStationLowerStageDelta / _BottomExtrapolationPoints;
            return interval;
        }

        public static float[] ExtrapolateFromBelowStagesAtIndexLocation(float[] WSEsAtLowest, float interval, int i, int numInterpolatedStagesToCompute)
        {
            float[] extrapolatedStages = new float[WSEsAtLowest.Length];
            for (int j = 0; j < WSEsAtLowest.Length; j++)
            {
                extrapolatedStages[j] = WSEsAtLowest[j] - interval * (numInterpolatedStagesToCompute - i);
            }
            return extrapolatedStages;
        }

        private float[] CalculateIntervals(float[] previousStagesAtStructures, float[] currentStagesAtStructures)
        {
            float[] intervals = new float[previousStagesAtStructures.Length];
            for (int j = 0; j < previousStagesAtStructures.Length; j++)
            {
                intervals[j] = (currentStagesAtStructures[j] - previousStagesAtStructures[j]) / _CentralInterpolationPoints;
            }
            return intervals;
        }

        private static float[] CalculateIncrementOfStages(float[] previousStagesAtStructures, float[] intervalsAtStructures, int interpolatorIndex)
        {
            float[] stages = new float[intervalsAtStructures.Length];
            for (int m = 0; m < stages.Length; m++)
            {
                stages[m] = previousStagesAtStructures[m] + intervalsAtStructures[m] * interpolatorIndex;
            }
            return stages;
        }

        //this is public and static for testing
        public static float[] ExtrapolateFromAboveAtIndexLocation(float[] stagesAtStructuresHighestProfile, float upperInterval, int stepCount)
        {
            float[] extrapolatedStages = new float[stagesAtStructuresHighestProfile.Length];
            for (int i = 0; i < stagesAtStructuresHighestProfile.Length; i++)
            {
                extrapolatedStages[i] = stagesAtStructuresHighestProfile[i] + upperInterval * stepCount;
            }
            return extrapolatedStages;
        }
        #endregion
    }
}
