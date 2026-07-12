// ported from: HEC.FDA.Model/metrics/StudyAreaConsequencesByQuantile.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_STUDY_AREA_CONSEQUENCES_BY_QUANTILE_HPP
#define HECFDA_MODEL_METRICS_STUDY_AREA_CONSEQUENCES_BY_QUANTILE_HPP
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/aggregated_consequences_by_quantile.hpp"
#include "hecfda/model/metrics/consequence_extensions.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/statistics/distributions/empirical.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: StudyAreaConsequencesByQuantile.cs `public class StudyAreaConsequencesByQuantile :
// Validation` -- the ByQuantile analog of StudyAreaConsequencesBinned
// (study_area_consequences_binned.hpp): a collection wrapper over per-(damageCategory,
// assetCategory, impactArea, ConsequenceType, RiskType) AggregatedConsequencesByQuantile results.
// Unlike Binned's move-only AggregatedConsequencesBinned members (owning unique_ptr<DynamicHistogram>),
// AggregatedConsequencesByQuantile holds its Empirical member BY VALUE (copyable, see that header's
// comment), so this class is copyable too -- no move-only ceremony needed here.
//
// SEVERANCES (present in the C# file, deliberately NOT ported / handled differently here):
//  - `Validation` base class + the `MessageReport` event + `ReportMessage(object, MessageEventArgs)`:
//    no MVVM/messaging infrastructure in this port (repo-wide MVVM severance).
//  - `GetConsequenceResult`/`GetAggregateEmpiricalDistribution`'s on-miss MVVM Fatal `ErrorMessage`
//    + `ReportMessage(...)` call: DIFFERENT SEVERANCE CHOICE from StudyAreaConsequencesBinned's
//    `get_specific_histogram`/`require_consequence_result` (which throw `std::runtime_error` on a
//    miss, because upstream documents that miss as "should never happen in practice"). Here, a miss
//    is NOT a defensive-only case: `AddExistingConsequenceResultObject` (ported below as
//    `add_existing_consequence_result_object`) calls `GetConsequenceResult` and inspects the
//    returned object's `IsNull` flag as its OWN dedup control flow -- the C# `ReportMessage` call is
//    a side-channel event notification (a no-op when `MessageReport` has no subscribers, which is
//    always true here since there's no MVVM bus in this port) that does NOT interrupt control flow;
//    `GetConsequenceResult`/`GetAggregateEmpiricalDistribution` always RETURN a value in C#, never
//    throw. So this port keeps that contract verbatim -- `get_consequence_result` returns a dummy
//    `AggregatedConsequencesByQuantile()` (`is_null() == true`) on a miss, and
//    `get_aggregate_empirical_distribution` returns a default `Empirical()` on an empty match set;
//    only the `ReportMessage` notification itself is omitted (documented no-op, not reproduced).
//
// Ctors transcribed exactly from the C# source's three ctors (see class members below):
//  - Parameterless: `ConsequenceResultList` holds ONE dummy `AggregatedConsequencesByQuantile()`,
//    `IsNull = true`, `AlternativeID` left at its C# default(int) == 0.
//  - `(int alternativeID)`: empty list, `AlternativeID` set, `IsNull = false`.
//  - `(std::vector<AggregatedConsequencesByQuantile>)` ("public for testing"): the given list,
//    `AlternativeID` left at 0, `IsNull = false`.
class StudyAreaConsequencesByQuantile {
   public:
    // ported from: StudyAreaConsequencesByQuantile.cs `public StudyAreaConsequencesByQuantile()`.
    StudyAreaConsequencesByQuantile()
        : consequence_result_list_{AggregatedConsequencesByQuantile()}, alternative_id_(0), is_null_(true) {}

    // ported from: StudyAreaConsequencesByQuantile.cs `internal StudyAreaConsequencesByQuantile(
    // int alternativeID)`.
    explicit StudyAreaConsequencesByQuantile(int alternative_id)
        : consequence_result_list_(), alternative_id_(alternative_id), is_null_(false) {}

    // ported from: StudyAreaConsequencesByQuantile.cs `internal StudyAreaConsequencesByQuantile(
    // List<AggregatedConsequencesByQuantile> damageResults)` -- "public for testing".
    explicit StudyAreaConsequencesByQuantile(std::vector<AggregatedConsequencesByQuantile> damage_results)
        : consequence_result_list_(std::move(damage_results)), alternative_id_(0), is_null_(false) {}

    const std::vector<AggregatedConsequencesByQuantile>& consequence_result_list() const {
        return consequence_result_list_;
    }
    bool is_null() const { return is_null_; }
    int alternative_id() const { return alternative_id_; }

    // ported from: StudyAreaConsequencesByQuantile.cs `public void AddExistingConsequenceResultObject(
    // AggregatedConsequencesByQuantile consequenceResultToAdd)` -- "public for testing purposes".
    // Dedupes via get_consequence_result's dummy-on-miss return contract (see class comment): adds
    // only if no existing result exactly matches consequenceResultToAdd's own (damageCategory,
    // assetCategory, RegionID, ConsequenceType, RiskType).
    void add_existing_consequence_result_object(AggregatedConsequencesByQuantile consequence_result_to_add) {
        AggregatedConsequencesByQuantile consequence_result = get_consequence_result(
            consequence_result_to_add.damage_category(), consequence_result_to_add.asset_category(),
            consequence_result_to_add.region_id(), consequence_result_to_add.consequence_type(),
            consequence_result_to_add.risk_type());
        if (consequence_result.is_null()) {
            consequence_result_list_.push_back(std::move(consequence_result_to_add));
        }
    }

    // ported from: StudyAreaConsequencesByQuantile.cs `public double SampleMeanDamage(string
    // damageCategory = null, string assetCategory = null, int impactAreaID = -999, ConsequenceType
    // consequenceType = ConsequenceType.Damage, RiskType riskType = RiskType.Total)`.
    double sample_mean_damage(const std::optional<std::string>& damage_category = std::nullopt,
                               const std::optional<std::string>& asset_category = std::nullopt,
                               int impact_area_id = kDefaultMissingValue,
                               ConsequenceType consequence_type = ConsequenceType::Damage,
                               RiskType risk_type = RiskType::Total) const {
        std::vector<AggregatedConsequencesByQuantile> matches = consequence_extensions::filter_by_categories(
            consequence_result_list_, damage_category, asset_category, impact_area_id, consequence_type,
            risk_type);
        double sum = 0.0;
        for (const AggregatedConsequencesByQuantile& result : matches) {
            sum += result.consequence_sample_mean();
        }
        return sum;
    }

    // ported from: StudyAreaConsequencesByQuantile.cs `public double
    // ConsequenceExceededWithProbabilityQ(double exceedanceProbability, string damageCategory =
    // null, string assetCategory = null, int impactAreaID = -999, ConsequenceType consequenceType =
    // ConsequenceType.Damage, RiskType riskType = RiskType.Total)`.
    double consequence_exceeded_with_probability_q(
        double exceedance_probability, const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt, int impact_area_id = kDefaultMissingValue,
        ConsequenceType consequence_type = ConsequenceType::Damage, RiskType risk_type = RiskType::Total) const {
        std::vector<AggregatedConsequencesByQuantile> matches = consequence_extensions::filter_by_categories(
            consequence_result_list_, damage_category, asset_category, impact_area_id, consequence_type,
            risk_type);
        double sum = 0.0;
        for (const AggregatedConsequencesByQuantile& result : matches) {
            sum += result.consequence_exceeded_with_probability_q(exceedance_probability);
        }
        return sum;
    }

    // ported from: StudyAreaConsequencesByQuantile.cs `public AggregatedConsequencesByQuantile
    // GetConsequenceResult(string damageCategory, string assetCategory, int impactAreaID = -999,
    // ConsequenceType consequenceType = ConsequenceType.Damage, RiskType riskType = RiskType.Total)`.
    // Returns a dummy `AggregatedConsequencesByQuantile()` (`is_null() == true`) on a miss -- see
    // class comment's SEVERANCES entry for why this does NOT throw (unlike
    // StudyAreaConsequencesBinned's private get_consequence_result/require_consequence_result).
    AggregatedConsequencesByQuantile get_consequence_result(
        const std::string& damage_category, const std::string& asset_category,
        int impact_area_id = kDefaultMissingValue, ConsequenceType consequence_type = ConsequenceType::Damage,
        RiskType risk_type = RiskType::Total) const {
        std::vector<AggregatedConsequencesByQuantile> matches = consequence_extensions::filter_by_categories(
            consequence_result_list_, damage_category, asset_category, impact_area_id, consequence_type,
            risk_type);
        if (!matches.empty()) {
            return matches.front();
        }
        // SEVERED MVVM: C# reports a Fatal ErrorMessage via ReportMessage(this, ...) here before
        // returning the dummy object below -- omitted (documented no-op, see class comment).
        return AggregatedConsequencesByQuantile();
    }

    // ported from: StudyAreaConsequencesByQuantile.cs `public Empirical
    // GetAggregateEmpiricalDistribution(string damageCategory = null, string assetCategory = null,
    // int impactAreaID = -999, ConsequenceType consequenceType = ConsequenceType.Damage, RiskType
    // riskType = RiskType.Total)`. Returns a default `Empirical()` on an empty match set -- see
    // class comment's SEVERANCES entry for why this does NOT throw.
    statistics::distributions::Empirical get_aggregate_empirical_distribution(
        const std::optional<std::string>& damage_category = std::nullopt,
        const std::optional<std::string>& asset_category = std::nullopt, int impact_area_id = kDefaultMissingValue,
        ConsequenceType consequence_type = ConsequenceType::Damage, RiskType risk_type = RiskType::Total) const {
        std::vector<AggregatedConsequencesByQuantile> matches = consequence_extensions::filter_by_categories(
            consequence_result_list_, damage_category, asset_category, impact_area_id, consequence_type,
            risk_type);
        if (matches.empty()) {
            // SEVERED MVVM: C# reports a Fatal ErrorMessage via ReportMessage(this, ...) here
            // before returning `new Empirical()` below -- omitted (documented no-op, see class
            // comment).
            return statistics::distributions::Empirical();
        }
        std::vector<statistics::distributions::Empirical> empirical_dists_to_stack;
        empirical_dists_to_stack.reserve(matches.size());
        for (const AggregatedConsequencesByQuantile& result : matches) {
            empirical_dists_to_stack.push_back(result.consequence_distribution());
        }
        return statistics::distributions::Empirical::stack_empirical_distributions(
            empirical_dists_to_stack, statistics::distributions::Empirical::StackOp::sum);
    }

   private:
    // ported from: StudyAreaConsequencesByQuantile.cs's implicit
    // `utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE` default arg (-999), defined locally
    // per the repo's established convention (see study_area_consequences_binned.hpp's own
    // kDefaultMissingValue) since the whole (mostly-severed) utilities file isn't otherwise ported.
    static constexpr int kDefaultMissingValue = -999;

    std::vector<AggregatedConsequencesByQuantile> consequence_result_list_;
    int alternative_id_;
    bool is_null_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_STUDY_AREA_CONSEQUENCES_BY_QUANTILE_HPP
