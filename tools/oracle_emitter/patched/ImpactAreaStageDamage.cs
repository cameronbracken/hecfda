// PATCHED LOCAL COPY of HEC.FDA.Model/stageDamage/ImpactAreaStageDamage.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
// Phase 4 Task 6 ported the GEOMETRY (ctor + EstablishAggregationStages and the pure static/
// instance stage-interval helpers). Phase 4 Task 7 (this patch) adds Compute() and everything it
// drives. The real source pulls in HEC.FDA.Model.hydraulics (HydraulicDataset, disk-backed
// IHydraulicProfile), HEC.MVVMFramework (PropertyValidationHelper/
// IDontImplementValidationButMyPropertiesDo base, MessageReport/ReportMessage), Utility.Progress
// (ProgressReporter), and System.Diagnostics.Stopwatch -- none reachable from / needed by this
// subset-compiled project. This patch:
//   - Drops the `: PropertyValidationHelper, IDontImplementValidationButMyPropertiesDo` base
//     entirely -- HasErrors/ErrorLevel are plain fields here instead (folded in the same way the
//     C++ port does, see impact_area_stage_damage.hpp's validate()).
//   - Replaces `HydraulicDataset _HydraulicDataset` with `List<double> _ProfileProbabilities` +
//     `List<float[]> _WsesByProfile` (RAW, uncorrected -- the hydraulics-as-arrays replacement,
//     matching the C++ port's HydraulicProfiles holding both probabilities and wses_by_profile_):
//     every `_HydraulicDataset.HydraulicProfiles.First()/Last().Probability`/`.Count()` reference
//     becomes `_ProfileProbabilities[0]`/`[^1]`/`.Count`, and Compute()'s
//     `_HydraulicDataset.GetHydraulicDatasetInFloatsWithProbabilities(...)` call becomes
//     `(_ProfileProbabilities, HydraulicDataset.CorrectAllProfiles(CloneWaterData(), GetGroundElevs()))`
//     -- CorrectAllProfiles is the same patched/HydraulicDataset.cs static helper Task 5 already
//     built, applied to a CLONE of `_WsesByProfile` each call (matching the C++ port's
//     get_corrected_wses returning a corrected copy, member never mutated -- CorrectDryStructureWSEs
//     mutates its float[] argument in place, so cloning first keeps repeated Compute() calls
//     idempotent). `hydroParentDirectory` (disk path, used only by the dropped CSV-detail methods)
//     stays dropped from the ctor signature.
//   - Replaces both `ErrorMessage`/`ReportMessage` "no frequency function found" branches with
//     `throw new Exception(message)` (same message text) -- matches the C++ port's
//     throw_missing_discharge_stage_error()/throw_no_frequency_function_error().
//   - Compute()'s own "at least one component has a major error" ErrorMessage/ReportMessage branch
//     is dropped (the still-empty `results` tuple is simply returned, matching the C++ port).
//   - ProduceImpactAreaStructureDetails/DamagesToStrings/DepthsToStrings/StagesToStrings (CSV
//     detail methods) remain dropped -- out of scope repo-wide (text/CSV formatting).
// Everything else -- the ctor, EstablishAggregationStages, IdentifyCentralStageFrequencyAtIndexLocation,
// IdentifyMinAndMaxStageWithUncertainty, SetCoordinateQuantity, ComputeStagesAtIndexLocation,
// ExtrapolateFromAboveAtIndexLocation, ExtrapolateFromBelowStagesAtIndexLocation, CalculateIntervals,
// CalculateIncrementOfStages, CalculateLowerIncrementOfStages, Compute(),
// ComputeDamageWithUncertaintyAllCoordinates, ComputeLower/Middle/UpperStageDamage +
// InterpolateBetweenProfiles, ProduceZeroDamageFunctions, CreateConsequenceDistributionResults,
// DumpDataIntoDistributions, IsTheFunctionNotConverged, Validate() -- is kept VERBATIM (minus the
// _HydraulicDataset substitution and MVVM severances described above).
using System;
using System.Collections.Generic;
using System.Linq;
using HEC.FDA.Model.hydraulics;
using HEC.FDA.Model.metrics;
using HEC.FDA.Model.paireddata;
using HEC.FDA.Model.structures;
using Statistics;
using Statistics.Histograms;

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
        private readonly ConvergenceCriteria _ConvergenceCriteria = new(minIterations: 1000, maxIterations: 5000);
        #endregion

        #region Fields
        private readonly ContinuousDistribution _AnalyticalFlowFrequency;
        private readonly GraphicalUncertainPairedData _GraphicalFrequency;
        private readonly UncertainPairedData _DischargeStage;
        private readonly UncertainPairedData _UnregulatedRegulated;
        private readonly int _AnalysisYear;
        // PATCHED: replaces HydraulicDataset _HydraulicDataset -- see file header.
        private readonly List<double> _ProfileProbabilities;
        private readonly List<float[]> _WsesByProfile;

        private double _MinStageForArea;
        private double _MaxStageForArea;

        private int _TopExtrapolationPoints;
        private int _CentralInterpolationPoints;
        private int _BottomExtrapolationPoints;

        private PairedData _StageFrequency;
        private double[] _StagesAtIndexLocation;
        #endregion

        #region Properties
        public Inventory Inventory { get; }
        public int ImpactAreaID { get; }
        public double MinStageForArea => _MinStageForArea;
        public double MaxStageForArea => _MaxStageForArea;
        public int BottomExtrapolationPoints => _BottomExtrapolationPoints;
        public int CentralInterpolationPoints => _CentralInterpolationPoints;
        public int TopExtrapolationPoints => _TopExtrapolationPoints;
        // PATCHED: PropertyValidationHelper is dropped (see file header) -- plain fields instead.
        public bool HasErrors { get; private set; }
        public MVVMFramework.Base.Enumerations.ErrorLevel ErrorLevel { get; private set; }
        #endregion

        #region Constructor
        public ImpactAreaStageDamage(int impactAreaID, Inventory inventory, List<double> profileProbabilities, List<float[]> wsesByProfile, int analysisYear = 9999, ContinuousDistribution analyticalFlowFrequency = null,
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
            _WsesByProfile = wsesByProfile;
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

        // ---- PHASE 4 TASK 7: Compute() and everything it drives -----------------------------
        // Kept VERBATIM against the real ImpactAreaStageDamage.cs (see file header for the
        // _HydraulicDataset -> _ProfileProbabilities/_WsesByProfile substitution and the dropped
        // ProgressReporter/Stopwatch parameters/MVVM ReportMessage branches).

        public (List<UncertainPairedData>, List<UncertainPairedData>) Compute(bool computeIsDeterministic = false)
        {
            Validate();
            (List<UncertainPairedData>, List<UncertainPairedData>) results = new(new List<UncertainPairedData>(), new List<UncertainPairedData>());
            if (ErrorLevel >= MVVMFramework.Base.Enumerations.ErrorLevel.Major)
            {
                // PATCHED: ReportMessage severed (see file header) -- just return the empty results.
                return results;
            }
            else
            {
                Inventory.GenerateRandomNumbers(_ConvergenceCriteria);
                List<string> damCats = Inventory.GetDamageCategories();
                if (Inventory.Structures.Count == 0)
                {
                    results = (ProduceZeroDamageFunctions());
                    return results;
                }
                else
                {
                    // PATCHED: replaces `_HydraulicDataset.GetHydraulicDatasetInFloatsWithProbabilities(...)`
                    // -- see file header. CloneWaterData() clones _WsesByProfile so CorrectAllProfiles'
                    // in-place mutation doesn't corrupt the stored raw arrays across repeated Compute() calls.
                    (List<double>, List<float[]>) wsesAtEachStructureByProfile = (_ProfileProbabilities, HydraulicDataset.CorrectAllProfiles(CloneWaterData(), Inventory.GetGroundElevations()));
                    _StagesAtIndexLocation = ComputeStagesAtIndexLocation(wsesAtEachStructureByProfile.Item1);
                    //Run the compute by dam cat to simplify data collection
                    foreach (string damageCategory in damCats)
                    {
                        (Inventory, List<float[]>) inventoryAndWaterTupled = Inventory.GetInventoryAndWaterTrimmedToDamageCategory(damageCategory, wsesAtEachStructureByProfile.Item2);

                        //There will be one ConsequenceDistributionResults object for each stage in the stage-damage function
                        //Each ConsequenceDistributionResults object holds a ConsequenceDistributionResult for each asset cat
                        List<StudyAreaConsequencesBinned> consequenceDistributionResults = ComputeDamageWithUncertaintyAllCoordinates(damageCategory, inventoryAndWaterTupled, wsesAtEachStructureByProfile.Item1, computeIsDeterministic);

                        //there should be four UncertainPairedData objects - one for each asset cat of the given dam cat level compute
                        (List<UncertainPairedData>, List<UncertainPairedData>) tempResultsList = StudyAreaConsequencesBinned.ToUncertainPairedData(_StagesAtIndexLocation.ToList(), consequenceDistributionResults, ImpactAreaID);
                        //damage
                        results.Item1.AddRange(tempResultsList.Item1);
                        //quantity damaged elements
                        results.Item2.AddRange(tempResultsList.Item2);
                    }
                    return results;
                }
            }
        }

        // PATCHED helper: clones _WsesByProfile (each profile's float[] row) so
        // HydraulicDataset.CorrectAllProfiles's in-place mutation never corrupts the stored raw
        // arrays across repeated Compute() calls -- see file header.
        private List<float[]> CloneWaterData()
        {
            List<float[]> clone = new();
            foreach (float[] profile in _WsesByProfile)
            {
                clone.Add((float[])profile.Clone());
            }
            return clone;
        }

        private (List<UncertainPairedData>, List<UncertainPairedData>) ProduceZeroDamageFunctions()
        {
            (List<UncertainPairedData>, List<UncertainPairedData>) zeroResults = new();
            IHistogram[] deterministics = new IHistogram[_StageFrequency.Yvals.Count];
            for (int i = 0; i < deterministics.Length; i++)
            {
                //this histogram is zero-valued
                deterministics[i] = new DynamicHistogram();
            }
            string damcat = "NO STRUCTURES";
            CurveMetaData structureMetaData = new CurveMetaData(name: "stage-damage function", xlabel: "stages", ylabel: "no structures", impactAreaID: ImpactAreaID, damageCategory: damcat, assetCategory: utilities.StringGlobalConstants.STRUCTURE_ASSET_CATEGORY);
            CurveMetaData contentMetaData = new CurveMetaData(name: "stage-damage function", xlabel: "stages", ylabel: "no structures", impactAreaID: ImpactAreaID, damageCategory: damcat, assetCategory: utilities.StringGlobalConstants.CONTENT_ASSET_CATEGORY);
            CurveMetaData otherMetaData = new CurveMetaData(name: "stage-damage function", xlabel: "stages", ylabel: "no structures", impactAreaID: ImpactAreaID, damageCategory: damcat, assetCategory: utilities.StringGlobalConstants.OTHER_ASSET_CATEGORY);
            CurveMetaData vehicleMetaData = new CurveMetaData(name: "stage-damage function", xlabel: "stages", ylabel: "no structures", impactAreaID: ImpactAreaID, damageCategory: damcat, assetCategory: utilities.StringGlobalConstants.VEHICLE_ASSET_CATEGORY);

            UncertainPairedData structure = new UncertainPairedData(_StageFrequency.Yvals.ToArray(), deterministics, structureMetaData);
            UncertainPairedData content = new UncertainPairedData(_StageFrequency.Yvals.ToArray(), deterministics, contentMetaData);
            UncertainPairedData other = new UncertainPairedData(_StageFrequency.Yvals.ToArray(), deterministics, otherMetaData);
            UncertainPairedData vehicle = new UncertainPairedData(_StageFrequency.Yvals.ToArray(), deterministics, vehicleMetaData);
            List<UncertainPairedData> zeros = new List<UncertainPairedData> { structure, content, other, vehicle };
            zeroResults.Item1 = (zeros);
            zeroResults.Item2 = (zeros);
            return zeroResults;
        }

        private List<StudyAreaConsequencesBinned> ComputeDamageWithUncertaintyAllCoordinates(
            string damageCategory,
            (Inventory, List<float[]>) inventoryAndWaterTupled,
            List<double> profileProbabilities,
            bool computeIsDeterministic)
        {
            //damage for each stage
            List<StudyAreaConsequencesBinned> consequenceDistributionResults = CreateConsequenceDistributionResults(damageCategory);
            int iterationsPerComputeChunk = _ConvergenceCriteria.IterationCount;
            int computeChunkQuantity = Convert.ToInt32(_ConvergenceCriteria.MinIterations / iterationsPerComputeChunk);
            int sampleSize = 0;
            bool stageDamageFunctionsAreNotConverged = true;

            while (stageDamageFunctionsAreNotConverged)
            {
                for (int computeChunk = 0; computeChunk < computeChunkQuantity; computeChunk++)
                {
                    for (int thisChunkIteration = 0; thisChunkIteration < iterationsPerComputeChunk; thisChunkIteration++)
                    {
                        //this is the only sampling taking place in the aggregated stage-damage compute with uncertainty
                        //the sampling takes place by the overall compute iteration number so that for each iteration the same random numbers are retrieved
                        int thisComputeIteration = computeChunk * iterationsPerComputeChunk + thisChunkIteration;
                        List<DeterministicOccupancyType> deterministicOccTypes = Inventory.SampleOccupancyTypes(thisComputeIteration, computeIsDeterministic);

                        //iteration counts in the following method calls are only used for saving results in temp results arrays
                        ComputeLowerStageDamage(ref consequenceDistributionResults, damageCategory, deterministicOccTypes, inventoryAndWaterTupled, profileProbabilities, thisChunkIteration);
                        ComputeMiddleStageDamage(ref consequenceDistributionResults, damageCategory, deterministicOccTypes, inventoryAndWaterTupled, profileProbabilities, thisChunkIteration);
                        ComputeUpperStageDamage(ref consequenceDistributionResults, damageCategory, deterministicOccTypes, inventoryAndWaterTupled, profileProbabilities, thisChunkIteration);
                        inventoryAndWaterTupled.Item1.ResetStructureWaterIndexTracking();
                        sampleSize += 1;
                    }
                    DumpDataIntoDistributions(ref consequenceDistributionResults);
                }
                stageDamageFunctionsAreNotConverged = IsTheFunctionNotConverged(consequenceDistributionResults);
                if (stageDamageFunctionsAreNotConverged)
                {
                    //TODO: I am going to hard-wire in an additional 10000 iterations for now.
                    //At some point we can estimate iterations remaining - but that is computationally expensive
                    computeChunkQuantity = 100;
                }
            }
            return consequenceDistributionResults;
        }

        private static void DumpDataIntoDistributions(ref List<StudyAreaConsequencesBinned> consequenceDistributionResultsList)
        {
            foreach (StudyAreaConsequencesBinned consequenceDistributionResults in consequenceDistributionResultsList)
            {
                consequenceDistributionResults.PutDataIntoHistograms();
            }
        }

        private List<StudyAreaConsequencesBinned> CreateConsequenceDistributionResults(string damageCategory)
        {
            List<StudyAreaConsequencesBinned> consequenceDistributionResultsList = new();

            for (int i = 0; i < _StagesAtIndexLocation.Length; i++)
            {
                List<AggregatedConsequencesBinned> consequenceDistributionResultList = new()
                {
                    new(damageCategory, utilities.StringGlobalConstants.STRUCTURE_ASSET_CATEGORY, _ConvergenceCriteria, ImpactAreaID, ConsequenceType.Damage),
                    new(damageCategory, utilities.StringGlobalConstants.CONTENT_ASSET_CATEGORY, _ConvergenceCriteria, ImpactAreaID, ConsequenceType.Damage),
                    new(damageCategory, utilities.StringGlobalConstants.OTHER_ASSET_CATEGORY, _ConvergenceCriteria, ImpactAreaID, ConsequenceType.Damage),
                    new(damageCategory, utilities.StringGlobalConstants.VEHICLE_ASSET_CATEGORY, _ConvergenceCriteria, ImpactAreaID, ConsequenceType.Damage)
                };
                StudyAreaConsequencesBinned consequenceDistributionResults = new(consequenceDistributionResultList);
                consequenceDistributionResultsList.Add(consequenceDistributionResults);
            }
            return consequenceDistributionResultsList;
        }

        private static bool IsTheFunctionNotConverged(List<StudyAreaConsequencesBinned> consequenceDistributionResults)
        {
            double lowerProb = 0.025;
            double upperProb = 0.975;
            foreach (StudyAreaConsequencesBinned consequences in consequenceDistributionResults)
            {
                bool isConverged = consequences.ResultsAreConverged(upperProb, lowerProb);
                if (!isConverged)
                {
                    return true;
                }
            }
            return false;
        }

        private void ComputeLowerStageDamage(ref List<StudyAreaConsequencesBinned> parallelConsequenceResultCollection, string damageCategory, List<DeterministicOccupancyType> deterministicOccTypes, (Inventory, List<float[]>) inventoryAndWaterCoupled, List<double> profileProbabilities, int thisChunkIteration)
        {
            float interval = CalculateLowerIncrementOfStages(profileProbabilities);
            List<float[]> stagesAtAllStructuresAllEvents = new();
            for (int stageIndex = 0; stageIndex < _BottomExtrapolationPoints + 1; stageIndex++)
            {
                float[] WSEsParallelToIndexLocation = ExtrapolateFromBelowStagesAtIndexLocation(inventoryAndWaterCoupled.Item2[0], interval, stageIndex, _BottomExtrapolationPoints);
                stagesAtAllStructuresAllEvents.Add(WSEsParallelToIndexLocation);
            }
            List<ConsequenceResult> consequenceResults = inventoryAndWaterCoupled.Item1.ComputeDamages(stagesAtAllStructuresAllEvents, _AnalysisYear, damageCategory, deterministicOccTypes);
            int i = 0;
            foreach (ConsequenceResult consequenceResult in consequenceResults)
            {
                parallelConsequenceResultCollection[i].AddConsequenceRealization(consequenceResult, damageCategory, ImpactAreaID, thisChunkIteration);
                i++;
            }
        }

        private void ComputeMiddleStageDamage(ref List<StudyAreaConsequencesBinned> parallelConsequenceResultCollection, string damageCategory, List<DeterministicOccupancyType> deterministicOccTypes, (Inventory, List<float[]>) inventoryAndWaterCoupled, List<double> profileProbabilities, int thisChunkIteration)
        {
            int numProfiles = profileProbabilities.Count;
            int stageIndex = _BottomExtrapolationPoints + 1;
            for (int profileIndex = 1; profileIndex < numProfiles; profileIndex++)
            {
                InterpolateBetweenProfiles(ref parallelConsequenceResultCollection, deterministicOccTypes, inventoryAndWaterCoupled.Item2[profileIndex - 1], inventoryAndWaterCoupled.Item2[profileIndex], damageCategory, inventoryAndWaterCoupled.Item1, stageIndex, thisChunkIteration);
                stageIndex += _CentralInterpolationPoints;
            }
        }

        private void InterpolateBetweenProfiles(ref List<StudyAreaConsequencesBinned> parallelConsequenceResultCollection, List<DeterministicOccupancyType> occTypes, float[] previousHydraulicProfile, float[] currentHydraulicProfile, string damageCategory, Inventory inventory, int stageIndex, int thisChunkIteration)
        {
            float[] intervalsAtStructures = CalculateIntervals(previousHydraulicProfile, currentHydraulicProfile);
            List<float[]> stagesAllStructuresAllStages = new();
            for (int interpolatorIndex = 0; interpolatorIndex < _CentralInterpolationPoints; interpolatorIndex++)
            {
                float[] stages = CalculateIncrementOfStages(previousHydraulicProfile, intervalsAtStructures, interpolatorIndex + 1);
                stagesAllStructuresAllStages.Add(stages);
            }
            int i = 0;
            List<ConsequenceResult> consequenceResults = inventory.ComputeDamages(stagesAllStructuresAllStages, _AnalysisYear, damageCategory, occTypes);
            foreach (ConsequenceResult consequenceResult in consequenceResults)
            {
                parallelConsequenceResultCollection[stageIndex + i].AddConsequenceRealization(consequenceResult, damageCategory, ImpactAreaID, thisChunkIteration);
                i++;
            }
        }

        private void ComputeUpperStageDamage(ref List<StudyAreaConsequencesBinned> parallelConsequenceResultCollection, string damageCategory, List<DeterministicOccupancyType> deterministicOccTypes, (Inventory, List<float[]>) inventoryAndWaterCoupled, List<double> profileProbabilities, int thisChunkIteration)
        {
            int stageIndex = _BottomExtrapolationPoints + _CentralInterpolationPoints * (profileProbabilities.Count - 1);
            double stageAtProbabilityOfHighestProfile = _StageFrequency.f(1 - profileProbabilities.Min());
            float indexStationUpperStageDelta = (float)(_MaxStageForArea - stageAtProbabilityOfHighestProfile);
            float upperInterval = indexStationUpperStageDelta / _TopExtrapolationPoints;

            List<float[]> stagesAllStructuresAllEvents = new();
            for (int extrapolatorIndex = 1; extrapolatorIndex < _TopExtrapolationPoints; extrapolatorIndex++)
            {
                float[] WSEsParallelToIndexLocation = ExtrapolateFromAboveAtIndexLocation(inventoryAndWaterCoupled.Item2[^1], upperInterval, extrapolatorIndex);
                stagesAllStructuresAllEvents.Add(WSEsParallelToIndexLocation);
            }
            List<ConsequenceResult> consequenceResults = inventoryAndWaterCoupled.Item1.ComputeDamages(stagesAllStructuresAllEvents, _AnalysisYear, damageCategory, deterministicOccTypes);
            int i = 1;
            foreach (ConsequenceResult consequenceResult in consequenceResults)
            {
                parallelConsequenceResultCollection[stageIndex + i].AddConsequenceRealization(consequenceResult, damageCategory, ImpactAreaID, thisChunkIteration);
                i++;
            }
        }

        // PATCHED: PropertyValidationHelper base is dropped (see file header) -- Validate() sets
        // the plain HasErrors/ErrorLevel properties directly instead of via ValidateProperty/
        // ResetErrors. Otherwise transcribed verbatim.
        public void Validate()
        {
            HasErrors = false;
            ErrorLevel = MVVMFramework.Base.Enumerations.ErrorLevel.Unassigned;
            if (_AnalyticalFlowFrequency != null) { _AnalyticalFlowFrequency.Validate(); if (_AnalyticalFlowFrequency.HasErrors) { HasErrors = true; if (ErrorLevel < _AnalyticalFlowFrequency.ErrorLevel) { ErrorLevel = _AnalyticalFlowFrequency.ErrorLevel; } } }
            if (_GraphicalFrequency != null) { _GraphicalFrequency.Validate(); if (_GraphicalFrequency.HasErrors) { HasErrors = true; if (ErrorLevel < _GraphicalFrequency.ErrorLevel) { ErrorLevel = _GraphicalFrequency.ErrorLevel; } } }
            if (_DischargeStage != null) { _DischargeStage.Validate(); if (_DischargeStage.HasErrors) { HasErrors = true; if (ErrorLevel < _DischargeStage.ErrorLevel) { ErrorLevel = _DischargeStage.ErrorLevel; } } }
            if (_UnregulatedRegulated != null) { _UnregulatedRegulated.Validate(); if (_UnregulatedRegulated.HasErrors) { HasErrors = true; if (ErrorLevel < _UnregulatedRegulated.ErrorLevel) { ErrorLevel = _UnregulatedRegulated.ErrorLevel; } } }
            if (_GraphicalFrequency == null)
            {
                if (_AnalyticalFlowFrequency == null)
                {
                    HasErrors = true;
                    ErrorLevel = MVVMFramework.Base.Enumerations.ErrorLevel.Fatal;
                }
            }
            Inventory.Validate();
            if (Inventory.ErrorLevel > ErrorLevel)
            {
                HasErrors = true;
                ErrorLevel = Inventory.ErrorLevel;
            }
        }
        #endregion
    }
}
