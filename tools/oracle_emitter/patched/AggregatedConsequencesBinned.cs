// PATCHED LOCAL COPY of HEC.FDA.Model/metrics/AggregatedConsequencesBinned.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 4 Task 3. Kept VERBATIM: both fields, all four ctors, PutDataIntoHistogram,
// AddConsequenceRealization, SampleMeanExpectedAnnualConsequences,
// ConsequenceExceededWithProbabilityQ, QuantityExceededWithProbabilityQ, and Equals -- the whole
// compute/convergence path this task ports to C++. Dropped (mirroring the C++ port's
// aggregated_consequences_binned.hpp DONE_WITH_CONCERNS list):
//  - WriteToXML()/ReadFromXML(XElement): XML (de)serialization, needs System.Xml.Linq and
//    DynamicHistogram.ToXML/ReadFromXML, no equivalent surface ported.
// Dropping this also drops the `using System.Xml.Linq;` it only needed.
//
// Phase 6 Task 4 RESTORED: ConvertToSingleEmpiricalDistributionOfConsequences(...) -- was dropped
// by the original Phase 4 Task 3 patch (AggregatedConsequencesByQuantile wasn't compiled into this
// project yet, and DynamicHistogram.ConvertToEmpiricalDistribution wasn't ported to C++ yet).
// Both dependencies now exist (AggregatedConsequencesByQuantile.cs is compiled in unpatched,
// oracle_emitter.csproj's Task Phase6T2 comment; DynamicHistogram.ConvertToEmpiricalDistribution
// lives in the real, unpatched HEC.FDA.Statistics.dll referenced via ProjectReference), so this
// method is restored VERBATIM below.
using Statistics;
using Statistics.Distributions;
using Statistics.Histograms;
using System;
using System.Linq;

namespace HEC.FDA.Model.metrics;

public class AggregatedConsequencesBinned
{
    #region Fields
    private const int INITIAL_BIN_QUANTITY = 500;
    private readonly double[] _TempResults;
    private readonly double[] _TempCounts;
    private bool _HistogramNotConstructed = false;
    #endregion

    #region Properties
    public IHistogram ConsequenceHistogram { get; private set; }
    public IHistogram DamagedElementQuantityHistogram { get; private set; }
    public string DamageCategory { get; }
    public string AssetCategory { get; }
    public ConsequenceType ConsequenceType { get; }
    public RiskType RiskType { get; }
    public int RegionID { get; } = utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE;
    public bool IsNull { get; }
    public ConvergenceCriteria ConvergenceCriteria { get; }
    #endregion

    #region Constructors
    /// <summary>
    /// This constructor is only used for handling errors.
    /// </summary>
    public AggregatedConsequencesBinned(int impactAreaID, ConsequenceType consequenceType, RiskType riskType)
    {
        DamageCategory = "UNASSIGNED";
        AssetCategory = "UNASSIGNED";
        ConsequenceType = consequenceType;
        RiskType = riskType;
        RegionID = impactAreaID;
        ConvergenceCriteria = new ConvergenceCriteria();
        ConsequenceHistogram = new DynamicHistogram();
        DamagedElementQuantityHistogram = new DynamicHistogram();
        IsNull = true;
        _TempResults = new double[ConvergenceCriteria.IterationCount];
        _TempCounts = new double[ConvergenceCriteria.IterationCount];
    }
    public AggregatedConsequencesBinned(string damageCategory, string assetCategory, ConvergenceCriteria convergenceCriteria, int impactAreaID, ConsequenceType consequenceType, RiskType riskType = RiskType.Fail)
    {
        DamageCategory = damageCategory;
        AssetCategory = assetCategory;
        ConsequenceType = consequenceType;
        RiskType = riskType;
        ConvergenceCriteria = convergenceCriteria;
        IsNull = false;
        RegionID = impactAreaID;
        _TempResults = new double[ConvergenceCriteria.IterationCount];
        _TempCounts = new double[ConvergenceCriteria.IterationCount];
        _HistogramNotConstructed = true;

    }

    public AggregatedConsequencesBinned(string damageCategory, string assetCategory, IHistogram histogram, int impactAreaID, ConsequenceType consequenceType = ConsequenceType.Damage, RiskType riskType = RiskType.Fail)
    {
        DamageCategory = damageCategory;
        AssetCategory = assetCategory;
        ConsequenceType = consequenceType;
        RiskType = riskType;
        ConsequenceHistogram = histogram;
        ConvergenceCriteria = ConsequenceHistogram.ConvergenceCriteria;
        RegionID = impactAreaID;
        IsNull = false;
        _TempResults = new double[ConvergenceCriteria.IterationCount];
        _TempCounts = new double[ConvergenceCriteria.IterationCount];

    }

    public AggregatedConsequencesBinned(string damageCategory, string assetCategory, int impactAreaID, ConsequenceType consequenceType = ConsequenceType.Damage, RiskType riskType = RiskType.Fail)
    {
        DamageCategory = damageCategory;
        AssetCategory = assetCategory;
        ConsequenceType = consequenceType;
        RiskType = riskType;
        RegionID = impactAreaID;
        ConvergenceCriteria = new ConvergenceCriteria();
        ConsequenceHistogram = new DynamicHistogram();
        DamagedElementQuantityHistogram = new DynamicHistogram();
        IsNull = true;
        _TempResults = new double[ConvergenceCriteria.IterationCount];
        _TempCounts = new double[ConvergenceCriteria.IterationCount];
    }
    #endregion

    #region Methods
    internal void PutDataIntoHistogram()
    {
        if (_HistogramNotConstructed)
        {
            double max = _TempResults.Max();
            double min = _TempResults.Min();
            double range = max - min;
            double binWidth;
            if (range < INITIAL_BIN_QUANTITY)
            {
                binWidth = DynamicHistogram.DEFAULT_BIN_WIDTH;
            }
            else
            {
                binWidth = range / INITIAL_BIN_QUANTITY;
            }
            ConsequenceHistogram = new DynamicHistogram(binWidth, ConvergenceCriteria);
            DamagedElementQuantityHistogram = new DynamicHistogram(binWidth: 1, ConvergenceCriteria);
            _HistogramNotConstructed = false;
        }
        ConsequenceHistogram.AddObservationsToHistogram(_TempResults);
        DamagedElementQuantityHistogram.AddObservationsToHistogram(_TempCounts);
        Array.Clear(_TempResults);
    }

    internal void AddConsequenceRealization(double damageRealization, long iteration = 1, int damagedElementsCount = 0)
    {
        _TempResults[iteration] = (damageRealization);
        _TempCounts[iteration] = (damagedElementsCount);

    }

    internal double SampleMeanExpectedAnnualConsequences()
    {
        return ConsequenceHistogram.SampleMean;
    }

    internal double ConsequenceExceededWithProbabilityQ(double exceedanceProbability)
    {
        double nonExceedanceProbability = 1 - exceedanceProbability;
        double quantile = ConsequenceHistogram.InverseCDF(nonExceedanceProbability);
        return quantile;
    }

    internal double QuantityExceededWithProbabilityQ(double exceedanceProbability)
    {
        double nonExceedanceProbability = 1 - exceedanceProbability;
        double quantile = DamagedElementQuantityHistogram.InverseCDF(nonExceedanceProbability);
        return quantile;
    }

    internal bool Equals(AggregatedConsequencesBinned damageResult)
    {
        bool histogramsMatch = ConsequenceHistogram.Equals(damageResult.ConsequenceHistogram);
        if (!histogramsMatch)
        {
            return false;
        }
        return true;
    }

    public static AggregatedConsequencesByQuantile ConvertToSingleEmpiricalDistributionOfConsequences(AggregatedConsequencesBinned consequenceDistributionResult)
    {
        Empirical empirical = DynamicHistogram.ConvertToEmpiricalDistribution(consequenceDistributionResult.ConsequenceHistogram);
        return new AggregatedConsequencesByQuantile(consequenceDistributionResult.DamageCategory, consequenceDistributionResult.AssetCategory, empirical, consequenceDistributionResult.RegionID, consequenceDistributionResult.ConsequenceType, consequenceDistributionResult.RiskType);
    }
    #endregion
}
