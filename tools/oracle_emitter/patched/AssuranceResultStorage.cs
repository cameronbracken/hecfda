// PATCHED LOCAL COPY of HEC.FDA.Model/metrics/AssuranceResultStorage.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 5 Task 1. Kept VERBATIM: the field, all three properties, the public compute ctor,
// Equals, AddObservation, and PutDataIntoHistogram -- the whole compute/convergence path this
// task ports to C++. Dropped (mirroring the C++ port's assurance_result_storage.hpp
// DONE_WITH_CONCERNS list):
//  - WriteToXML()/ReadFromXML(XElement): XML (de)serialization, needs System.Xml.Linq and
//    DynamicHistogram.ToXML/ReadFromXML, no equivalent surface ported.
//  - The private (string, double, DynamicHistogram) ctor: exists solely to feed ReadFromXML's
//    reconstruction, dropped alongside it.
// Dropping these also drops the `using System.Xml.Linq;` that only they needed. The internal
// "dummy" (string, double) ctor is kept verbatim -- it does not touch XML, only the
// parameterless `new DynamicHistogram()` "ARBITRARY histogram" ctor (itself compiled in via
// HEC.FDA.Statistics), and isn't a compile blocker.
using System;
using Statistics.Histograms;
using Statistics;

namespace HEC.FDA.Model.metrics;

public class AssuranceResultStorage
{
    #region Fields
    private readonly double[] _TempResults;
    #endregion

    #region Properties
    public string AssuranceType { get; }
    public DynamicHistogram AssuranceHistogram { get; }
    public double StandardNonExceedanceProbability { get; }
    #endregion

    #region Constructors
    internal AssuranceResultStorage(string dummyAsuranceType, double standardNonExceedanceProbability)
    {
        _TempResults = Array.Empty<double>();
        AssuranceHistogram = new DynamicHistogram();
        AssuranceType = dummyAsuranceType;
        StandardNonExceedanceProbability = standardNonExceedanceProbability;
    }
    public AssuranceResultStorage(string assuranceType, double binWidth, ConvergenceCriteria convergenceCriteria, double standardNonExceedanceProbabilityForAssuranceOfTargetOrLevee = 0)
    {
        StandardNonExceedanceProbability = standardNonExceedanceProbabilityForAssuranceOfTargetOrLevee;
        _TempResults = new double[convergenceCriteria.IterationCount];
        AssuranceHistogram = new DynamicHistogram(binWidth, convergenceCriteria);
        AssuranceType = assuranceType;
    }
    #endregion

    #region Methods

    public bool Equals(AssuranceResultStorage incomingAssuranceResultStorage)
    {
        if (AssuranceType == incomingAssuranceResultStorage.AssuranceType)
        {
            if (StandardNonExceedanceProbability == incomingAssuranceResultStorage.StandardNonExceedanceProbability)
            {
                if (AssuranceHistogram.Equals(incomingAssuranceResultStorage.AssuranceHistogram))
                {
                    return true;
                }
            }
        }
        return false;
    }

    public void AddObservation(double result, int iteration)
    {
        _TempResults[iteration] = (result);
    }

    public void PutDataIntoHistogram()
    {
        AssuranceHistogram.AddObservationsToHistogram(_TempResults);
        Array.Clear(_TempResults);
    }

    #endregion
}
