// ported from: upstream/HEC-FDA/HEC.FDA.Model/structures/DeterministicOccupancyType.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_STRUCTURES_DETERMINISTIC_OCCUPANCY_TYPE_HPP
#define HECFDA_MODEL_STRUCTURES_DETERMINISTIC_OCCUPANCY_TYPE_HPP
#include <string>
#include "hecfda/model/paired_data/paired_data.hpp"
namespace hecfda {
namespace model {
namespace structures {

// ported from: DeterministicOccupancyType.cs. Pure data holder produced by OccupancyType::sample()
// (see occupancy_type.hpp): one set of already-sampled depth-percent-damage curves and value/offset
// scalars for a single probability/iteration. No behavior, no base class in C# (not
// ValidationErrorLogger/ICanBeNull/etc.) -- just a 24-field record with a single public ctor that
// always sets IsNull=false (there is no other ctor that could produce a "null" instance in this
// class; IsNull is a vestigial field carried over from the record's field list, transcribed
// verbatim rather than dropped).
//
// Unlike UncertainPairedData/OccupancyType, PairedData is copyable (plain std::vector<double>
// storage, no unique_ptr), so this class is an ordinary value type -- copyable and movable, matching
// C#'s object-with-getters shape closely enough that no special ownership handling is needed here.
class DeterministicOccupancyType {
   public:
    using PairedData = hecfda::model::paired_data::PairedData;

    // ported from: DeterministicOccupancyType.cs DeterministicOccupancyType(...). Parameter order
    // mirrors the C# ctor exactly (see class comment above); the five trailing `bool ... = false`
    // parameters are the C# ctor's `isStructureValueLogNormal = false` etc. defaults.
    DeterministicOccupancyType(std::string occupancy_type_name, std::string occupancy_type_damage_category,
                                PairedData struct_percent_damage_paired_data,
                                double sampled_first_floor_elevation, double sampled_structure_value,
                                bool compute_content_damage, bool compute_vehicle_damage,
                                bool compute_other_damage, PairedData content_percent_damage_paired_data,
                                double sampled_content_value, bool use_csvr, double csvr,
                                PairedData vehicle_percent_damage_paired_data, double sampled_vehicle_value,
                                PairedData other_percent_damage_paired_data, double sampled_other_value,
                                bool use_osvr, double osvr, bool is_structure_value_log_normal = false,
                                bool is_content_value_log_normal = false, bool is_other_value_log_normal = false,
                                bool is_vehicle_value_log_normal = false,
                                bool is_first_floor_elevation_log_normal = false)
        : occupancy_type_name_(std::move(occupancy_type_name)),
          occupancy_type_damage_category_(std::move(occupancy_type_damage_category)),
          struct_percent_damage_paired_data_(std::move(struct_percent_damage_paired_data)),
          content_percent_damage_paired_data_(std::move(content_percent_damage_paired_data)),
          vehicle_percent_damage_paired_data_(std::move(vehicle_percent_damage_paired_data)),
          other_percent_damage_paired_data_(std::move(other_percent_damage_paired_data)),
          first_floor_elevation_offset_(sampled_first_floor_elevation),
          structure_value_offset_(sampled_structure_value),
          content_value_offset_(sampled_content_value),
          vehicle_value_offset_(sampled_vehicle_value),
          other_value_offset_(sampled_other_value),
          compute_content_damage_(compute_content_damage),
          compute_vehicle_damage_(compute_vehicle_damage),
          compute_other_damage_(compute_other_damage),
          use_csvr_(use_csvr),
          use_osvr_(use_osvr),
          content_to_structure_value_ratio_(csvr),
          other_to_structure_value_ratio_(osvr),
          is_structure_value_log_normal_(is_structure_value_log_normal),
          is_content_value_log_normal_(is_content_value_log_normal),
          is_other_value_log_normal_(is_other_value_log_normal),
          is_vehicle_value_log_normal_(is_vehicle_value_log_normal),
          is_first_floor_elevation_log_normal_(is_first_floor_elevation_log_normal),
          is_null_(false) {}

    bool compute_content_damage() const { return compute_content_damage_; }
    bool compute_vehicle_damage() const { return compute_vehicle_damage_; }
    bool compute_other_damage() const { return compute_other_damage_; }
    bool use_csvr() const { return use_csvr_; }
    bool use_osvr() const { return use_osvr_; }
    const std::string& occupancy_type_name() const { return occupancy_type_name_; }
    const std::string& occupancy_type_damage_category() const { return occupancy_type_damage_category_; }
    const PairedData& struct_percent_damage_paired_data() const { return struct_percent_damage_paired_data_; }
    const PairedData& content_percent_damage_paired_data() const { return content_percent_damage_paired_data_; }
    const PairedData& vehicle_percent_damage_paired_data() const { return vehicle_percent_damage_paired_data_; }
    const PairedData& other_percent_damage_paired_data() const { return other_percent_damage_paired_data_; }
    double first_floor_elevation_offset() const { return first_floor_elevation_offset_; }
    double structure_value_offset() const { return structure_value_offset_; }
    double content_value_offset() const { return content_value_offset_; }
    double vehicle_value_offset() const { return vehicle_value_offset_; }
    double other_value_offset() const { return other_value_offset_; }
    double content_to_structure_value_ratio() const { return content_to_structure_value_ratio_; }
    double other_to_structure_value_ratio() const { return other_to_structure_value_ratio_; }
    bool is_structure_value_log_normal() const { return is_structure_value_log_normal_; }
    bool is_content_value_log_normal() const { return is_content_value_log_normal_; }
    bool is_other_value_log_normal() const { return is_other_value_log_normal_; }
    bool is_vehicle_value_log_normal() const { return is_vehicle_value_log_normal_; }
    bool is_first_floor_elevation_log_normal() const { return is_first_floor_elevation_log_normal_; }
    bool is_null() const { return is_null_; }

   private:
    std::string occupancy_type_name_;
    std::string occupancy_type_damage_category_;
    PairedData struct_percent_damage_paired_data_;
    PairedData content_percent_damage_paired_data_;
    PairedData vehicle_percent_damage_paired_data_;
    PairedData other_percent_damage_paired_data_;
    double first_floor_elevation_offset_;
    double structure_value_offset_;
    double content_value_offset_;
    double vehicle_value_offset_;
    double other_value_offset_;
    bool compute_content_damage_;
    bool compute_vehicle_damage_;
    bool compute_other_damage_;
    bool use_csvr_;
    bool use_osvr_;
    double content_to_structure_value_ratio_;
    double other_to_structure_value_ratio_;
    bool is_structure_value_log_normal_;
    bool is_content_value_log_normal_;
    bool is_other_value_log_normal_;
    bool is_vehicle_value_log_normal_;
    bool is_first_floor_elevation_log_normal_;
    bool is_null_;
};

}  // namespace structures
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_STRUCTURES_DETERMINISTIC_OCCUPANCY_TYPE_HPP
