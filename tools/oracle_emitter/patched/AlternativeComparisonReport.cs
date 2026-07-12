// PATCHED LOCAL COPY of HEC.FDA.Model/alternativeComparisonReport/AlternativeComparisonReport.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 6 Task 10: the real source pulls in Utility.Progress (ProgressReporter, via
// `ProgressReporter pr = null` parameters and `pr.ReportMessage(...)`/`pr.ReportProgress(...)`
// calls) and Utility.Logging (OperationResult, via ValidateAlternativeResults' Fail()/Success()
// early-exit), neither reachable/needed by this subset-compiled project (same rationale as
// patched/Alternative.cs's own ProgressReporter strip, and same rationale as
// patched/ScenarioResults.cs's/patched/StudyAreaConsequencesBinned.cs's MVVM-ReportMessage strips
// elsewhere). This patch drops the `pr`/`ProgressReporter` parameters entirely (and every
// `pr.Report*` call site, pure logging with no computational side effect), drops
// ValidateAlternativeResults + the `OperationResult success = ...; if (!success) { ...; return
// null; }` early-exit entirely (matches the C++ port's own severance: "the port can skip
// OperationResult, do the compute if valid" -- this emitter only ever feeds it valid
// non-null AlternativeResults), and drops the now-unused `using System;`/`using Utility.Logging;`/
// `using Utility.Progress;`. Everything else -- ComputeAlternativeComparisonReport's actual
// compute calls, ComputeDistributionOfEqadReduced, IterateOnConsequenceDistributionResult,
// ComputeDistributionEADReduced (including its verified-dead first `switch (type)` local, kept
// here for fidelity even though the C++ port omits it as inert) -- is kept VERBATIM.
using HEC.FDA.Model.metrics;
using Statistics.Distributions;
using System.Collections.Generic;
using System.Linq;

namespace HEC.FDA.Model.alternativeComparisonReport;

public enum AlternativeComparisonReportType
{
    BaseYearEADReduced,
    FutureYearEADReduced
}

public static class AlternativeComparisonReport
{
    public static AlternativeComparisonReportResults ComputeAlternativeComparisonReport(AlternativeResults withoutProjectAlternativeResults, IEnumerable<AlternativeResults> withProjectAlternativesResults)
    {
        List<StudyAreaConsequencesByQuantile> _EqadResults;
        List<StudyAreaConsequencesByQuantile> _BaseYearEADResults;
        List<StudyAreaConsequencesByQuantile> _FutureYearEADResults;

        _EqadResults = ComputeDistributionOfEqadReduced(withoutProjectAlternativeResults, withProjectAlternativesResults);

        _BaseYearEADResults = ComputeDistributionEADReduced(withoutProjectAlternativeResults, withProjectAlternativesResults, AlternativeComparisonReportType.BaseYearEADReduced);

        _FutureYearEADResults = ComputeDistributionEADReduced(withoutProjectAlternativeResults, withProjectAlternativesResults, AlternativeComparisonReportType.FutureYearEADReduced);

        return new AlternativeComparisonReportResults(withProjectAlternativesResults, withoutProjectAlternativeResults, _EqadResults, _BaseYearEADResults, _FutureYearEADResults);
    }

    private static List<StudyAreaConsequencesByQuantile> ComputeDistributionOfEqadReduced(AlternativeResults withoutProjectAlternativeResults, IEnumerable<AlternativeResults> withProjectAlternativesResults)
    {
        //We calculate a list of many empirical distributions of consequences - one for each with-project alternative
        List<StudyAreaConsequencesByQuantile> damagesReducedAllAlternatives = [];

        foreach (AlternativeResults withProjectAlternativeResults in withProjectAlternativesResults)
        {

            StudyAreaConsequencesByQuantile damageReducedOneAlternative = new(withProjectAlternativeResults.AlternativeID);

            List<AggregatedConsequencesByQuantile> withoutProjectConsequenceDistList = [.. withoutProjectAlternativeResults.EqadResults.ConsequenceResultList];

            foreach (AggregatedConsequencesByQuantile withProjectDamageResult in withProjectAlternativeResults.EqadResults.ConsequenceResultList)
            {
                AggregatedConsequencesByQuantile withoutProjectDamageResult = withoutProjectAlternativeResults.EqadResults.GetConsequenceResult(withProjectDamageResult.DamageCategory, withProjectDamageResult.AssetCategory, withProjectDamageResult.RegionID, withProjectDamageResult.ConsequenceType); //GetEqadHistogram;
                withoutProjectConsequenceDistList.Remove(withoutProjectDamageResult);


                AggregatedConsequencesByQuantile damageReducedResult = IterateOnConsequenceDistributionResult(withProjectDamageResult, withoutProjectDamageResult, true);
                damageReducedOneAlternative.AddExistingConsequenceResultObject(damageReducedResult);
            }
            if (withoutProjectConsequenceDistList.Count > 0)
            {
                foreach (AggregatedConsequencesByQuantile withoutProjectDamageResult in withoutProjectConsequenceDistList)
                {
                    AggregatedConsequencesByQuantile withProjectDamageResult = withProjectAlternativeResults.EqadResults.GetConsequenceResult(withoutProjectDamageResult.DamageCategory, withoutProjectDamageResult.AssetCategory, withoutProjectDamageResult.RegionID, withoutProjectDamageResult.ConsequenceType);
                    AggregatedConsequencesByQuantile damageReducedResult = IterateOnConsequenceDistributionResult(withProjectDamageResult, withoutProjectDamageResult, false);
                    damageReducedOneAlternative.AddExistingConsequenceResultObject(damageReducedResult);
                }
            }
            damagesReducedAllAlternatives.Add(damageReducedOneAlternative);
        }
        return damagesReducedAllAlternatives;
    }

    private static AggregatedConsequencesByQuantile IterateOnConsequenceDistributionResult(AggregatedConsequencesByQuantile withProjectDamageResult, AggregatedConsequencesByQuantile withoutProjectDamageResult, bool iterateOnWithProject = true)
    {
        List<Empirical> empiricalList =
        [
            withoutProjectDamageResult.ConsequenceDistribution,
            withProjectDamageResult.ConsequenceDistribution
        ];
        Empirical empirical = Empirical.StackEmpiricalDistributions(empiricalList, Empirical.Subtract);
        AggregatedConsequencesByQuantile singleEmpiricalDistributionOfConsequences = new();


        if (iterateOnWithProject)
        {
            singleEmpiricalDistributionOfConsequences = new AggregatedConsequencesByQuantile(withProjectDamageResult.DamageCategory, withProjectDamageResult.AssetCategory, empirical, withProjectDamageResult.RegionID, withProjectDamageResult.ConsequenceType, withoutProjectDamageResult.RiskType);

        }
        else
        {
            singleEmpiricalDistributionOfConsequences = new AggregatedConsequencesByQuantile(withoutProjectDamageResult.DamageCategory, withoutProjectDamageResult.AssetCategory, empirical, withoutProjectDamageResult.RegionID, withoutProjectDamageResult.ConsequenceType, withoutProjectDamageResult.RiskType);

        }
        return singleEmpiricalDistributionOfConsequences;
    }

    private static List<StudyAreaConsequencesByQuantile> ComputeDistributionEADReduced(
        AlternativeResults withoutProjectAlternativeResults, IEnumerable<AlternativeResults> withProjectAlternativesResults,
        AlternativeComparisonReportType type)
    {
        ScenarioResults withoutProjectScenarioResults;
        IEnumerable<ScenarioResults> withProjectScenarioResultsList;

        switch (type)
        {
            case AlternativeComparisonReportType.BaseYearEADReduced:
                List<ScenarioResults> withProj = withProjectAlternativesResults.Select(x => x.BaseYearScenarioResults).ToList();
                withoutProjectScenarioResults = withoutProjectAlternativeResults.BaseYearScenarioResults;
                withProjectScenarioResultsList = withProj;
                break;
            case AlternativeComparisonReportType.FutureYearEADReduced:
                List<ScenarioResults> withProjFut = withProjectAlternativesResults.Select(x => x.FutureYearScenarioResults).ToList();
                withoutProjectScenarioResults = withoutProjectAlternativeResults.FutureYearScenarioResults;
                withProjectScenarioResultsList = withProjFut;
                break;
            default:
                throw new System.ArgumentOutOfRangeException(nameof(type), type, null);
        }

        // List to hold the EAD reduced results for all alternatives
        List<StudyAreaConsequencesByQuantile> damageReducedAlternatives = [];

        // Loop through each with-project scenario result
        foreach (AlternativeResults withProjResults in withProjectAlternativesResults)
        {
            int alternativeID = withProjResults.AlternativeID;
            ScenarioResults withProjResultsList = type switch
            {
                AlternativeComparisonReportType.BaseYearEADReduced => withProjResults.BaseYearScenarioResults,
                AlternativeComparisonReportType.FutureYearEADReduced => withProjResults.FutureYearScenarioResults,
                _ => throw new System.ArgumentOutOfRangeException(nameof(type), type, null),
            };

            // Create a new StudyAreaConsequencesByQuantile for the current alternative
            StudyAreaConsequencesByQuantile damageReducedAlternative = new(alternativeID);

            // Loop through each impact area scenario result in the with-project scenario
            foreach (ImpactAreaScenarioResults withProjectResults in withProjResultsList.ResultsList)
            {
                // Get the corresponding without-project results for the same impact area
                ImpactAreaScenarioResults withoutProjectResults = withoutProjectScenarioResults.GetResults(withProjectResults.ImpactAreaID);
                StudyAreaConsequencesBinned withprojectDamageResults = withProjectResults.ConsequenceResults;
                StudyAreaConsequencesBinned withoutProjectDamageResults = withoutProjectResults.ConsequenceResults;

                // Create a list of all consequence results for the without-project scenario
                List<AggregatedConsequencesBinned> withoutProjectDamageResultsList = [.. withoutProjectDamageResults.ConsequenceResultList];

                // Loop through each consequence result in the with-project scenario
                foreach (AggregatedConsequencesBinned withProjectDamageResult in withprojectDamageResults.ConsequenceResultList)
                {
                    // Find the matching consequence result in the without-project scenario
                    AggregatedConsequencesBinned withoutProjectDamageResult = withoutProjectDamageResults.GetConsequenceResult(withProjectDamageResult.DamageCategory, withProjectDamageResult.AssetCategory, withProjectDamageResult.RegionID, withProjectDamageResult.ConsequenceType);
                    // Remove the matched result from the list to track unmatched results
                    withoutProjectDamageResultsList.Remove(withoutProjectDamageResult);

                    if (withoutProjectDamageResult == null)
                    {
                        withoutProjectDamageResult = new(
                            withProjectDamageResult.DamageCategory,
                            withProjectDamageResult.AssetCategory,
                            withProjectDamageResult.RegionID,
                            withProjectDamageResult.ConsequenceType);
                    }

                    // Compute the reduced damage result by subtracting with- and without-project distributions
                    AggregatedConsequencesByQuantile damageReducedResult = IterateOnConsequenceDistributionResult(
                        AggregatedConsequencesBinned.ConvertToSingleEmpiricalDistributionOfConsequences(withProjectDamageResult),
                        AggregatedConsequencesBinned.ConvertToSingleEmpiricalDistributionOfConsequences(withoutProjectDamageResult),
                        true);
                    // Add the result to the current alternative's results
                    damageReducedAlternative.AddExistingConsequenceResultObject(damageReducedResult);
                }

                // Handle any remaining consequence results that exist only in the without-project scenario
                if (withoutProjectDamageResultsList.Count > 0)
                {
                    foreach (AggregatedConsequencesBinned withoutProjectDamageResult in withoutProjectDamageResultsList)
                    {
                        // Try to find a matching with-project result (may be null)
                        AggregatedConsequencesBinned withProjectDamageResult = withprojectDamageResults.GetConsequenceResult(withoutProjectDamageResult.DamageCategory, withoutProjectDamageResult.AssetCategory, withoutProjectDamageResult.RegionID, withoutProjectDamageResult.ConsequenceType);
                        // Compute the reduced damage result (with-project may be null)

                        if (withProjectDamageResult == null)
                        {
                            withProjectDamageResult = new(
                                withoutProjectDamageResult.DamageCategory,
                                withoutProjectDamageResult.AssetCategory,
                                withoutProjectDamageResult.RegionID,
                                withoutProjectDamageResult.ConsequenceType);
                        }

                        AggregatedConsequencesByQuantile damageReducedResult = IterateOnConsequenceDistributionResult(
                            AggregatedConsequencesBinned.ConvertToSingleEmpiricalDistributionOfConsequences(withProjectDamageResult),
                            AggregatedConsequencesBinned.ConvertToSingleEmpiricalDistributionOfConsequences(withoutProjectDamageResult),
                            false);
                        // Add the result to the current alternative's results
                        damageReducedAlternative.AddExistingConsequenceResultObject(damageReducedResult);
                    }
                }
            }
            // Add the results for this alternative to the overall list
            damageReducedAlternatives.Add(damageReducedAlternative);
        }
        // Return the list of EAD reduced results for all alternatives
        return damageReducedAlternatives;
    }


}
