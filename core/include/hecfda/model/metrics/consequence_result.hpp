// ported from: HEC.FDA.Model/metrics/ConsequenceResult.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_CONSEQUENCE_RESULT_HPP
#define HECFDA_MODEL_METRICS_CONSEQUENCE_RESULT_HPP
#include <string>
namespace hecfda {
namespace model {
namespace metrics {

// ported from: ConsequenceResult.cs `public class ConsequenceResult`. A plain per-structure
// damage accumulator: four running damage totals (structure/content/vehicle/other) and their
// matching "how many structures had nonzero damage of this kind" counters, plus the damage
// category label the result was built for. Zero external deps -- a leaf.
//
// C# leaves a `//TODO: I THINK SOME OR ALL OF THIS CLASS SHOULD BE INTERNAL` comment at the top
// of the class; transcribed here for provenance, not acted on (this port has no assembly
// boundary to enforce that visibility).
class ConsequenceResult {
   public:
    // ported from: ConsequenceResult.cs ConsequenceResult() -- the parameterless ctor.
    // DamageCategory = "unassigned", IsNull = true.
    ConsequenceResult() : damage_category_("unassigned"), is_null_(true) {}

    // ported from: ConsequenceResult.cs ConsequenceResult(string damageCategory).
    // IsNull = false.
    explicit ConsequenceResult(const std::string& damage_category)
        : damage_category_(damage_category), is_null_(false) {}

    int damaged_structures_quantity() const { return damaged_structures_quantity_; }
    int damaged_contents_quantity() const { return damaged_contents_quantity_; }
    int damaged_others_quantity() const { return damaged_others_quantity_; }
    int damaged_vehicles_quantity() const { return damaged_vehicles_quantity_; }
    const std::string& damage_category() const { return damage_category_; }
    double other_damage() const { return other_damage_; }
    double structure_damage() const { return structure_damage_; }
    double content_damage() const { return content_damage_; }
    double vehicle_damage() const { return vehicle_damage_; }
    bool is_null() const { return is_null_; }

    // ported from: ConsequenceResult.cs public void IncrementConsequence(double structureDamage,
    // double contentDamage = 0, double vehicleDamage = 0, double otherDamage = 0). Each damage is
    // added to its running total; for each argument strictly greater than 0, the matching quantity
    // counter is incremented by 1. Field/guard order transcribed verbatim (structure, content,
    // other, vehicle -- note "other" is checked before "vehicle", matching the C# source order).
    void increment_consequence(double structure_damage, double content_damage = 0,
                                double vehicle_damage = 0, double other_damage = 0) {
        structure_damage_ += structure_damage;
        if (structure_damage > 0) {
            damaged_structures_quantity_ += 1;
        }
        content_damage_ += content_damage;
        if (content_damage > 0) {
            damaged_contents_quantity_ += 1;
        }
        other_damage_ += other_damage;
        if (other_damage > 0) {
            damaged_others_quantity_ += 1;
        }
        vehicle_damage_ += vehicle_damage;
        if (vehicle_damage > 0) {
            damaged_vehicles_quantity_ += 1;
        }
    }

    // ported from: ConsequenceResult.cs internal bool Equals(ConsequenceResult damageResult).
    // Field-by-field short-circuit comparison of the four damage totals and the damage category
    // -- NOT the four quantity counters or IsNull, matching the C# source exactly.
    bool equals(const ConsequenceResult& damage_result) const {
        if (!(structure_damage_ == damage_result.structure_damage_)) {
            return false;
        }
        if (!(content_damage_ == damage_result.content_damage_)) {
            return false;
        }
        if (!(other_damage_ == damage_result.other_damage_)) {
            return false;
        }
        if (!(vehicle_damage_ == damage_result.vehicle_damage_)) {
            return false;
        }
        if (!(damage_category_ == damage_result.damage_category_)) {
            return false;
        }
        return true;
    }

   private:
    int damaged_structures_quantity_ = 0;
    int damaged_contents_quantity_ = 0;
    int damaged_others_quantity_ = 0;
    int damaged_vehicles_quantity_ = 0;
    std::string damage_category_;
    double other_damage_ = 0;
    double structure_damage_ = 0;
    double content_damage_ = 0;
    double vehicle_damage_ = 0;
    bool is_null_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_CONSEQUENCE_RESULT_HPP
