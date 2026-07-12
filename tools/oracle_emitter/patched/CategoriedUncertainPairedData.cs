// PATCHED LOCAL COPY of HEC.FDA.Model/metrics/CategoriedUncertainPairedData.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 5 Task 4. Kept VERBATIM: both fields, both ctors, AddCurveRealization,
// PutDataIntoHistograms, GetUncertainPairedData, and InitializeHistograms -- the whole
// compute/batching path this task ports to C++. Dropped:
//  - WriteToXML()/ReadFromXML(XElement): needs System.Xml.Linq and DynamicHistogram.ToXML/
//    ReadFromXML, no equivalent surface ported (mirrors patched/AggregatedConsequencesBinned.cs
//    and patched/AssuranceResultStorage.cs's identical XML drop).
// Dropping this also drops the `using System.Xml.Linq;` that only it needed.
using HEC.FDA.Model.paireddata;
using Statistics;
using Statistics.Histograms;
using System;
using System.Collections.Generic;
using System.Linq;

namespace HEC.FDA.Model.metrics;

/// <summary>
/// Represents an uncertain consequence-frequency curve that aggregates multiple
/// ConsequenceFrequencyCurve samples into histograms for each ordinate position.
/// Curves must be added in batches to ensure deterministic results when using parallel computation.
/// </summary>
public class CategoriedUncertainPairedData
{
    #region Fields
    private const int INITIAL_BIN_QUANTITY = 500;
    private readonly double[][] _TempYValues;
    private readonly int _BatchSize;
    private bool _HistogramsNotConstructed = true;
    private double[] _Xvals;
    #endregion

    #region Properties
    /// <summary>
    /// The x-values (typically exceedance probabilities) shared by all curves.
    /// </summary>
    public IReadOnlyList<double> Xvals => _Xvals;

    /// <summary>
    /// Histograms for each y-value position in the curves.
    /// </summary>
    public List<DynamicHistogram> YHistograms { get; private set; } = [];

    /// <summary>
    /// The type of consequence (Damage or LifeLoss).
    /// </summary>
    public ConsequenceType ConsequenceType { get; }

    /// <summary>
    /// The type of risk (Fail, Non_Fail, or Total).
    /// </summary>
    public RiskType RiskType { get; }

    /// <summary>
    /// The damage category (e.g., "Residential", "Commercial").
    /// </summary>
    public string DamageCategory { get; }

    /// <summary>
    /// The asset category (e.g., "Structure", "Content").
    /// </summary>
    public string AssetCategory { get; }

    /// <summary>
    /// The convergence criteria used for the histograms.
    /// </summary>
    public ConvergenceCriteria ConvergenceCriteria { get; }
    #endregion

    #region Constructors
    /// <summary>
    /// Creates an UncertainConsequenceFrequencyCurve with the specified metadata and convergence criteria.
    /// </summary>
    /// <param name="xvals">The x-values (exceedance probabilities) for all curves.</param>
    /// <param name="damageCategory">The damage category.</param>
    /// <param name="assetCategory">The asset category.</param>
    /// <param name="consequenceType">The type of consequence.</param>
    /// <param name="riskType">The type of risk.</param>
    /// <param name="convergenceCriteria">The convergence criteria for the histograms.</param>
    public CategoriedUncertainPairedData(
        double[] xvals,
        string damageCategory,
        string assetCategory,
        ConsequenceType consequenceType,
        RiskType riskType,
        ConvergenceCriteria convergenceCriteria)
    {
        _Xvals = xvals;
        DamageCategory = damageCategory;
        AssetCategory = assetCategory;
        ConsequenceType = consequenceType;
        RiskType = riskType;
        ConvergenceCriteria = convergenceCriteria;
        _BatchSize = convergenceCriteria.IterationCount;

        // Initialize temp arrays for each y-value position
        _TempYValues = new double[xvals.Length][];
        for (int i = 0; i < xvals.Length; i++)
        {
            _TempYValues[i] = new double[_BatchSize];
        }
    }

    /// <summary>
    /// Creates an UncertainConsequenceFrequencyCurve from an initial ConsequenceFrequencyCurve.
    /// The x-values are extracted from the curve.
    /// </summary>
    /// <param name="initialCurve">The first curve to use for initialization.</param>
    /// <param name="convergenceCriteria">The convergence criteria for the histograms.</param>
    public CategoriedUncertainPairedData(
        CategoriedPairedData initialCurve,
        ConvergenceCriteria convergenceCriteria)
        : this(
            initialCurve.FrequencyCurve.Xvals.ToArray(),
            initialCurve.DamageCategory,
            initialCurve.AssetCategory,
            initialCurve.ConsequenceType,
            initialCurve.RiskType,
            convergenceCriteria)
    {
    }
    #endregion

    #region Methods

    /// <summary>
    /// Adds a PairedData curve to the batch at the specified iteration index.
    /// </summary>
    /// <param name="frequencyCurve">The paired data curve to add.</param>
    /// <param name="iterationIndex">The index within the current batch.</param>
    public void AddCurveRealization(PairedData frequencyCurve, long iterationIndex)
    {
        if (frequencyCurve.Xvals.Count != Xvals.Count)
        {
            throw new ArgumentException("frequency curves need to have the same x ordinates to be added.");
        }
        IReadOnlyList<double> yvals = frequencyCurve.Yvals;
        for (int i = 0; i < yvals.Count; i++)
        {
            _TempYValues[i][iterationIndex] = yvals[i];
        }
    }

    /// <summary>
    /// Flushes the batch of curves from temp arrays into the histograms.
    /// Should be called after each batch of iterations completes.
    /// </summary>
    public void PutDataIntoHistograms()
    {
        if (_HistogramsNotConstructed)
        {
            InitializeHistograms();
            _HistogramsNotConstructed = false;
        }

        for (int i = 0; i < _TempYValues.Length; i++)
        {
            YHistograms[i].AddObservationsToHistogram(_TempYValues[i]);
            Array.Clear(_TempYValues[i]);
        }
    }

    public UncertainPairedData GetUncertainPairedData()
    {
        return new(_Xvals, YHistograms.ToArray(), new());
    }


    private void InitializeHistograms()
    {
        YHistograms = new List<DynamicHistogram>(_TempYValues.Length);
        for (int i = 0; i < _TempYValues.Length; i++)
        {
            double max = _TempYValues[i].Max();
            double min = _TempYValues[i].Min();
            double range = max - min;
            double binWidth;
            if (range < INITIAL_BIN_QUANTITY)
            {
                binWidth = 0.001;
            }
            else
            {
                binWidth = range / INITIAL_BIN_QUANTITY;
            }
            YHistograms.Add(new DynamicHistogram(binWidth, ConvergenceCriteria));
        }
    }
    #endregion
}
