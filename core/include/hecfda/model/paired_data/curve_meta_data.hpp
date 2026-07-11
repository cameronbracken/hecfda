// ported from: HEC.FDA.Model/paireddata/CurveMetaData.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_PAIRED_DATA_CURVE_META_DATA_HPP
#define HECFDA_MODEL_PAIRED_DATA_CURVE_META_DATA_HPP
#include <string>
namespace hecfda {
namespace model {
namespace paired_data {
// Immutable-except-for-YLabel/IsNull curve label bag, VERBATIM including the field-level
// mutability split from the C# auto-properties: XLabel/Name/DamageCategory/AssetCategory/
// ImpactAreaID are `{ get; }` (settable only via ctor), YLabel and IsNull are `{ get; set; }`.
// WriteToXML/ReadFromXML are SEVERED (documented severance -- no XML/serialization layer in this
// port; StoredPropertyAttribute tags on the C# fields are dropped for the same reason).
class CurveMetaData {
   public:
    // ported from: CurveMetaData.cs CurveMetaData() -- the parameterless ctor.
    // NOTE (upstream quirk, kept verbatim): DamageCategory here is "unassiged" (misspelled),
    // while the 3-arg ctor below uses the correctly spelled "unassigned". Both spellings are
    // real upstream text -- do not "fix" the typo.
    CurveMetaData()
        : x_label_("xlabel"),
          y_label_("ylabel"),
          name_("unnamed"),
          damage_category_("unassiged"),
          asset_category_("unassigned"),
          is_null_(true),
          impact_area_id_(0) {}

    // ported from: CurveMetaData.cs CurveMetaData(string damageCategory, string assetCategory = "unassigned")
    explicit CurveMetaData(const std::string& damage_category, const std::string& asset_category = "unassigned")
        : x_label_("xlabel"),
          y_label_("ylabel"),
          name_("unnamed"),
          damage_category_(damage_category),
          asset_category_(asset_category),
          is_null_(false),
          impact_area_id_(0) {}

    // ported from: CurveMetaData.cs
    // CurveMetaData(string xlabel, string ylabel, string name, string damageCategory, string assetCategory = "unassigned")
    CurveMetaData(const std::string& xlabel, const std::string& ylabel, const std::string& name,
                  const std::string& damage_category, const std::string& asset_category = "unassigned")
        : x_label_(xlabel),
          y_label_(ylabel),
          name_(name),
          damage_category_(damage_category),
          asset_category_(asset_category),
          is_null_(false),
          impact_area_id_(0) {}

    // ported from: CurveMetaData.cs CurveMetaData(string xlabel, string ylabel, string name)
    // DamageCategory/AssetCategory both default to the correctly-spelled "unassigned" here.
    CurveMetaData(const std::string& xlabel, const std::string& ylabel, const std::string& name)
        : x_label_(xlabel),
          y_label_(ylabel),
          name_(name),
          damage_category_("unassigned"),
          asset_category_("unassigned"),
          is_null_(false),
          impact_area_id_(0) {}

    // ported from: CurveMetaData.cs
    // CurveMetaData(string xlabel, string ylabel, string name, string damageCategory, int impactAreaID, string assetCategory = "unassigned")
    CurveMetaData(const std::string& xlabel, const std::string& ylabel, const std::string& name,
                  const std::string& damage_category, int impact_area_id,
                  const std::string& asset_category = "unassigned")
        : x_label_(xlabel),
          y_label_(ylabel),
          name_(name),
          damage_category_(damage_category),
          asset_category_(asset_category),
          is_null_(false),
          impact_area_id_(impact_area_id) {}

    const std::string& x_label() const { return x_label_; }
    const std::string& y_label() const { return y_label_; }
    void set_y_label(const std::string& y_label) { y_label_ = y_label; }
    const std::string& name() const { return name_; }
    const std::string& damage_category() const { return damage_category_; }
    const std::string& asset_category() const { return asset_category_; }
    bool is_null() const { return is_null_; }
    void set_is_null(bool is_null) { is_null_ = is_null; }
    int impact_area_id() const { return impact_area_id_; }

   private:
    std::string x_label_;
    std::string y_label_;
    std::string name_;
    std::string damage_category_;
    std::string asset_category_;
    bool is_null_;
    int impact_area_id_;
};
}  // namespace paired_data
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_PAIRED_DATA_CURVE_META_DATA_HPP
