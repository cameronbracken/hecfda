// PATCHED LOCAL COPY of HEC.FDA.Model/metrics/StudyAreaConsequencesBinned.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 4 Task 4. Kept VERBATIM: both "dummy" ctors, the (List<AggregatedConsequencesBinned>)
// ctor ("public for testing"), AddNewConsequenceResultObject, AddExistingConsequenceResultObject,
// both AddConsequenceRealization overloads, PutDataIntoHistograms, Equals, GetSpecificHistogram
// (including its MVVM Fatal-ErrorMessage-on-miss fallback -- never actually hit by this task's
// fixture, kept as-is rather than stripped since it compiles fine transitively, same as
// OccupancyType/ValueUncertainty above), RemainingIterations, ResultsAreConverged,
// ToUncertainPairedData, GetAssetCategories, GetDamageCategories, and GetConsequenceResult -- the
// whole compute/convergence/collection path this task ports to C++.
//
// Dropped (mirroring the C++ port's study_area_consequences_binned.hpp SEVERANCES list):
//  - WriteToXML()/ReadFromXML(XElement): GENUINE COMPILE BLOCKER, not just scope-trim --
//    WriteToXML calls `damageResult.WriteToXML()` and ReadFromXML calls
//    `AggregatedConsequencesBinned.ReadFromXML(...)`, and patched/AggregatedConsequencesBinned.cs
//    (Task 3) already dropped both of those methods, so this class's own XML methods would no
//    longer compile even if kept. Also needs System.Xml.Linq (dropped below, only used here).
//  - GetAggregateEmpiricalDistribution(...) / ConsequenceExceededWithProbabilityQ(...) (the
//    "Aggregation" region, minus GetConsequenceResult which is kept -- it's required by the
//    compute path): NOT hard compile blockers (Empirical/DynamicHistogram.
//    ConvertToEmpiricalDistribution live in the real, unpatched Statistics.dll referenced via
//    ProjectReference, and FilterByCategories is patched in below), but dropped to match the C++
//    port's scope -- neither is ported (see study_area_consequences_binned.hpp's SEVERANCES for
//    the rationale: no fixture/produced-interface need, trivial to add later).
//  - SampleMeanDamage(...) was ALSO dropped by the original Phase 4 Task 4 patch (same rationale
//    as the two methods above), but is ADDED BACK here by Phase 5 Task 6:
//    ImpactAreaScenarioResults.MeanExpectedAnnualConsequences needs it (see
//    patched/ImpactAreaScenarioResults.cs). Kept VERBATIM from the real source.
//
// Phase 6 Task 4 RESTORED: ConvertToStudyAreaConsequencesByQuantile(...) -- was a genuine compile
// blocker for the original Phase 4 Task 4 patch (AggregatedConsequencesByQuantile/
// StudyAreaConsequencesByQuantile weren't compiled into this project yet, and
// AggregatedConsequencesBinned.ConvertToSingleEmpiricalDistributionOfConsequences was dropped by
// the Task 3 patch). All three now exist (Phase 6 Tasks 2/3/4), so this method is restored
// VERBATIM below.
using Statistics;
using Statistics.Distributions;
using Statistics.Histograms;
using HEC.FDA.Model.metrics.Extensions;
using HEC.FDA.Model.paireddata;
using HEC.MVVMFramework.Base.Events;
using HEC.MVVMFramework.Base.Implementations;
using HEC.MVVMFramework.Model.Messaging;
using System.Collections.Generic;
using System.Linq;

namespace HEC.FDA.Model.metrics;

public class StudyAreaConsequencesBinned : ValidationErrorLogger
{
    public List<AggregatedConsequencesBinned> ConsequenceResultList { get; }
    public bool IsNull { get; }

    #region Constructors
    public StudyAreaConsequencesBinned(int impactAreaID)
    {
        ConsequenceResultList = [];
        AggregatedConsequencesBinned dummyConsequenceDistributionResult = new(impactAreaID, ConsequenceType.UNASSIGNED, RiskType.Fail);
        ConsequenceResultList.Add(dummyConsequenceDistributionResult);
        IsNull = true;
    }
    public StudyAreaConsequencesBinned(bool isNull)
    {
        ConsequenceResultList = [];
        IsNull = isNull;
    }

    //public for testing
    public StudyAreaConsequencesBinned(List<AggregatedConsequencesBinned> damageResults)
    {
        ConsequenceResultList = damageResults;
        IsNull = false;
    }
    #endregion

    #region Methods
    internal void AddNewConsequenceResultObject(string damageCategory, string assetCategory, ConvergenceCriteria convergenceCriteria, int impactAreaID, ConsequenceType consequenceType, RiskType riskType)
    {
        AggregatedConsequencesBinned existingResult = GetConsequenceResult(damageCategory, assetCategory, impactAreaID, consequenceType, riskType);
        if (existingResult == null)
        {
            AggregatedConsequencesBinned newResult = new(damageCategory, assetCategory, convergenceCriteria, impactAreaID, consequenceType, riskType);
            ConsequenceResultList.Add(newResult);
        }
    }
    public void AddExistingConsequenceResultObject(AggregatedConsequencesBinned consequenceResultToAdd)
    {
        AggregatedConsequencesBinned consequenceResult = GetConsequenceResult(consequenceResultToAdd.DamageCategory, consequenceResultToAdd.AssetCategory, consequenceResultToAdd.RegionID, consequenceResultToAdd.ConsequenceType, consequenceResultToAdd.RiskType);
        if (consequenceResult == null)
        {
            ConsequenceResultList.Add(consequenceResultToAdd);
        }
    }
    internal void AddConsequenceRealization(double damageEstimate, string damageCategory, string assetCategory, int impactAreaID, long iteration, ConsequenceType consequenceType, RiskType riskType)
    {
        AggregatedConsequencesBinned damageResult = GetConsequenceResult(damageCategory, assetCategory, impactAreaID, consequenceType, riskType);
        damageResult.AddConsequenceRealization(damageEstimate, iteration);
    }
    internal void AddConsequenceRealization(ConsequenceResult consequenceResult, string damageCategory, int impactAreaID, int iteration)
    {
        GetConsequenceResult(damageCategory, utilities.StringGlobalConstants.STRUCTURE_ASSET_CATEGORY, impactAreaID).AddConsequenceRealization(consequenceResult.StructureDamage, iteration, consequenceResult.DamagedStructuresQuantity);
        GetConsequenceResult(damageCategory, utilities.StringGlobalConstants.CONTENT_ASSET_CATEGORY, impactAreaID).AddConsequenceRealization(consequenceResult.ContentDamage, iteration, consequenceResult.DamagedContentsQuantity);
        GetConsequenceResult(damageCategory, utilities.StringGlobalConstants.VEHICLE_ASSET_CATEGORY, impactAreaID).AddConsequenceRealization(consequenceResult.VehicleDamage, iteration, consequenceResult.DamagedVehiclesQuantity);
        GetConsequenceResult(damageCategory, utilities.StringGlobalConstants.OTHER_ASSET_CATEGORY, impactAreaID).AddConsequenceRealization(consequenceResult.OtherDamage, iteration, consequenceResult.DamagedOthersQuantity);
    }
    public void PutDataIntoHistograms()
    {
        foreach (AggregatedConsequencesBinned consequenceDistributionResult in ConsequenceResultList)
        {
            consequenceDistributionResult.PutDataIntoHistogram();
        }
    }

    public bool Equals(StudyAreaConsequencesBinned inputDamageResults)
    {
        foreach (AggregatedConsequencesBinned damageResult in ConsequenceResultList)
        {
            AggregatedConsequencesBinned inputDamageResult = inputDamageResults.GetConsequenceResult(damageResult.DamageCategory, damageResult.AssetCategory, damageResult.RegionID, damageResult.ConsequenceType, damageResult.RiskType);
            if (inputDamageResult == null)
                return false;

            bool resultsMatch = damageResult.Equals(inputDamageResult);
            if (!resultsMatch)
            {
                return false;
            }
        }
        return true;
    }

    public IHistogram GetSpecificHistogram(string damageCategory, string assetCategory, int impactAreaID, bool getQuantityHistogram = false)
    {
        IHistogram returnHistogram = null;
        foreach (AggregatedConsequencesBinned consequenceDistributionResult in ConsequenceResultList)
        {
            if (consequenceDistributionResult.DamageCategory == damageCategory)
            {
                if (consequenceDistributionResult.AssetCategory == assetCategory)
                {
                    if (consequenceDistributionResult.RegionID == impactAreaID)
                    {
                        if (getQuantityHistogram)
                        {
                            returnHistogram = consequenceDistributionResult.DamagedElementQuantityHistogram;
                        }
                        else
                        {
                            returnHistogram = consequenceDistributionResult.ConsequenceHistogram;
                        }
                    }
                }
            }
        }
        if (returnHistogram == null)
        {
            string message = "The requested damage category - asset category - impact area combination could not be found. An arbitrary object is being returned";
            ErrorMessage errorMessage = new(message, MVVMFramework.Base.Enumerations.ErrorLevel.Fatal);
            ReportMessage(this, new MessageEventArgs(errorMessage));
            returnHistogram = new DynamicHistogram();
        }
        return returnHistogram;
    }

    internal long RemainingIterations(double upperProb, double lowerProb)
    {
        List<long> stageDamageIterationsRemaining = [];

        foreach (AggregatedConsequencesBinned consequenceDistributionResult in ConsequenceResultList)
        {
            if (consequenceDistributionResult.ConsequenceHistogram.HistogramIsZeroValued)
            {
                stageDamageIterationsRemaining.Add(0);
            }
            else
            {
                stageDamageIterationsRemaining.Add(consequenceDistributionResult.ConsequenceHistogram.EstimateIterationsRemaining(upperProb, lowerProb));
            }
        }
        return stageDamageIterationsRemaining.Max();
    }

    public bool ResultsAreConverged(double upperConfidenceLimit, double lowerConfidenceLimit)
    {
        bool allHistogramsAreConverged = true;
        foreach (AggregatedConsequencesBinned consequenceDistributionResult in ConsequenceResultList)
        {
            bool histogramIsConverged = consequenceDistributionResult.ConsequenceHistogram.IsHistogramConverged(upperConfidenceLimit, lowerConfidenceLimit);
            if (!histogramIsConverged)
            {
                allHistogramsAreConverged = false;
                break;
            }
        }
        return allHistogramsAreConverged;
    }

    public static (List<UncertainPairedData>, List<UncertainPairedData>) ToUncertainPairedData(List<double> xValues, List<StudyAreaConsequencesBinned> yValues, int impactAreaID)
    {
        (List<UncertainPairedData>, List<UncertainPairedData>) uncertainPairedDataList = new([], []);
        List<string> damageCategories = yValues[^1].GetDamageCategories();
        List<string> assetCategories = yValues[^1].GetAssetCategories();

        foreach (string damageCategory in damageCategories)
        {
            foreach (string assetCategory in assetCategories)
            {
                CurveMetaData damageCurveMetaData = new("X Values", "Consequences", "Consequences Uncertain Paired Data", damageCategory, impactAreaID, assetCategory);
                List<IHistogram> damageHistograms = [];

                CurveMetaData quantityCurveMetaData = new("X Values", "Damaged Elements Quantity", "Damaged Elements Quantity Uncertain Paired Data", damageCategory, impactAreaID, assetCategory);
                List<IHistogram> quantityHistograms = [];

                foreach (StudyAreaConsequencesBinned consequenceDistributions in yValues)
                {
                    IHistogram histogram = consequenceDistributions.GetSpecificHistogram(damageCategory, assetCategory, impactAreaID);
                    damageHistograms.Add(histogram);

                    IHistogram quantityHistogram = consequenceDistributions.GetSpecificHistogram(damageCategory, assetCategory, impactAreaID, getQuantityHistogram: true);
                    quantityHistograms.Add(quantityHistogram);
                }

                UncertainPairedData damageUncertainPairedData = new(xValues.ToArray(), damageHistograms.ToArray(), damageCurveMetaData);
                UncertainPairedData quantityUncertainPairedData = new(xValues.ToArray(), quantityHistograms.ToArray(), quantityCurveMetaData);

                uncertainPairedDataList.Item1.Add(damageUncertainPairedData);
                uncertainPairedDataList.Item2.Add(quantityUncertainPairedData);
            }
        }
        return uncertainPairedDataList;
    }

    private List<string> GetAssetCategories()
    {
        List<string> assetCategories = [];
        foreach (AggregatedConsequencesBinned consequenceDistributionResult in ConsequenceResultList)
        {
            if (!assetCategories.Contains(consequenceDistributionResult.AssetCategory))
            {
                assetCategories.Add(consequenceDistributionResult.AssetCategory);
            }
        }
        return assetCategories;
    }

    private List<string> GetDamageCategories()
    {
        List<string> damageCategories = [];
        foreach (AggregatedConsequencesBinned consequenceDistributionResult in ConsequenceResultList)
        {
            if (!damageCategories.Contains(consequenceDistributionResult.DamageCategory))
            {
                damageCategories.Add(consequenceDistributionResult.DamageCategory);
            }
        }
        return damageCategories;
    }

    public AggregatedConsequencesBinned GetConsequenceResult(string damageCategory, string assetCategory, int impactAreaID = utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE, ConsequenceType consequenceType = ConsequenceType.Damage, RiskType riskType = RiskType.Fail)
    {
        AggregatedConsequencesBinned result = ConsequenceResultList
            .FilterByCategories(damageCategory, assetCategory, impactAreaID, consequenceType, riskType)
            .FirstOrDefault();
        return result;
    }

    // Added back by Phase 5 Task 6 -- see the header comment's SampleMeanDamage bullet.
    public double SampleMeanDamage(string damageCategory = null, string assetCategory = null, int impactAreaID = utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE, ConsequenceType consequenceType = ConsequenceType.Damage, RiskType riskType = RiskType.Fail)
    {
        return ConsequenceResultList
            .FilterByCategories(damageCategory, assetCategory, impactAreaID, consequenceType, riskType)
            .Sum(result => result.SampleMeanExpectedAnnualConsequences());
    }

    // Added back by Phase 6 Task 4 -- see the header comment's RESTORED bullet.
    public static StudyAreaConsequencesByQuantile ConvertToStudyAreaConsequencesByQuantile(StudyAreaConsequencesBinned studyAreaConsequencesBinned, ConsequenceType filterByConsequenceType)
    {
        List<AggregatedConsequencesByQuantile> aggregatedConsequencesByQuantiles = [];

        //here we apply the filter.
        var res = studyAreaConsequencesBinned.ConsequenceResultList.Where((r) => r.ConsequenceType == filterByConsequenceType).ToArray();

        foreach (AggregatedConsequencesBinned aggregatedConsequencesBinned in res)
        {
            AggregatedConsequencesByQuantile aggregatedConsequencesByQuantile = AggregatedConsequencesBinned.ConvertToSingleEmpiricalDistributionOfConsequences(aggregatedConsequencesBinned);
            aggregatedConsequencesByQuantiles.Add(aggregatedConsequencesByQuantile);
        }
        StudyAreaConsequencesByQuantile studyAreaConsequencesByQuantile = new(aggregatedConsequencesByQuantiles);
        return studyAreaConsequencesByQuantile;
    }
    #endregion
}
