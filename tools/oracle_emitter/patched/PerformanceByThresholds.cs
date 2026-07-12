// PATCHED LOCAL COPY of HEC.FDA.Model/metrics/PerformanceByThresholds.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 5 Task 3. Kept VERBATIM: ListOfThresholds, the parameterless ctor, the (bool isNull) dummy
// ctor, AddThreshold, GetThreshold (minus the MVVM ReportMessage call -- see below), and Equals.
// Dropped (mirroring the C++ port's performance_by_thresholds.hpp SEVERANCES list):
//  - `: ValidationErrorLogger` base class: needs HEC.MVVMFramework.Base.Implementations, no MVVM
//    base in this subset-compiled project.
//  - The private (List<Threshold>) ctor: exists solely to feed ReadFromXML's reconstruction,
//    dropped alongside it.
//  - WriteToXML()/ReadFromXML(XElement): needs System.Xml.Linq, no equivalent surface ported.
//  - GetThreshold's `ErrorMessage`/`ReportMessage(this, new MessageEventArgs(errorMessage))` call
//    (needs HEC.MVVMFramework.Model.Messaging/Base.Events, unreachable MVVM messaging closure):
//    the miss path still constructs and returns the same dummy Threshold() fallback, just without
//    the log side effect -- none of this task's fixture cases trigger a miss, so the dropped log
//    line has zero effect on any pinned value.
// Dropping these also drops the `using System.Xml.Linq;`/`using HEC.MVVMFramework.*;` that only
// they needed.
using System.Collections.Generic;

namespace HEC.FDA.Model.metrics;

public class PerformanceByThresholds
{
    #region Properties
    internal bool IsNull { get; }
    public List<Threshold> ListOfThresholds { get; set; }

    #endregion

    #region Constructors

    public PerformanceByThresholds()
    {
        ListOfThresholds = new List<Threshold>();
    }
    public PerformanceByThresholds(bool isNull)
    {
        ListOfThresholds = new List<Threshold>();
        Threshold dummyThreshold = new();
        ListOfThresholds.Add(dummyThreshold);
        IsNull = isNull;
    }
    #endregion
    #region Methods
    public void AddThreshold(Threshold threshold)
    {
        ListOfThresholds.Add(threshold);
    }
    public bool Equals(PerformanceByThresholds incomingPerformanceByThresholds)
    {
        bool success = true;
        foreach (Threshold threshold in ListOfThresholds)
        {
            foreach (Threshold inputThreshold in incomingPerformanceByThresholds.ListOfThresholds)
            {
                if (threshold.ThresholdID == inputThreshold.ThresholdID)
                {
                    success = threshold.Equals(inputThreshold);
                    if (!success)
                    {
                        break;
                    }
                }
            }
        }
        return success;
    }
    public Threshold GetThreshold(int thresholdID)
    {
        foreach (Threshold threshold in ListOfThresholds)
        {
            if (threshold.ThresholdID.Equals(thresholdID))
            {
                return threshold;
            }
        }
        Threshold dummyThreshold = new();
        return dummyThreshold;
    }
    #endregion
}
