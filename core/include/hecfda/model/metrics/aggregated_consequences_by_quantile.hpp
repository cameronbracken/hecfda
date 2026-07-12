// ported from: HEC.FDA.Model/metrics/AggregatedConsequencesByQuantile.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_AGGREGATED_CONSEQUENCES_BY_QUANTILE_HPP
#define HECFDA_MODEL_METRICS_AGGREGATED_CONSEQUENCES_BY_QUANTILE_HPP
#include <string>
#include <utility>
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/statistics/distributions/empirical.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: AggregatedConsequencesByQuantile.cs `public class AggregatedConsequencesByQuantile`
// -- the quantile-result analog of AggregatedConsequencesBinned (aggregated_consequences_binned.hpp),
// backed by an Empirical distribution rather than a DynamicHistogram. Unlike Binned's
// unique_ptr<DynamicHistogram> members (deferred/nullable, since a histogram is built lazily),
// Empirical is held BY VALUE here -- matching the C# `Empirical ConsequenceDistribution { get; }`
// get-only property, which is always assigned (never null) in both ctors. This makes
// AggregatedConsequencesByQuantile copyable, unlike AggregatedConsequencesBinned, hence the
// ByQuantile consequence_extensions::filter_by_categories overload below returns filtered
// AggregatedConsequencesByQuantile values directly rather than pointers (see that header).
//
// Ctor field-order and defaults transcribed exactly from the C# source's two ctors:
//  - Parameterless ctor: DamageCategory/AssetCategory = "unassigned", ConsequenceType = Damage,
//    RiskType = Fail, RegionID = 0 (NOT the `= -999` field initializer -- the parameterless ctor
//    body unconditionally overwrites it to 0, same pattern as
//    AggregatedConsequencesBinned.cs's own RegionID field initializer being dead, see that
//    header's DONE_WITH_CONCERNS), ConsequenceDistribution = a fresh default-constructed
//    Empirical (see empirical.hpp's now-restored `Empirical()` ctor), IsNull = true.
//  - Compute ctor `(string, string, Empirical, int, ConsequenceType, RiskType)`: IsNull = false,
//    RegionID = the impactAreaID argument (no wildcard substitution here -- that only happens in
//    the FilterByCategories predicate, not construction).
class AggregatedConsequencesByQuantile {
   public:
    // ported from: AggregatedConsequencesByQuantile.cs `public AggregatedConsequencesByQuantile()`.
    AggregatedConsequencesByQuantile()
        : consequence_distribution_(),
          damage_category_("unassigned"),
          asset_category_("unassigned"),
          region_id_(0),
          consequence_type_(ConsequenceType::Damage),
          risk_type_(RiskType::Fail),
          is_null_(true) {}

    // ported from: AggregatedConsequencesByQuantile.cs `public AggregatedConsequencesByQuantile(
    // string damageCategory, string assetCategory, Empirical empirical, int impactAreaID,
    // ConsequenceType consequenceType, RiskType riskType)`. "This constructor can accept a
    // previously created Empirical Distribution as such can be used for both compute types"
    // (upstream doc comment).
    AggregatedConsequencesByQuantile(std::string damage_category, std::string asset_category,
                                      statistics::distributions::Empirical empirical, int impact_area_id,
                                      ConsequenceType consequence_type, RiskType risk_type)
        : consequence_distribution_(std::move(empirical)),
          damage_category_(std::move(damage_category)),
          asset_category_(std::move(asset_category)),
          region_id_(impact_area_id),
          consequence_type_(consequence_type),
          risk_type_(risk_type),
          is_null_(false) {}

    const statistics::distributions::Empirical& consequence_distribution() const {
        return consequence_distribution_;
    }
    const std::string& damage_category() const { return damage_category_; }
    const std::string& asset_category() const { return asset_category_; }
    int region_id() const { return region_id_; }
    ConsequenceType consequence_type() const { return consequence_type_; }
    RiskType risk_type() const { return risk_type_; }
    bool is_null() const { return is_null_; }

    // ported from: AggregatedConsequencesByQuantile.cs `internal double ConsequenceSampleMean()`.
    double consequence_sample_mean() const { return consequence_distribution_.sample_mean(); }

    // ported from: AggregatedConsequencesByQuantile.cs `internal double
    // ConsequenceExceededWithProbabilityQ(double exceedanceProbability)`.
    double consequence_exceeded_with_probability_q(double exceedance_probability) const {
        double non_exceedance_probability = 1 - exceedance_probability;
        return consequence_distribution_.inverse_cdf(non_exceedance_probability);
    }

   private:
    statistics::distributions::Empirical consequence_distribution_;
    std::string damage_category_;
    std::string asset_category_;
    int region_id_;
    ConsequenceType consequence_type_;
    RiskType risk_type_;
    bool is_null_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_AGGREGATED_CONSEQUENCES_BY_QUANTILE_HPP
