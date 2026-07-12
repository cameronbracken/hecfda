// PATCHED LOCAL COPY of HEC.FDA.Model/metrics/ImpactAreaScenarioResults.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 5 Task 6. Kept VERBATIM: both public ctors, all Properties, MeanAEP/MedianAEP/
// AEPWithGivenAssurance/AssuranceOfAEP/GetAEPHistogramForPlotting/LongTermExceedanceProbability/
// AssuranceOfEvent, MeanExpectedAnnualConsequences, GetSpecificHistogram, ResultsAreConverged/
// PerformanceResultsAreConverged/ConsequenceResultsAreConverged/RemainingIterations/
// ParallelResultsAreConverged, Equals, GetOrCreateUncertainConsequenceFrequencyCurve (INCLUDING its
// `lock (_uncertainCurveLock)` -- not a compile blocker, kept as real C# behaves; the C++ port
// drops the lock as a serial-loop simplification, documented in
// impact_area_scenario_results.hpp, but this emitter copy stays faithful to the real class), and
// PutUncertainFrequencyCurvesIntoHistograms -- the whole compute-output-container surface this
// task ports to C++.
//
// Dropped:
//  - The private (PerformanceByThresholds, StudyAreaConsequencesBinned, int) ctor: exists solely
//    to feed ReadFromXML's reconstruction, dropped alongside it.
//  - WriteToXml()/static ReadFromXML(XElement): GENUINE COMPILE BLOCKER -- needs System.Xml.Linq
//    plus PerformanceByThresholds.WriteToXML/StudyAreaConsequencesBinned.WriteToXML/
//    CategoriedUncertainPairedData.WriteToXML, all already dropped by their own patched/ copies
//    (Phase5T3/Phase4T4/Phase5T4). Also drops the now-unused `using System.Xml.Linq;`.
using Statistics;
using Statistics.Histograms;
using System;
using System.Collections.Generic;
using System.Linq;

namespace HEC.FDA.Model.metrics
{
    public class ImpactAreaScenarioResults
    {
        #region Properties
        public PerformanceByThresholds PerformanceByThresholds { get; set; }
        public StudyAreaConsequencesBinned ConsequenceResults { get; }
        public int ImpactAreaID { get; }
        public bool IsNull { get; }
        public List<CategoriedPairedData> ConsequenceFrequencyFunctions { get; set; } = [];
        public List<CategoriedUncertainPairedData> UncertainConsequenceFrequencyCurves { get; set; } = [];
        #endregion
        #region Constructors
        public ImpactAreaScenarioResults(int impactAreaID, bool isNull)
        {
            PerformanceByThresholds = new PerformanceByThresholds(true);
            ConsequenceResults = new StudyAreaConsequencesBinned(impactAreaID);
            ImpactAreaID = impactAreaID;
            IsNull = isNull;
        }
        public ImpactAreaScenarioResults(int impactAreaID)
        {
            PerformanceByThresholds = new PerformanceByThresholds();
            ConsequenceResults = new StudyAreaConsequencesBinned(false);
            ImpactAreaID = impactAreaID;
            IsNull = false;
        }
        #endregion
        #region Methods
        public double MeanAEP(int thresholdID)
        {
            return PerformanceByThresholds.GetThreshold(thresholdID).SystemPerformanceResults.MeanAEP();
        }
        public double MedianAEP(int thresholdID)
        {
            return PerformanceByThresholds.GetThreshold(thresholdID).SystemPerformanceResults.MedianAEP();
        }
        public double AEPWithGivenAssurance(int thresholdID, double assurance)
        {
            return PerformanceByThresholds.GetThreshold(thresholdID).SystemPerformanceResults.AEPWithGivenAssurance(assurance);
        }
        public double AssuranceOfAEP(int thresholdID, double exceedanceProbability)
        {
            return PerformanceByThresholds.GetThreshold(thresholdID).SystemPerformanceResults.AssuranceOfAEP(exceedanceProbability);
        }
        public DynamicHistogram GetAEPHistogramForPlotting(int thresholdID)
        {
            return PerformanceByThresholds.GetThreshold(thresholdID).SystemPerformanceResults.GetAEPHistogram();
        }
        public double LongTermExceedanceProbability(int thresholdID, int years)
        {
            return PerformanceByThresholds.GetThreshold(thresholdID).SystemPerformanceResults.LongTermExceedanceProbability(years);
        }
        public double AssuranceOfEvent(int thresholdID, double standardNonExceedanceProbability)
        {
            Threshold thresh = PerformanceByThresholds.GetThreshold(thresholdID);
            return thresh.SystemPerformanceResults.AssuranceOfEvent(standardNonExceedanceProbability, thresh.ThresholdValue);
        }
        public double MeanExpectedAnnualConsequences(int impactAreaID = utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE, string damageCategory = null, string assetCategory = null, ConsequenceType consequenceType = ConsequenceType.Damage, RiskType riskType = RiskType.Total)
        {
            return ConsequenceResults.SampleMeanDamage(damageCategory, assetCategory, impactAreaID, consequenceType, riskType);
        }

        public IHistogram GetSpecificHistogram(int impactAreaID, string damageCategory, string assetCategory)
        {
            return ConsequenceResults.GetSpecificHistogram(damageCategory, assetCategory, impactAreaID);
        }
        public bool ResultsAreConverged(double upperConfidenceLimitProb, double lowerConfidenceLimitProb, bool checkConsequenceResults)
        {
            bool consequenceConverged = true;
            if (checkConsequenceResults == true)
                consequenceConverged = ConsequenceResultsAreConverged(upperConfidenceLimitProb, lowerConfidenceLimitProb);
            bool performanceConverged = PerformanceResultsAreConverged(upperConfidenceLimitProb, lowerConfidenceLimitProb);
            return consequenceConverged && performanceConverged;
        }

        private bool PerformanceResultsAreConverged(double upperConfidenceLimitProb, double lowerConfidenceLimitProb)
        {
            foreach (var threshold in PerformanceByThresholds.ListOfThresholds)
            {
                bool thresholdAssuranceIsConverged = threshold.SystemPerformanceResults.AssuranceTestForConvergence(upperConfidenceLimitProb, lowerConfidenceLimitProb);
                if (!thresholdAssuranceIsConverged)
                {
                    return false;
                }
            }
            return true;
        }

        private bool ConsequenceResultsAreConverged(double upperConfidenceLimitProb, double lowerConfidenceLimitProb)
        {
            foreach (AggregatedConsequencesBinned consequenceDistributionResult in ConsequenceResults.ConsequenceResultList)
            {
                if (consequenceDistributionResult.ConsequenceHistogram.HistogramIsZeroValued)
                {
                    continue;
                }
                if (consequenceDistributionResult.ConsequenceHistogram.IsHistogramConverged(upperConfidenceLimitProb, lowerConfidenceLimitProb) == false)
                {
                    return false;
                }
            }
            return true;
        }

        public long RemainingIterations(double upperConfidenceLimitProb, double lowerConfidenceLimitProb, bool computeWithDamage)
        {
            List<long> eadIterationsRemaining = new();
            eadIterationsRemaining.Add(0);
            if (computeWithDamage == true)
            {
                foreach (AggregatedConsequencesBinned consequenceDistributionResult in ConsequenceResults.ConsequenceResultList)
                {
                    if (consequenceDistributionResult.ConsequenceHistogram.HistogramIsZeroValued)
                    {
                        eadIterationsRemaining.Add(0);
                    }
                    else
                    {
                        long itsRemaining = consequenceDistributionResult.ConsequenceHistogram.EstimateIterationsRemaining(upperConfidenceLimitProb, lowerConfidenceLimitProb);
                        eadIterationsRemaining.Add(itsRemaining);
                    }
                }
            }
            else
            {
                eadIterationsRemaining.Add(0);
            }

            List<long> performanceIterationsRemaining = new();
            foreach (var threshold in PerformanceByThresholds.ListOfThresholds)
            {
                long itsRemaining = threshold.SystemPerformanceResults.AssuranceRemainingIterations(upperConfidenceLimitProb, lowerConfidenceLimitProb);
                performanceIterationsRemaining.Add(itsRemaining);
            }
            return Math.Max(eadIterationsRemaining.Max(), performanceIterationsRemaining.Max());
        }
        public void ParallelResultsAreConverged(double upperConfidenceLimitProb, double lowerConfidenceLimitProb)
        {
            foreach (var threshold in PerformanceByThresholds.ListOfThresholds)
            {
                threshold.SystemPerformanceResults.ParallelResultsAreConverged(upperConfidenceLimitProb, lowerConfidenceLimitProb);
            }
        }
        public bool Equals(ImpactAreaScenarioResults incomingIContainResults)
        {
            bool performanceMatches = PerformanceByThresholds.Equals(incomingIContainResults.PerformanceByThresholds);
            bool damageResultsMatch = ConsequenceResults.Equals(incomingIContainResults.ConsequenceResults);
            if (!performanceMatches || !damageResultsMatch)
            {
                return false;
            }
            return true;
        }

        private readonly object _uncertainCurveLock = new();

        public CategoriedUncertainPairedData GetOrCreateUncertainConsequenceFrequencyCurve(
            double[] xvals,
            string damageCategory,
            string assetCategory,
            ConsequenceType consequenceType,
            RiskType riskType,
            ConvergenceCriteria convergenceCriteria)
        {
            lock (_uncertainCurveLock)
            {
                CategoriedUncertainPairedData existingCurve = UncertainConsequenceFrequencyCurves
                    .FirstOrDefault(c =>
                        c.DamageCategory == damageCategory &&
                        c.AssetCategory == assetCategory &&
                        c.ConsequenceType == consequenceType &&
                        c.RiskType == riskType);

                if (existingCurve != null)
                {
                    return existingCurve;
                }

                var newCurve = new CategoriedUncertainPairedData(
                    xvals,
                    damageCategory,
                    assetCategory,
                    consequenceType,
                    riskType,
                    convergenceCriteria);

                UncertainConsequenceFrequencyCurves.Add(newCurve);
                return newCurve;
            }
        }

        public void PutUncertainFrequencyCurvesIntoHistograms()
        {
            foreach (var curve in UncertainConsequenceFrequencyCurves)
            {
                curve.PutDataIntoHistograms();
            }
        }
        #endregion
    }
}
