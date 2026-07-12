// ported from: HEC.FDA.Model/metrics/CategoriedPairedData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_CATEGORIED_PAIRED_DATA_HPP
#define HECFDA_MODEL_METRICS_CATEGORIED_PAIRED_DATA_HPP
#include <string>
#include <utility>
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: CategoriedPairedData.cs `public class CategoriedPairedData`. A tiny immutable
// record-like holder pairing one MC-realization frequency curve (a damage-frequency or
// FN-frequency `PairedData`) with the four labels a `CategoriedUncertainPairedData` accumulator
// groups realizations by: `ConsequenceType`, `RiskType`, `DamageCategory`, `AssetCategory`. The
// whole upstream file is 21 lines: one ctor, five auto-properties, nothing else -- no XML, no
// other methods to sever.
class CategoriedPairedData {
   public:
    // ported from: CategoriedPairedData.cs CategoriedPairedData(PairedData frequencyCurve, string
    // damageCategory, string assetCategory, ConsequenceType consequenceType, RiskType riskType).
    CategoriedPairedData(paired_data::PairedData frequency_curve, std::string damage_category,
                          std::string asset_category, ConsequenceType consequence_type,
                          RiskType risk_type)
        : frequency_curve_(std::move(frequency_curve)),
          consequence_type_(consequence_type),
          risk_type_(risk_type),
          damage_category_(std::move(damage_category)),
          asset_category_(std::move(asset_category)) {}

    const paired_data::PairedData& frequency_curve() const { return frequency_curve_; }
    ConsequenceType consequence_type() const { return consequence_type_; }
    RiskType risk_type() const { return risk_type_; }
    const std::string& damage_category() const { return damage_category_; }
    const std::string& asset_category() const { return asset_category_; }

   private:
    paired_data::PairedData frequency_curve_;
    ConsequenceType consequence_type_;
    RiskType risk_type_;
    std::string damage_category_;
    std::string asset_category_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_CATEGORIED_PAIRED_DATA_HPP
