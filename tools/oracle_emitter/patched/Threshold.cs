// PATCHED LOCAL COPY of HEC.FDA.Model/metrics/Threshold.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 5 Task 3. Kept VERBATIM: all three public properties (ThresholdType, ThresholdValue,
// SystemPerformanceResults) plus ThresholdID/IsNull, the parameterless dummy ctor, both public
// compute ctors, and Equals. Dropped (mirroring the C++ port's threshold.hpp SEVERANCES list):
//  - The private (int, ThresholdEnum, double, SystemPerformanceResults) ctor: exists solely to
//    feed ReadFromXML's reconstruction, dropped alongside it.
//  - WriteToXML()/ReadFromXML(XElement) and their GetXMLTagFromProperty/ThresholdEnumFromString
//    reflection-over-[StoredProperty] helpers: needs System.Xml.Linq, System.Reflection's
//    attribute-lookup surface, and Utility.Extensions.Attributes (not reachable from this
//    subset-compiled project), no equivalent surface ported.
//  - The [StoredProperty(...)] attributes on the class/properties: pure serialization metadata,
//    only read by the dropped ReadFromXML/WriteToXML/GetXMLTagFromProperty methods.
// Dropping these also drops the `using System.Reflection;`, `using System.Runtime.Serialization;`,
// `using System.Xml.Linq;`, and `using Utility.Extensions.Attributes;` that only they needed.
using HEC.FDA.Model.paireddata;
using Statistics;

namespace HEC.FDA.Model.metrics;

public class Threshold
{
    #region Properties
    public ThresholdEnum ThresholdType { get; set; }
    public double ThresholdValue { get; set; }
    public SystemPerformanceResults SystemPerformanceResults { get; set; }
    /// <summary>
    /// Threshold ID should be an integer greater than or equal to 1.
    /// The threshold ID = 0 is reserved for the default threshold.
    /// </summary>
    public int ThresholdID { get; }
    public bool IsNull { get; }
    #endregion

    #region Constructors
    public Threshold()
    {
        ThresholdType = ThresholdEnum.DefaultExteriorStage;
        ThresholdID = 9999;
        SystemPerformanceResults = new SystemPerformanceResults();
        IsNull = true;
    }
    public Threshold(int thresholdID, ConvergenceCriteria c, ThresholdEnum thresholdType = 0, double thresholdValue = 0)
    {
        ThresholdType = thresholdType;
        ThresholdValue = thresholdValue;
        SystemPerformanceResults = new SystemPerformanceResults(c);
        ThresholdID = thresholdID;
        IsNull = false;
    }

    public Threshold(int thresholdID, UncertainPairedData systemResponseCurve, ConvergenceCriteria c, ThresholdEnum thresholdType = 0, double thresholdValue = 0)
    {
        ThresholdType = thresholdType;
        ThresholdValue = thresholdValue;
        SystemPerformanceResults = new SystemPerformanceResults(systemResponseCurve, c);
        ThresholdID = thresholdID;
        IsNull = false;

    }
    #endregion
    #region Methods
    public bool Equals(Threshold incomingThreshold)
    {
        bool thresholdTypeIsTheSame = ThresholdType.Equals(incomingThreshold.ThresholdType);
        bool thresholdValueIsTheSame = ThresholdValue.Equals(incomingThreshold.ThresholdValue);
        bool thresholdIDIsTheSame = ThresholdID.Equals(incomingThreshold.ThresholdID);
        bool projectPerformanceIsTheSame = SystemPerformanceResults.Equals(incomingThreshold.SystemPerformanceResults);
        if (!thresholdTypeIsTheSame || !thresholdIDIsTheSame || !thresholdValueIsTheSame || !thresholdIDIsTheSame || !projectPerformanceIsTheSame)
        {
            return false;
        }
        return true;
    }
    #endregion
}
