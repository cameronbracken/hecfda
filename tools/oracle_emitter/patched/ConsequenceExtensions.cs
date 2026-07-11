// PATCHED LOCAL COPY of HEC.FDA.Model/metrics/Extensions/ConsequenceExtensions.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 4 Task 4. Kept VERBATIM: the AggregatedConsequencesBinned FilterByCategories overload --
// the whole surface this task ports to C++ (consequence_extensions.hpp).
//
// Dropped: the sibling FilterByCategories(this IEnumerable<AggregatedConsequencesByQuantile>, ...)
// overload -- GENUINE COMPILE BLOCKER, references AggregatedConsequencesByQuantile, which is not
// compiled into this subset project (the quantile-result type family is out of scope for this
// port; see study_area_consequences_binned.hpp's SEVERANCES). This mirrors the C++ port, which
// likewise only ports the Binned overload (see consequence_extensions.hpp's class comment).
using System.Collections.Generic;
using System.Linq;

namespace HEC.FDA.Model.metrics.Extensions;

public static class ConsequenceExtensions
{
    public static IEnumerable<AggregatedConsequencesBinned> FilterByCategories(
        this IEnumerable<AggregatedConsequencesBinned> consequences,
        string damageCategory = null,
        string assetCategory = null,
        int impactAreaID = -999,
        ConsequenceType type = ConsequenceType.Damage,
        RiskType riskType = RiskType.Total)
    {
        return consequences.Where(result =>
            (damageCategory == null || damageCategory.Equals(result.DamageCategory)) &&
            (assetCategory == null || assetCategory.Equals(result.AssetCategory)) &&
            (impactAreaID == -999 || impactAreaID == result.RegionID) &&
            (type == result.ConsequenceType) &&
            (riskType == RiskType.Total || riskType == result.RiskType));
    }
}
