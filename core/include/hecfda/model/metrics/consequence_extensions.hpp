// ported from: HEC.FDA.Model/metrics/Extensions/ConsequenceExtensions.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_CONSEQUENCE_EXTENSIONS_HPP
#define HECFDA_MODEL_METRICS_CONSEQUENCE_EXTENSIONS_HPP
#include <optional>
#include <string>
#include <vector>
#include "hecfda/model/metrics/aggregated_consequences_binned.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
namespace hecfda {
namespace model {
namespace metrics {
namespace consequence_extensions {

// ported from: ConsequenceExtensions.cs `public static IEnumerable<AggregatedConsequencesBinned>
// FilterByCategories(this IEnumerable<AggregatedConsequencesBinned> consequences, ...)` -- the
// Binned overload ONLY. The sibling `FilterByCategories(this
// IEnumerable<AggregatedConsequencesByQuantile>, ...)` overload is NOT ported: this port has no
// AggregatedConsequencesByQuantile/StudyAreaConsequencesByQuantile/Empirical quantile-result
// closure (see study_area_consequences_binned.hpp's SEVERANCES for the aggregation-region methods
// that would have been its only caller).
//
// C# extension-method idiom (`this IEnumerable<T>`) has no direct C++ analog; ported as a plain
// free function taking the collection by mutable reference and returning raw pointers into it
// (matching the C# behavior of yielding references to the SAME list elements, not copies --
// AggregatedConsequencesBinned holds unique_ptr histogram members and is not copyable, so a
// pointer/reference-returning filter is the only option here; a copying filter would not compile).
//
// Nullable string wildcards (C# `string damageCategory = null` / `string assetCategory = null`):
// expressed as std::optional<std::string>, defaulted to std::nullopt (== "no filter on this
// field", matching the C# null-means-wildcard semantics). `impactAreaID`'s wildcard is instead a
// literal sentinel (-999, `utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE`) in the C#
// source itself (an int can't be null without `int?`), so it is transcribed here as a plain `int`
// default rather than a second optional -- matching the C# source's own choice.
inline std::vector<AggregatedConsequencesBinned*> filter_by_categories(
    std::vector<AggregatedConsequencesBinned>& consequences,
    const std::optional<std::string>& damage_category = std::nullopt,
    const std::optional<std::string>& asset_category = std::nullopt, int impact_area_id = -999,
    ConsequenceType type = ConsequenceType::Damage, RiskType risk_type = RiskType::Total) {
    std::vector<AggregatedConsequencesBinned*> result;
    for (AggregatedConsequencesBinned& candidate : consequences) {
        bool damage_matches = !damage_category.has_value() || *damage_category == candidate.damage_category();
        bool asset_matches = !asset_category.has_value() || *asset_category == candidate.asset_category();
        bool area_matches = impact_area_id == -999 || impact_area_id == candidate.region_id();
        bool type_matches = type == candidate.consequence_type();
        bool risk_matches = risk_type == RiskType::Total || risk_type == candidate.risk_type();
        if (damage_matches && asset_matches && area_matches && type_matches && risk_matches) {
            result.push_back(&candidate);
        }
    }
    return result;
}

// const overload, added Phase 5 Task 6 for StudyAreaConsequencesBinned::equals (needs a read-only
// lookup so it can stay a const method, matching Threshold::equals/PerformanceByThresholds::
// equals/AggregatedConsequencesBinned::equals's own const convention). Identical filtering logic
// to the non-const overload above, over a const collection, returning const pointers.
inline std::vector<const AggregatedConsequencesBinned*> filter_by_categories(
    const std::vector<AggregatedConsequencesBinned>& consequences,
    const std::optional<std::string>& damage_category = std::nullopt,
    const std::optional<std::string>& asset_category = std::nullopt, int impact_area_id = -999,
    ConsequenceType type = ConsequenceType::Damage, RiskType risk_type = RiskType::Total) {
    std::vector<const AggregatedConsequencesBinned*> result;
    for (const AggregatedConsequencesBinned& candidate : consequences) {
        bool damage_matches = !damage_category.has_value() || *damage_category == candidate.damage_category();
        bool asset_matches = !asset_category.has_value() || *asset_category == candidate.asset_category();
        bool area_matches = impact_area_id == -999 || impact_area_id == candidate.region_id();
        bool type_matches = type == candidate.consequence_type();
        bool risk_matches = risk_type == RiskType::Total || risk_type == candidate.risk_type();
        if (damage_matches && asset_matches && area_matches && type_matches && risk_matches) {
            result.push_back(&candidate);
        }
    }
    return result;
}

}  // namespace consequence_extensions
}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_CONSEQUENCE_EXTENSIONS_HPP
