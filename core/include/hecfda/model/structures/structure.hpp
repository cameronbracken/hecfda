// ported from: upstream/HEC-FDA/HEC.FDA.Model/structures/Structure.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_STRUCTURES_STRUCTURE_HPP
#define HECFDA_MODEL_STRUCTURES_STRUCTURE_HPP
#include <cmath>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include "hecfda/model/structures/deterministic_occupancy_type.hpp"
#include "hecfda/statistics/validation.hpp"
namespace hecfda {
namespace model {
namespace structures {

// ported from: Structure.cs. C# is `class Structure : Validation`. Structure is the per-structure
// numeric depth-damage compute: given a sampled DeterministicOccupancyType (produced by
// OccupancyType::sample(), see occupancy_type.hpp) and a water-surface elevation, it looks up the
// depth-percent-damage curves at the structure's depth-above-first-floor and turns the resulting
// percentages into a (structure, content, vehicle, other) dollar-damage tuple.
//
// SEVERED (spatial / non-numeric):
//  - Both C# constructors -- `Structure(string, PointM, ...)` and
//    `Structure(string, Geospatial.Vectors.Point, ...)` -- and the `PointM Point` property are
//    dropped in favor of ONE numeric constructor with no point parameter (mirrors patched/
//    Structure.cs in the oracle emitter, and the "PointM/Point ctors -> one numeric ctor"
//    severance called out in CLAUDE.md's structures scope).
//  - `ConsequenceResult ComputeDamage(float, List<DeterministicOccupancyType>, double, int)` is
//    dropped (HEC.FDA.Model.metrics dependency, Phase 5). Only the tuple-returning
//    `ComputeDamage(float, DeterministicOccupancyType, double, int)` overload it wraps is ported;
//    `find_occ_type`/`find_occ_type_index` (its `FindOccType` lookup helper) are kept standalone
//    since they have no metrics dependency of their own.
//  - `ProduceDetailsHeader`/`ProduceDetails` (CSV row output, needs Point.X/Y and an impact-area
//    name lookup) and `CalculateDepthZeroDamage`/`FindHighestDepthZeroPercentDamage` (their only
//    caller, uses `f_inverse`) are dropped -- CSV/reporting, Phase 5.
//
// `DEFAULT_MISSING_VALUE` ports `HEC.FDA.Model.utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE`
// (`-999`, confirmed in upstream/HEC-FDA/HEC.FDA.Model/utilities/IntegerGlobalConstants.cs), defined
// locally here as `kDefaultMissingValue` since that whole (mostly-severed) utilities file isn't
// otherwise ported.
//
// `compute_damage` is intentionally NON-CONST: it mutates the four
// `LastWSPStageDamageSegmentTopIndex*` members (sequential-search cursors for
// PairedData::f(x, ref index), see paired_data.hpp), exactly mirroring the C# method's mutation of
// its four private int fields. `reset_index_tracking()` (ported from `ResetIndexTracking`) resets
// all four back to `1`.
class Structure : public hecfda::statistics::Validation {
   public:
    using DeterministicOccupancyType = hecfda::model::structures::DeterministicOccupancyType;

    // ported from: Structure.cs `utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE` (== -999).
    static constexpr int kDefaultMissingValue = -999;

    // ported from: Structure.cs Structure(string fid, PointM/Point point, double
    // firstFloorElevation, ...) -- numeric ctor, point parameter severed (see class comment).
    // Parameter order and defaults mirror the C# ctor exactly (minus `point`).
    Structure(std::string fid, double first_floor_elevation, double val_struct, std::string st_damcat,
               std::string occtype, int impact_area_id, double val_cont = 0, double val_vehic = 0,
               double val_other = 0, std::string cbfips = "unassigned",
               double begin_damage = kDefaultMissingValue, double ground_elevation = kDefaultMissingValue,
               double foundation_height = kDefaultMissingValue, int year = kDefaultMissingValue,
               int num_structures = 1, std::string notes = "", std::string description = "")
        : fid_(std::move(fid)),
          first_floor_elevation_(first_floor_elevation),
          ground_elevation_(ground_elevation),
          inventoried_structure_value_(val_struct),
          inventoried_content_value_(val_cont),
          inventoried_vehicle_value_(val_vehic),
          inventoried_other_value_(val_other),
          damage_catagory_(std::move(st_damcat)),
          occ_type_name_(std::move(occtype)),
          impact_area_id_(impact_area_id),
          cbfips_(std::move(cbfips)),
          beginning_damage_depth_(begin_damage),
          foundation_height_(foundation_height),
          year_in_service_(year),
          number_of_structures_(num_structures),
          notes_(std::move(notes)),
          description_(std::move(description)) {
        add_rules();
    }

    const std::string& fid() const { return fid_; }
    double inventoried_structure_value() const { return inventoried_structure_value_; }
    // ported from: Structure.cs `DamageCatagory` -- upstream spelling ("Catagory") kept verbatim.
    const std::string& damage_catagory() const { return damage_catagory_; }
    const std::string& occ_type_name() const { return occ_type_name_; }
    int impact_area_id() const { return impact_area_id_; }
    double ground_elevation() const { return ground_elevation_; }
    // ported from: Structure.cs `internal double FoundationHeight { get; }`. Stored but never read
    // by ComputeDamage in the real C# either (only CalculateDepthZeroDamage/ProduceDetails touch
    // it, both severed -- see class comment); exposed here anyway, mirroring the C# getter.
    double foundation_height() const { return foundation_height_; }

    // ported from: Structure.cs public (double, double, double, double) ComputeDamage(float
    // waterSurfaceElevation, DeterministicOccupancyType deterministicOccupancyType, double
    // priceIndex = 1, int analysisYear = 9999). NON-CONST (see class comment).
    //
    // Returns (structDamage, contDamage, vehicleDamage, otherDamage), transcribed verbatim
    // (including the guards, clamps, and log-normal/CSVR/OSVR branches).
    std::tuple<double, double, double, double> compute_damage(
        float water_surface_elevation, const DeterministicOccupancyType& deterministic_occupancy_type,
        double price_index = 1, int analysis_year = 9999) {
        double sampled_ffe;
        if (deterministic_occupancy_type.is_first_floor_elevation_log_normal()) {
            sampled_ffe = first_floor_elevation_ * deterministic_occupancy_type.first_floor_elevation_offset();
        } else {
            sampled_ffe = first_floor_elevation_ + deterministic_occupancy_type.first_floor_elevation_offset();
        }
        double depthabovefoundheight = water_surface_elevation - sampled_ffe;
        double sampled_structure_value = 0.0;
        double struct_damage = 0;
        double cont_damage = 0;
        double vehicle_damage = 0;
        double other_damage = 0;

        // house should have been constructed before or equal to the analysis year to be damaged
        if (year_in_service_ <= analysis_year) {
            // Beginning damage depth is relative to the first floor elevation and so a beginning
            // damage depth of -1 means that damage begins 1 foot below the first floor elevation;
            // if not defined by the user, the beginning damage depth is equal to the negative of
            // foundation height
            if (beginning_damage_depth_ <= depthabovefoundheight) {
                // Structure
                double struct_damage_percent = deterministic_occupancy_type.struct_percent_damage_paired_data().f(
                    depthabovefoundheight, last_wsp_stage_damage_segment_top_index_structure_);
                if (struct_damage_percent > 100) {
                    struct_damage_percent = 100;
                }
                if (struct_damage_percent < 0) {
                    struct_damage_percent = 0;
                }
                if (deterministic_occupancy_type.is_structure_value_log_normal()) {
                    sampled_structure_value =
                        std::pow(deterministic_occupancy_type.structure_value_offset(),
                                 std::log(inventoried_structure_value_)) *
                        inventoried_structure_value_;
                    struct_damage =
                        (struct_damage_percent / 100) * price_index * number_of_structures_ * sampled_structure_value;
                } else {
                    sampled_structure_value =
                        inventoried_structure_value_ * deterministic_occupancy_type.structure_value_offset();
                    struct_damage =
                        (struct_damage_percent / 100) * price_index * number_of_structures_ * sampled_structure_value;
                }

                // Content
                if (deterministic_occupancy_type.compute_content_damage()) {
                    double content_damage_percent =
                        deterministic_occupancy_type.content_percent_damage_paired_data().f(
                            depthabovefoundheight, last_wsp_stage_damage_segment_top_index_content_);
                    if (content_damage_percent > 100) {
                        content_damage_percent = 100;
                    }
                    if (content_damage_percent < 0) {
                        content_damage_percent = 0;
                    }
                    if (deterministic_occupancy_type.use_csvr()) {
                        cont_damage = (content_damage_percent / 100) * price_index * number_of_structures_ *
                                      (deterministic_occupancy_type.content_to_structure_value_ratio() / 100) *
                                      sampled_structure_value;
                    } else {
                        if (deterministic_occupancy_type.is_content_value_log_normal()) {
                            double sampled_content_value =
                                std::pow(deterministic_occupancy_type.content_value_offset(),
                                         std::log(inventoried_content_value_)) *
                                inventoried_content_value_;
                            cont_damage = (content_damage_percent / 100) * price_index * number_of_structures_ *
                                          sampled_content_value;
                        } else {
                            double sampled_content_value =
                                inventoried_content_value_ * deterministic_occupancy_type.content_value_offset();
                            cont_damage = (content_damage_percent / 100) * price_index * number_of_structures_ *
                                          sampled_content_value;
                        }
                    }
                }

                // Vehicle
                if (deterministic_occupancy_type.compute_vehicle_damage()) {
                    double vehicle_damage_percent =
                        deterministic_occupancy_type.vehicle_percent_damage_paired_data().f(
                            depthabovefoundheight, last_wsp_stage_damage_segment_top_index_vehicle_);
                    if (vehicle_damage_percent > 100) {
                        vehicle_damage_percent = 100;
                    }
                    if (vehicle_damage_percent < 0) {
                        vehicle_damage_percent = 0;
                    }
                    if (deterministic_occupancy_type.is_vehicle_value_log_normal()) {
                        double sampled_vehicle_value =
                            std::pow(deterministic_occupancy_type.vehicle_value_offset(),
                                     std::log(inventoried_vehicle_value_)) *
                            inventoried_vehicle_value_;
                        vehicle_damage = (vehicle_damage_percent / 100) * price_index * number_of_structures_ *
                                         sampled_vehicle_value;
                    } else {
                        double sampled_vehicle_value =
                            inventoried_vehicle_value_ * deterministic_occupancy_type.vehicle_value_offset();
                        vehicle_damage = (vehicle_damage_percent / 100) * price_index * number_of_structures_ *
                                         sampled_vehicle_value;
                    }
                }

                // Other
                if (deterministic_occupancy_type.compute_other_damage()) {
                    double other_damage_percent = deterministic_occupancy_type.other_percent_damage_paired_data().f(
                        depthabovefoundheight, last_wsp_stage_damage_segment_top_index_other_);
                    if (other_damage_percent > 100) {
                        other_damage_percent = 100;
                    }
                    if (other_damage_percent < 0) {
                        other_damage_percent = 0;
                    }
                    if (deterministic_occupancy_type.use_osvr()) {
                        other_damage = (other_damage_percent / 100) * price_index * number_of_structures_ *
                                       sampled_structure_value *
                                       (deterministic_occupancy_type.other_to_structure_value_ratio() / 100);
                    } else {
                        if (deterministic_occupancy_type.is_other_value_log_normal()) {
                            double sampled_other_value =
                                std::pow(deterministic_occupancy_type.other_value_offset(),
                                         std::log(inventoried_other_value_)) *
                                inventoried_other_value_;
                            other_damage = (other_damage_percent / 100) * price_index * number_of_structures_ *
                                           sampled_other_value;
                        } else {
                            double sampled_other_value =
                                inventoried_other_value_ * deterministic_occupancy_type.other_value_offset();
                            other_damage = (other_damage_percent / 100) * price_index * number_of_structures_ *
                                           sampled_other_value;
                        }
                    }
                }
            }
        }
        return {struct_damage, cont_damage, vehicle_damage, other_damage};
    }

    // ported from: Structure.cs public DeterministicOccupancyType FindOccType(List<...> list).
    DeterministicOccupancyType find_occ_type(const std::vector<DeterministicOccupancyType>& list) const {
        int index = find_occ_type_index(list);
        return list[static_cast<std::size_t>(index)];
    }

    // ported from: Structure.cs public int FindOccTypeIndex(List<...> list). Linear search by
    // OccupancyTypeName; throws (matching the C# `throw new Exception(...)`) if not found.
    int find_occ_type_index(const std::vector<DeterministicOccupancyType>& list) const {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i].occupancy_type_name() == occ_type_name_) {
                return static_cast<int>(i);
            }
        }
        throw std::runtime_error("Failed to find OccupancyType Named: " + occ_type_name_ +
                                  " referenced by structure ID: " + fid_);
    }

    // ported from: Structure.cs internal void ResetIndexTracking().
    void reset_index_tracking() {
        last_wsp_stage_damage_segment_top_index_structure_ = 1;
        last_wsp_stage_damage_segment_top_index_content_ = 1;
        last_wsp_stage_damage_segment_top_index_other_ = 1;
        last_wsp_stage_damage_segment_top_index_vehicle_ = 1;
    }

   private:
    // ported from: Structure.cs private void AddRules() -- seven Fatal rules, messages transcribed
    // verbatim (substituting Fid the way the C# string interpolation does).
    //
    // BUGFIX (not a C# transcription issue -- a C++-port-only memory-safety defect found and fixed
    // during Task 6/Inventory): the predicates below capture the checked fields BY VALUE
    // (`[value = first_floor_elevation_]`), not `[this]`. Structure has no deleted copy/move ops
    // (unlike OccupancyType, it is an ordinary copyable/movable value type), and every field
    // checked here is set once in the ctor and never mutated after -- so a by-value capture is
    // behaviorally identical to reading `this->field_` at validate()-time, for the lifetime of the
    // object. A `[this]` capture is NOT safe here: any relocation of a Structure after
    // construction (e.g. `std::vector<Structure>::push_back(Structure(...))`, which move-
    // constructs the vector's element from a short-lived temporary, or any vector growth/
    // reallocation) leaves these closures holding a dangling pointer to the temporary's destroyed
    // storage -- confirmed via ASan (stack-use-after-scope) while building Inventory, whose
    // trim/validate methods are the first callers to store multiple Structures in a
    // `std::vector<Structure>` and then call `validate()` on them. See inventory.hpp for the
    // storage discussion this fix unblocks.
    void add_rules() {
        add_single_property_rule(
            "FirstFloorElevation", [value = first_floor_elevation_]() { return value > -300; },
            "First floor elevation must be greater than -300, but is " +
                std::to_string(first_floor_elevation_) + " for Structure " + fid_,
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "InventoriedStructureValue", [value = inventoried_structure_value_]() { return value >= 0; },
            "The inventoried structure value must be greater than or equal to 0, but is " +
                std::to_string(inventoried_structure_value_) + " for Structure " + fid_,
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "InventoriedContentValue", [value = inventoried_content_value_]() { return value >= 0; },
            "The inventoried content value must be greater than or equal to 0, but is " +
                std::to_string(inventoried_content_value_) + " for Structure " + fid_,
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "InventoriedOtherValue", [value = inventoried_other_value_]() { return value >= 0; },
            "The inventoried other value must be greater than or equal to 0, but is " +
                std::to_string(inventoried_other_value_) + " for Structure " + fid_,
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "InventoriedVehicleValue", [value = inventoried_vehicle_value_]() { return value >= 0; },
            "The inventoried vehicle value must be greater than or equal to 0, but is " +
                std::to_string(inventoried_vehicle_value_) + " for Structure " + fid_,
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "DamageCatagory", [value = damage_catagory_]() { return !value.empty(); },
            "Damage category should not be null but appears null for Structure " + fid_,
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "OccTypeName", [value = occ_type_name_]() { return !value.empty(); },
            "The occupancy type should not be null but appears null for Structure " + fid_,
            hecfda::statistics::ErrorLevel::Fatal);
    }

    std::string fid_;
    double first_floor_elevation_;
    double ground_elevation_;
    double inventoried_structure_value_;
    double inventoried_content_value_;
    double inventoried_vehicle_value_;
    double inventoried_other_value_;
    std::string damage_catagory_;
    std::string occ_type_name_;
    int impact_area_id_;
    std::string cbfips_;
    double beginning_damage_depth_;
    double foundation_height_;
    int year_in_service_;
    int number_of_structures_;
    std::string notes_;
    std::string description_;

    // ported from: Structure.cs private int LastWSPStageDamageSegmentTopIndex{Structure,Content,
    // Vehicle,Other} = 1. Sequential-search cursors mutated by compute_damage() (see class
    // comment); reset via reset_index_tracking().
    int last_wsp_stage_damage_segment_top_index_structure_ = 1;
    int last_wsp_stage_damage_segment_top_index_content_ = 1;
    int last_wsp_stage_damage_segment_top_index_vehicle_ = 1;
    int last_wsp_stage_damage_segment_top_index_other_ = 1;
};

}  // namespace structures
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_STRUCTURES_STRUCTURE_HPP
