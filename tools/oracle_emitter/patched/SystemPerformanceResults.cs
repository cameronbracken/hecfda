// PATCHED LOCAL COPY of HEC.FDA.Model/metrics/SystemPerformanceResults.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 5 Task 2. Kept VERBATIM: all three public ctors, AddStageAssuranceHistogram,
// GetAssuranceOfThresholdHistogram, GetAEPHistogram, AddAEPForAssurance, AddStageForAssurance,
// MeanAEP, MedianAEP, AEPWithGivenAssurance, AssuranceOfAEP, AssuranceIsConverged,
// AssuranceTestForConvergence, AssuranceRemainingIterations, AssuranceOfEvent,
// CalculateAssuranceForLevee, LongTermExceedanceProbability, ParallelResultsAreConverged, Equals,
// GetAssurance (minus the MVVM ReportMessage call -- see below), PutDataIntoHistograms. Dropped
// (mirroring the C++ port's system_performance_results.hpp SEVERANCES list):
//  - `: ValidationErrorLogger` base class: no MVVM base in this subset-compiled project.
//  - The two private (ConvergenceCriteria, List<AssuranceResultStorage>) /
//    (UncertainPairedData, ConvergenceCriteria, List<AssuranceResultStorage>) ctors: exist solely
//    to feed ReadFromXML's reconstruction, dropped alongside it.
//  - WriteToXML()/ReadFromXML(XElement): XML (de)serialization, needs System.Xml.Linq, no
//    equivalent surface ported.
//  - GetAssurance's `ErrorMessage`/`ReportMessage(this, new MessageEventArgs(errorMessage))` call
//    (needs HEC.MVVMFramework.Model.Messaging/Base.Events, unreachable MVVM messaging closure):
//    the miss path still constructs and returns the same dummy AssuranceResultStorage fallback,
//    just without the log side effect -- none of this task's fixture cases trigger a miss, so the
//    dropped log line has zero effect on any pinned value.
// Dropping these also drops the `using System.Xml.Linq;`/`using HEC.MVVMFramework.*;` that only
// they needed; `using System.Linq;` is added for ToList()/ToArray()/ElementAt() (already reachable
// transitively but not previously `using`'d directly in this file).
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Statistics;
using Statistics.Histograms;
using HEC.FDA.Model.paireddata;

namespace HEC.FDA.Model.metrics;

public class SystemPerformanceResults
{
    #region Fields
    private const string AEP_ASSURANCE_TYPE = "AEP";
    private const string STAGE_ASSURANCE_TYPE = "STAGE";
    private const double AEP_BIN_WIDTH = 0.0002;
    private const double STAGE_BIN_WIDTH = 0.001;
    private readonly bool _CalculatePerformanceForLevee;
    private readonly UncertainPairedData _SystemResponseFunction;
    private readonly ConvergenceCriteria _ConvergenceCriteria;

    #endregion
    #region Properties
    public List<AssuranceResultStorage> Assurances { get; }
    #endregion
    #region Constructors
    public SystemPerformanceResults()
    {
        _ConvergenceCriteria = new ConvergenceCriteria();
        Assurances = new List<AssuranceResultStorage>();
        AssuranceResultStorage dummyAEP = new(AEP_ASSURANCE_TYPE, 0);
        Assurances.Add(dummyAEP);
        double[] standardNonExceedanceProbabilities = new double[] { .9, .96, .98, .99, .996, .998 };
        foreach (double probability in standardNonExceedanceProbabilities)
        {
            AssuranceResultStorage dummyAssurance = new(STAGE_ASSURANCE_TYPE, probability);
            Assurances.Add(dummyAssurance);
        }
    }
    public SystemPerformanceResults(ConvergenceCriteria convergenceCriteria)
    {
        _ConvergenceCriteria = convergenceCriteria;
        Assurances = new List<AssuranceResultStorage>();
        AssuranceResultStorage aepAssurance = new(AEP_ASSURANCE_TYPE, AEP_BIN_WIDTH, convergenceCriteria);
        Assurances.Add(aepAssurance);
    }
    public SystemPerformanceResults(UncertainPairedData systemResponseFunction, ConvergenceCriteria convergenceCriteria)
    {
        _SystemResponseFunction = systemResponseFunction;
        //If the system response function is the default function
        if (_SystemResponseFunction.Xvals.Length <= 2)
        {
            _CalculatePerformanceForLevee = false;
        } else
        {
            _CalculatePerformanceForLevee = true;
        }
        Assurances = new List<AssuranceResultStorage>();
        AssuranceResultStorage aepAssurance = new(AEP_ASSURANCE_TYPE, AEP_BIN_WIDTH, convergenceCriteria);
        Assurances.Add(aepAssurance);
        _ConvergenceCriteria = convergenceCriteria;
    }
    #endregion
    #region Methods
    /// <summary>
    /// The standard non-exceedance probabilities are one of the double[] { .9, .96, .98, .99, .996, .998 };
    /// For now, bin width for histograms of stages will be 0.001 - so 1/1000 of a foot
    /// </summary>
    /// <param name="standardNonExceedanceProbability"></param>
    public void AddStageAssuranceHistogram(double standardNonExceedanceProbability, double binWidth = STAGE_BIN_WIDTH)
    {
        AssuranceResultStorage assurance = new(STAGE_ASSURANCE_TYPE, binWidth, _ConvergenceCriteria, standardNonExceedanceProbability);
        if (!Assurances.Contains(assurance))
        {
            Assurances.Add(assurance);
        }
    }
    public DynamicHistogram GetAssuranceOfThresholdHistogram(double standardNonExceedanceProbability)
    {
        DynamicHistogram stageHistogram = GetAssurance(STAGE_ASSURANCE_TYPE, standardNonExceedanceProbability).AssuranceHistogram;
        return stageHistogram;
    }
    internal DynamicHistogram GetAEPHistogram()
    {
        DynamicHistogram aepHistogram = GetAssurance(AEP_ASSURANCE_TYPE).AssuranceHistogram;
        return aepHistogram;
    }

    public void AddAEPForAssurance(double aep, int iteration)
    {
        GetAssurance(AEP_ASSURANCE_TYPE).AddObservation(aep, iteration);
    }
    public void AddStageForAssurance(double standardNonExceedanceProbability, double stage, int iteration)
    {
        GetAssurance(STAGE_ASSURANCE_TYPE, standardNonExceedanceProbability).AddObservation(stage, iteration);
    }

    public double MeanAEP()
    {
        return GetAssurance(AEP_ASSURANCE_TYPE).AssuranceHistogram.SampleMean;
    }

    public double MedianAEP()
    {
        return GetAssurance(AEP_ASSURANCE_TYPE).AssuranceHistogram.InverseCDF(0.5);
    }

    internal double AEPWithGivenAssurance(double assurance)
    {
        double aepWithGivenAssurance = GetAssurance(AEP_ASSURANCE_TYPE).AssuranceHistogram.InverseCDF(assurance);
        if (aepWithGivenAssurance > 1)
        {
            aepWithGivenAssurance = 1;
        }
        return aepWithGivenAssurance;
    }

    internal double AssuranceOfAEP(double exceedanceProbability)
    {
        double assuranceOfAEP = GetAssurance(AEP_ASSURANCE_TYPE).AssuranceHistogram.CDF(exceedanceProbability);
        return assuranceOfAEP;
    }
    internal bool AssuranceIsConverged()
    {
        double standardNonExceedanceProbability = 0.98;
        DynamicHistogram assuranceHistogram = GetAssurance(STAGE_ASSURANCE_TYPE, standardNonExceedanceProbability).AssuranceHistogram;
        return assuranceHistogram.IsConverged;
    }
    public bool AssuranceTestForConvergence(double upperConfidenceLimitProb, double lowerConfidenceLimitProb)
    {
        double standardNonExceedanceProbability = 0.98;
        DynamicHistogram assuranceHistogram = GetAssurance(STAGE_ASSURANCE_TYPE, standardNonExceedanceProbability).AssuranceHistogram;
        bool assuranceIsConverged = assuranceHistogram.IsHistogramConverged(upperConfidenceLimitProb, lowerConfidenceLimitProb);
        return assuranceIsConverged;
    }
    public long AssuranceRemainingIterations(double upperConfidenceLimitProb, double lowerConfidenceLimitProb)
    {
        double standardNonExceedanceProbability = 0.98;
        DynamicHistogram assuranceHistogram = GetAssurance(STAGE_ASSURANCE_TYPE, standardNonExceedanceProbability).AssuranceHistogram;
        long iterationsRemaining = assuranceHistogram.EstimateIterationsRemaining(upperConfidenceLimitProb, lowerConfidenceLimitProb);
        return iterationsRemaining;
    }
    public double AssuranceOfEvent(double standardNonExceedanceProbability, double thresholdValue)
    {
        if (_CalculatePerformanceForLevee)
        {
            return CalculateAssuranceForLevee(standardNonExceedanceProbability);
        }
        else
        {
            GetAssurance(STAGE_ASSURANCE_TYPE, standardNonExceedanceProbability).AssuranceHistogram.ForceDeQueue();
            DynamicHistogram assuranceHistogram = GetAssurance(STAGE_ASSURANCE_TYPE, standardNonExceedanceProbability).AssuranceHistogram;
            double assurance = assuranceHistogram.CDF(thresholdValue);
            return assurance;
        }

    }

    private double CalculateAssuranceForLevee(double standardNonExceedanceProbability)
    {
        DynamicHistogram assuranceHistogram = GetAssurance(STAGE_ASSURANCE_TYPE, standardNonExceedanceProbability).AssuranceHistogram;
        IPairedData medianLeveeCurve = _SystemResponseFunction.SamplePairedData(0.5);

        //if the user defined sysstem reponse function does not have certain failure defined, then define it
        if (medianLeveeCurve.Yvals[^1] < 1) {
            double epsilon = 0.001;
            double largestX = medianLeveeCurve.Xvals[^1];
            List<double> xVals = medianLeveeCurve.Xvals.ToList();
            List<double> yVals = medianLeveeCurve.Yvals.ToList();
            xVals.Add(largestX + epsilon);
            yVals.Add(1);
            medianLeveeCurve = new PairedData(xVals.ToArray(), yVals.ToArray());
        }
        assuranceHistogram.ForceDeQueue();
        double stageStep = assuranceHistogram.BinWidth;
        double[] stages = _SystemResponseFunction.Xvals;
        double firstStage = stages[0];
        double currentStage;
        double nextStage;
        double currentCumulativeExceedanceProbability = 0;
        double geotechnicalFailureAtAverageStage = 0;
        double incrementalProbabilityWithFailure = 0;
        double exceedanceProbabilityWithFailure = 0;
        double conditionalNonExceedanceProbability;
        int i = 0;
        //calculate from the bottom of the fragility curve up, until certain failure
        while (geotechnicalFailureAtAverageStage < 1)
        {
            currentStage = firstStage + i * stageStep;
            nextStage = currentStage + stageStep;
            currentCumulativeExceedanceProbability = 1 - assuranceHistogram.CDF(currentStage);
            double nextCumulativeExceedanceProbability = 1 - assuranceHistogram.CDF(nextStage);
            double incrementalProbability = currentCumulativeExceedanceProbability - nextCumulativeExceedanceProbability;
            double averageStage = (currentStage + nextStage) / 2;
            geotechnicalFailureAtAverageStage = medianLeveeCurve.f(averageStage);
            incrementalProbabilityWithFailure = incrementalProbability * geotechnicalFailureAtAverageStage;
            exceedanceProbabilityWithFailure += incrementalProbabilityWithFailure;
            i++;
        }
        //correct cumulative probability with failure by removing incorrect incremental probability with failure
        exceedanceProbabilityWithFailure -= incrementalProbabilityWithFailure;
        //the incremental probability with failure for the stage at which prob(failure) = 1 is the current cumulative exceedance probability
        exceedanceProbabilityWithFailure += currentCumulativeExceedanceProbability;
        conditionalNonExceedanceProbability = 1 - exceedanceProbabilityWithFailure;
        return conditionalNonExceedanceProbability;
    }

    public double LongTermExceedanceProbability(int years)
    {
        double ltep = 1 - Math.Pow(1 - MeanAEP(), years);
        return ltep;
    }
    /// <summary>
    /// The parallel test for convergence will test for convergence in the histograms of stages and in the histogram of aeps
    /// </summary>
    /// <param name="upperQuantile"></param>
    /// <param name="lowerQuantile"></param>
    public void ParallelResultsAreConverged(double upperQuantile, double lowerQuantile)
    {
        double[] assuranceQuantity = new double[Assurances.Count];
        Parallel.For(0, assuranceQuantity.Length, i =>
        {
            Assurances.ElementAt(i).AssuranceHistogram.IsHistogramConverged(upperQuantile, lowerQuantile);
        });
    }
    public bool Equals(SystemPerformanceResults projectPerformanceResults)
    {
        foreach (AssuranceResultStorage assuranceResultStorage in Assurances)
        {
            bool areEqual = assuranceResultStorage.Equals(projectPerformanceResults.GetAssurance(assuranceResultStorage.AssuranceType, assuranceResultStorage.StandardNonExceedanceProbability));
            if (!areEqual)
            {
                return false;
            }
        }
        return true;
    }
    internal AssuranceResultStorage GetAssurance(string type, double standardNonExceedanceProbabilityForAssuranceOfTargetOrLevee = 0)
    {
        foreach (AssuranceResultStorage assurance in Assurances)
        {
            if (assurance.AssuranceType == type)
            {
                if (assurance.StandardNonExceedanceProbability == standardNonExceedanceProbabilityForAssuranceOfTargetOrLevee)
                {
                    return assurance;
                }
            }
        }
        AssuranceResultStorage dummyAssurance = new(STAGE_ASSURANCE_TYPE, .98);
        return dummyAssurance;

    }
    public void PutDataIntoHistograms()
    {
        foreach (AssuranceResultStorage assuranceResultStorage in  Assurances)
        {
            assuranceResultStorage.PutDataIntoHistogram();
        }
    }
    #endregion
}
