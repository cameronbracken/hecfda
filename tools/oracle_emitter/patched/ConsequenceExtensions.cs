// PATCHED LOCAL COPY of HEC.FDA.Model/metrics/Extensions/ConsequenceExtensions.cs
// @ f63682a86a30dc306a105689714a92bfd95956c5
// Phase 4 Task 4 kept the AggregatedConsequencesBinned FilterByCategories overload VERBATIM and
// dropped the sibling ByQuantile overload as a genuine compile blocker (AggregatedConsequences
// ByQuantile wasn't compiled into this project yet). Phase 6 Task 2 compiles
// AggregatedConsequencesByQuantile.cs in (see oracle_emitter.csproj), removing that blocker, so
// this patched copy now keeps BOTH overloads VERBATIM -- this file is identical to the real
// upstream source; it remains a "patched" copy only for historical/no-op reasons (nothing left to
// drop). Kept as a local copy rather than switching to a direct Compile Include of the upstream
// file so this header note travels with the file.
using System.Collections.Generic;
using System.Linq;

namespace HEC.FDA.Model.metrics.Extensions;

public static class ConsequenceExtensions
{
    public static IEnumerable<AggregatedConsequencesByQuantile> FilterByCategories(
        this IEnumerable<AggregatedConsequencesByQuantile> consequences,
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
