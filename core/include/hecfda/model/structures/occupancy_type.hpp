// ported from: upstream/HEC-FDA/HEC.FDA.Model/structures/OccupancyType.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_STRUCTURES_OCCUPANCY_TYPE_HPP
#define HECFDA_MODEL_STRUCTURES_OCCUPANCY_TYPE_HPP
#include <stdexcept>
#include <string>
#include <utility>
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/model/structures/deterministic_occupancy_type.hpp"
#include "hecfda/model/structures/first_floor_elevation_uncertainty.hpp"
#include "hecfda/model/structures/value_ratio_with_uncertainty.hpp"
#include "hecfda/model/structures/value_uncertainty.hpp"
#include "hecfda/statistics/validation.hpp"
namespace hecfda {
namespace model {
namespace structures {

// ported from: OccupancyType.cs. C# is `class OccupancyType : PropertyValidationHelper,
// IDontImplementValidationButMyPropertiesDo`. PropertyValidationHelper (HasErrors/ErrorLevel +
// ValidateProperty/ResetErrors) has no port-wide base class of its own yet, so its two fields and
// two methods are folded directly into this class (has_errors_/error_level_ +
// validate_property()); IDontImplementValidationButMyPropertiesDo is a thin
// Validate()/GetErrorsFromProperties()/messaging marker interface with no behavior beyond what
// OccupancyType itself implements, so it has no separate C++ analog.
//
// SEVERED: the `MessageReport` event, the private `ReportMessage(OccupancyType, MessageEventArgs)`
// helper (which wraps `MessageHub.Register/Unregister` around the event invoke), and the public
// `ReportMessage(object, MessageEventArgs)` override (which just `throw new
// NotImplementedException()` in C# anyway) are all messaging/MVVM plumbing with no reachable
// numeric effect -- dropped entirely, matching this port's blanket MVVM/messaging severance
// (see CLAUDE.md).
//
// SEVERED (validation-surface mismatch, not a messaging severance): UncertainPairedData's
// AddRules()/rule-registration was already severed in paired_data/uncertain_paired_data.hpp (it
// registers zero validation rules in this port), so it exposes no validate()/has_errors()/
// error_level() surface for validate()/get_errors_from_properties() to call here. The four C#
// calls to `_StructureDepthPercentDamageFunction`/`_ContentDepthPercentDamageFunction`/
// `_VehicleDepthPercentDamageFunction`/`_OtherDepthPercentDamageFunction`
// .GetErrorMessages(...)/ValidateProperty(...) are omitted below -- they would always be a no-op
// in the real C# too, since a Validation object with zero registered rules can never fail
// Validate() and so never contributes HasErrors/ErrorLevel/error messages.
//
// MOVE-ONLY: the four depth-percent-damage functions are UncertainPairedData, which is move-only
// (holds `vector<unique_ptr<IDistribution>>` -- see uncertain_paired_data.hpp), so OccupancyType
// holding them by value makes OccupancyType itself move-only too. Ownership: OccupancyTypeBuilder
// (nested below, mirroring the C# nested class) holds a single OccupancyType by value and moves it
// through the `with_*` chain, matching the C# builder's reference-semantics chain (`return new
// OccupancyTypeBuilder(_OccupancyType)` on every call, all wrapping the SAME underlying object) as
// closely as C++ move-only value semantics allow: every `with_*` method here returns
// `OccupancyTypeBuilder&&` (an rvalue reference to itself) rather than a new builder, so the fluent
// chain compiles and only the final `build()` needs to move the OccupancyType out.
//
// Faithful upstream bugs (deliberately reproduced, do NOT "fix" without an explicit upstream
// change to port):
//  - GetErrorsFromProperties()'s `ComputeOtherDamage` block checks `UseContentToStructureValueRatio`
//    (not `UseOtherToStructureValueRatio`) to decide whether to query
//    `_OtherToStructureValueRatio` vs. `_OtherValueError` for error messages -- a real C#
//    copy-paste bug (Validate()'s equivalent block correctly checks
//    `UseOtherToStructureValueRatio`). Transcribed verbatim below.
class OccupancyType {
   public:
    using UncertainPairedData = hecfda::model::paired_data::UncertainPairedData;
    using PairedData = hecfda::model::paired_data::PairedData;
    using ErrorLevel = hecfda::statistics::ErrorLevel;

    class OccupancyTypeBuilder;

    // Move-only: see class comment above.
    OccupancyType(OccupancyType&&) = default;
    OccupancyType& operator=(OccupancyType&&) = default;
    OccupancyType(const OccupancyType&) = delete;
    OccupancyType& operator=(const OccupancyType&) = delete;

    // ported from: OccupancyType.cs static OccupancyTypeBuilder Builder().
    static OccupancyTypeBuilder builder();

    const std::string& name() const { return name_; }
    const std::string& damage_category() const { return damage_category_; }
    bool use_content_to_structure_value_ratio() const { return use_content_to_structure_value_ratio_; }
    bool use_other_to_structure_value_ratio() const { return use_other_to_structure_value_ratio_; }
    bool compute_content_damage() const { return compute_content_damage_; }
    bool compute_vehicle_damage() const { return compute_vehicle_damage_; }
    bool compute_other_damage() const { return compute_other_damage_; }

    // ported from: OccupancyType.cs public void GenerateRandomNumbers(long size). Conditioned on
    // ComputeContentDamage/UseContentToStructureValueRatio/ComputeOtherDamage/
    // UseOtherToStructureValueRatio/ComputeVehicleDamage exactly as the C# method is.
    void generate_random_numbers(long size) {
        structure_depth_percent_damage_function_.generate_random_numbers(kDepthDamageSeed, size);
        structure_value_error_.generate_random_numbers(kStructureValueSeed, size);
        first_floor_elevation_error_.generate_random_numbers(kFirstFloorSeed, size);
        if (compute_content_damage_) {
            content_depth_percent_damage_function_.generate_random_numbers(kDepthDamageSeed, size);
            if (use_content_to_structure_value_ratio_) {
                content_to_structure_value_ratio_.generate_random_numbers(kContentValueSeed, size);
            } else {
                content_value_error_.generate_random_numbers(kContentValueSeed, size);
            }
        }
        if (compute_other_damage_) {
            other_depth_percent_damage_function_.generate_random_numbers(kDepthDamageSeed, size);
            if (use_other_to_structure_value_ratio_) {
                other_to_structure_value_ratio_.generate_random_numbers(kOtherValueSeed, size);
            } else {
                other_value_error_.generate_random_numbers(kOtherValueSeed, size);
            }
        }
        if (compute_vehicle_damage_) {
            vehicle_depth_percent_damage_function_.generate_random_numbers(kDepthDamageSeed, size);
            vehicle_value_error_.generate_random_numbers(kVehicleValueSeed, size);
        }
    }

    // ported from: OccupancyType.cs public DeterministicOccupancyType Sample(double probability).
    DeterministicOccupancyType sample(double probability) const {
        throw_if_major_error();
        PairedData struct_damage_paired_data = structure_depth_percent_damage_function_.sample_paired_data(probability);
        // "This hack is here because we need to create these functions before assigning their
        // value" -- transcribed verbatim (see class comment in uncertain_paired_data.hpp for the
        // sibling hack this mirrors).
        PairedData content_damage_paired_data({0}, {0});
        PairedData vehicle_damage_paired_data({0}, {0});
        PairedData other_damage_paired_data({0}, {0});

        double first_floor_elevation_offset_sampled = first_floor_elevation_error_.sample(probability);
        double structure_value_offset_sampled = structure_value_error_.sample(probability);
        double content_value_offset_sampled = 0;
        double content_value_ratio_sampled = 0;
        if (compute_content_damage_) {
            if (use_content_to_structure_value_ratio_) {
                content_value_ratio_sampled = content_to_structure_value_ratio_.sample(probability);
            } else {
                content_value_offset_sampled = content_value_error_.sample(probability);
            }
            content_damage_paired_data = content_depth_percent_damage_function_.sample_paired_data(probability);
        }
        double other_value_offset_sampled = 0;
        double other_value_ratio_sampled = 0;
        if (compute_other_damage_) {
            if (use_other_to_structure_value_ratio_) {
                other_value_ratio_sampled = other_to_structure_value_ratio_.sample(probability) / 100;
            } else {
                other_value_offset_sampled = other_value_error_.sample(probability);
            }
            other_damage_paired_data = other_depth_percent_damage_function_.sample_paired_data(probability);
        }

        double vehicle_value_offset_sampled = 0;
        if (compute_vehicle_damage_) {
            vehicle_value_offset_sampled = vehicle_value_error_.sample(probability);
            vehicle_damage_paired_data = vehicle_depth_percent_damage_function_.sample_paired_data(probability);
        }
        return DeterministicOccupancyType(
            name_, damage_category_, std::move(struct_damage_paired_data),
            first_floor_elevation_offset_sampled, structure_value_offset_sampled, compute_content_damage_,
            compute_vehicle_damage_, compute_other_damage_, std::move(content_damage_paired_data),
            content_value_offset_sampled, use_content_to_structure_value_ratio_, content_value_ratio_sampled,
            std::move(vehicle_damage_paired_data), vehicle_value_offset_sampled,
            std::move(other_damage_paired_data), other_value_offset_sampled,
            use_other_to_structure_value_ratio_, other_value_ratio_sampled, structure_value_is_log_normal_,
            content_value_is_log_normal_, other_value_is_log_normal_, vehicle_value_is_log_normal_,
            first_floor_elevation_is_log_normal_);
    }

    // ported from: OccupancyType.cs public DeterministicOccupancyType Sample(long iteration, bool
    // computeIsDeterministic = false).
    //
    // All sampling methods include a computeIsDeterministic argument that bypasses the iteration
    // number for the retrieval of the deterministic representation of the variable.
    DeterministicOccupancyType sample(long iteration, bool compute_is_deterministic = false) const {
        throw_if_major_error();
        PairedData struct_damage_paired_data =
            structure_depth_percent_damage_function_.sample_paired_data(iteration, compute_is_deterministic);
        PairedData content_damage_paired_data({0}, {0});
        PairedData vehicle_damage_paired_data({0}, {0});
        PairedData other_damage_paired_data({0}, {0});

        double first_floor_elevation_offset_sampled =
            first_floor_elevation_error_.sample(iteration, compute_is_deterministic);
        double structure_value_offset_sampled = structure_value_error_.sample(iteration, compute_is_deterministic);
        double content_value_offset_sampled = 0;
        double content_value_ratio_sampled = 0;
        if (compute_content_damage_) {
            if (use_content_to_structure_value_ratio_) {
                content_value_ratio_sampled =
                    content_to_structure_value_ratio_.sample(iteration, compute_is_deterministic);
            } else {
                content_value_offset_sampled = content_value_error_.sample(iteration, compute_is_deterministic);
            }
            content_damage_paired_data =
                content_depth_percent_damage_function_.sample_paired_data(iteration, compute_is_deterministic);
        }
        double other_value_offset_sampled = 0;
        double other_value_ratio_sampled = 0;
        if (compute_other_damage_) {
            if (use_other_to_structure_value_ratio_) {
                other_value_ratio_sampled =
                    other_to_structure_value_ratio_.sample(iteration, compute_is_deterministic) / 100;
            } else {
                other_value_offset_sampled = other_value_error_.sample(iteration, compute_is_deterministic);
            }
            other_damage_paired_data =
                other_depth_percent_damage_function_.sample_paired_data(iteration, compute_is_deterministic);
        }

        double vehicle_value_offset_sampled = 0;
        if (compute_vehicle_damage_) {
            vehicle_value_offset_sampled = vehicle_value_error_.sample(iteration, compute_is_deterministic);
            vehicle_damage_paired_data =
                vehicle_depth_percent_damage_function_.sample_paired_data(iteration, compute_is_deterministic);
        }
        return DeterministicOccupancyType(
            name_, damage_category_, std::move(struct_damage_paired_data),
            first_floor_elevation_offset_sampled, structure_value_offset_sampled, compute_content_damage_,
            compute_vehicle_damage_, compute_other_damage_, std::move(content_damage_paired_data),
            content_value_offset_sampled, use_content_to_structure_value_ratio_, content_value_ratio_sampled,
            std::move(vehicle_damage_paired_data), vehicle_value_offset_sampled,
            std::move(other_damage_paired_data), other_value_offset_sampled,
            use_other_to_structure_value_ratio_, other_value_ratio_sampled, structure_value_is_log_normal_,
            content_value_is_log_normal_, other_value_is_log_normal_, vehicle_value_is_log_normal_,
            first_floor_elevation_is_log_normal_);
    }

    // ported from: OccupancyType.cs public void Validate() (dispatches through
    // PropertyValidationHelper.ValidateProperty -- folded into validate_property() below; see the
    // UncertainPairedData severance note in the class comment for why the four depth-percent-
    // damage-function ValidateProperty(...) calls are omitted).
    void validate() {
        has_errors_ = false;
        error_level_ = ErrorLevel::Unassigned;

        validate_property(first_floor_elevation_error_);
        validate_property(structure_value_error_);
        if (compute_content_damage_) {
            if (use_content_to_structure_value_ratio_) {
                validate_property(content_to_structure_value_ratio_);
            } else {
                validate_property(content_value_error_);
            }
        }
        if (compute_other_damage_) {
            if (use_other_to_structure_value_ratio_) {
                validate_property(other_to_structure_value_ratio_);
            } else {
                validate_property(other_value_error_);
            }
        }
        if (compute_vehicle_damage_) {
            validate_property(vehicle_value_error_);
        }
    }

    bool has_errors() const { return has_errors_; }
    ErrorLevel error_level() const { return error_level_; }

    // ported from: OccupancyType.cs public string GetErrorsFromProperties(). See the class comment
    // for the UncertainPairedData severance (the four depth-percent-damage-function
    // .GetErrorMessages(...) calls are omitted) and the faithful `ComputeOtherDamage` block bug
    // (checks UseContentToStructureValueRatio, not UseOtherToStructureValueRatio).
    std::string get_errors_from_properties() {
        std::string identifying_string = "Occupancy Type " + name_ + " ";
        std::string errors;
        errors += error_messages_for(structure_value_error_, identifying_string + "_StructureValueError");
        errors += error_messages_for(first_floor_elevation_error_, identifying_string + "_FirstFloorElevationError");
        if (compute_content_damage_) {
            if (use_content_to_structure_value_ratio_) {
                errors += error_messages_for(content_to_structure_value_ratio_,
                                              identifying_string + "_ContentToStructureValueRatio");
            } else {
                errors += error_messages_for(content_value_error_, identifying_string + "_ContentValueError");
            }
        }
        if (compute_other_damage_) {
            // FAITHFUL BUG (see class comment): checks use_content_to_structure_value_ratio_, not
            // use_other_to_structure_value_ratio_, exactly as the C# source does.
            if (use_content_to_structure_value_ratio_) {
                errors += error_messages_for(other_to_structure_value_ratio_,
                                              identifying_string + "_OtherToStructureValueRatio");
            } else {
                errors += error_messages_for(other_value_error_, identifying_string + "_OtherValueError");
            }
        }
        if (compute_vehicle_damage_) {
            errors += error_messages_for(vehicle_value_error_, identifying_string + "_VehicleValueError");
        }
        return errors;
    }

   private:
    // ported from: OccupancyType.cs private const int ... _SEED fields.
    static constexpr int kDepthDamageSeed = 1234;
    static constexpr int kFirstFloorSeed = 2345;
    static constexpr int kStructureValueSeed = 3456;
    static constexpr int kContentValueSeed = 4567;
    static constexpr int kOtherValueSeed = 5678;
    static constexpr int kVehicleValueSeed = 6789;

    // ported from: OccupancyType.cs private OccupancyType() -- the parameterless ctor, only
    // reachable via builder(). Every UncertainPairedData member is default-constructed with empty
    // xs/ys (C++'s UncertainPairedData has no parameterless ctor, unlike C#'s `new
    // UncertainPairedData()` -- see uncertain_paired_data.hpp; `{}, {}` is the equivalent empty
    // curve).
    OccupancyType()
        : structure_depth_percent_damage_function_({}, {}),
          content_depth_percent_damage_function_({}, {}),
          vehicle_depth_percent_damage_function_({}, {}),
          other_depth_percent_damage_function_({}, {}) {}

    // ported from: OccupancyType.cs private void ReportMessage(...) -- SEVERED (see class
    // comment); throws if ErrorLevel >= Major, mirroring both Sample() overloads' identical guard.
    void throw_if_major_error() const {
        if (error_level_ >= ErrorLevel::Major) {
            throw std::runtime_error("Occupancy type " + name_ +
                                      " has at least one major error and cannot be sampled.");
        }
    }

    // ported from: PropertyValidationHelper.ValidateProperty(Validation) + ResetErrors(Validation)
    // (folded together here, matching the C# call sequence exactly: Validate() the child, then --
    // if it now HasErrors -- set HasErrors=true and raise ErrorLevel to the child's ErrorLevel if
    // higher, i.e. the MAX ErrorLevel across all validated children, not the "last failing
    // property wins" semantics hecfda::statistics::Validation::validate() itself uses internally).
    void validate_property(hecfda::statistics::Validation& validation_error_logger) {
        validation_error_logger.validate();
        if (validation_error_logger.has_errors()) {
            has_errors_ = true;
            if (error_level_ < validation_error_logger.error_level()) {
                error_level_ = validation_error_logger.error_level();
            }
        }
    }

    // ported from: Validation.GetErrorMessages(ErrorLevel errorLevelSeverityThreshold, string
    // objName), called here with errorLevelSeverityThreshold always Unassigned (0) -- the same
    // constant OccupancyType.GetErrorsFromProperties() always passes in C#
    // (`minimumLevelToCheckForErrors = ErrorLevel.Unassigned`) -- so the per-message
    // `err.ErrorLevel >= errorLevelSeverityThreshold` filter never excludes anything and is
    // omitted (hecfda::statistics::Validation::errors() doesn't retain a per-message ErrorLevel to
    // filter by in the first place). Not exercised by any fixture; the numeric ErrorLevel is
    // formatted directly since Validation has no name-string mapping for it.
    static std::string error_messages_for(hecfda::statistics::Validation& validation_error_logger,
                                           const std::string& obj_name) {
        validation_error_logger.validate();
        auto messages = validation_error_logger.errors();
        if (messages.empty()) return "";
        std::string out;
        for (const auto& message : messages) {
            out += obj_name + " Error Level: " +
                   std::to_string(static_cast<int>(validation_error_logger.error_level())) + " | " + message + "\n";
        }
        return out;
    }

    std::string name_;
    std::string damage_category_;

    UncertainPairedData structure_depth_percent_damage_function_;
    UncertainPairedData content_depth_percent_damage_function_;
    UncertainPairedData vehicle_depth_percent_damage_function_;
    UncertainPairedData other_depth_percent_damage_function_;

    FirstFloorElevationUncertainty first_floor_elevation_error_;
    bool first_floor_elevation_is_log_normal_ = false;
    ValueUncertainty structure_value_error_;
    bool structure_value_is_log_normal_ = false;
    ValueUncertainty content_value_error_;
    bool content_value_is_log_normal_ = false;
    ValueUncertainty vehicle_value_error_;
    bool vehicle_value_is_log_normal_ = false;
    ValueUncertainty other_value_error_;
    bool other_value_is_log_normal_ = false;
    ValueRatioWithUncertainty content_to_structure_value_ratio_;
    ValueRatioWithUncertainty other_to_structure_value_ratio_;

    bool use_content_to_structure_value_ratio_ = false;
    bool use_other_to_structure_value_ratio_ = false;
    bool compute_content_damage_ = false;
    bool compute_vehicle_damage_ = false;
    bool compute_other_damage_ = false;

    // ported from: PropertyValidationHelper.HasErrors / .ErrorLevel.
    bool has_errors_ = false;
    ErrorLevel error_level_ = ErrorLevel::Unassigned;
};

// ported from: OccupancyType.cs public class OccupancyTypeBuilder (nested). Holds a single
// OccupancyType by value and moves it through the chain -- see the MOVE-ONLY note in
// OccupancyType's class comment for why every `with_*` here returns `OccupancyTypeBuilder&&`
// rather than constructing a fresh builder the way the C# reference-semantics version does.
class OccupancyType::OccupancyTypeBuilder {
   public:
    // ported from: OccupancyTypeBuilder(OccupancyType occupancyType) -- `internal` in C#, exposed
    // here since this header-only port has no assembly boundary to enforce that visibility (same
    // rationale as e.g. ValueUncertainty::generate_random_numbers).
    explicit OccupancyTypeBuilder(OccupancyType occupancy_type) : occupancy_type_(std::move(occupancy_type)) {}

    // ported from: OccupancyTypeBuilder.Build().
    OccupancyType build() { return std::move(occupancy_type_); }

    OccupancyTypeBuilder&& with_damage_category(std::string damage_category) {
        occupancy_type_.damage_category_ = std::move(damage_category);
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_name(std::string name) {
        occupancy_type_.name_ = std::move(name);
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_structure_depth_percent_damage(OccupancyType::UncertainPairedData structure_depth_percent_damage) {
        occupancy_type_.structure_depth_percent_damage_function_ = std::move(structure_depth_percent_damage);
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_content_depth_percent_damage(OccupancyType::UncertainPairedData content_depth_percent_damage) {
        occupancy_type_.content_depth_percent_damage_function_ = std::move(content_depth_percent_damage);
        occupancy_type_.compute_content_damage_ = true;
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_vehicle_depth_percent_damage(OccupancyType::UncertainPairedData vehicle_depth_percent_damage) {
        occupancy_type_.vehicle_depth_percent_damage_function_ = std::move(vehicle_depth_percent_damage);
        occupancy_type_.compute_vehicle_damage_ = true;
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_other_depth_percent_damage(OccupancyType::UncertainPairedData other_depth_percent_damage) {
        occupancy_type_.other_depth_percent_damage_function_ = std::move(other_depth_percent_damage);
        occupancy_type_.compute_other_damage_ = true;
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_structure_value_uncertainty(ValueUncertainty value_uncertainty) {
        if (value_uncertainty.distribution_type() == ValueUncertainty::DistributionType::LogNormal) {
            occupancy_type_.structure_value_is_log_normal_ = true;
        }
        occupancy_type_.structure_value_error_ = std::move(value_uncertainty);
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_content_value_uncertainty(ValueUncertainty value_uncertainty) {
        if (value_uncertainty.distribution_type() == ValueUncertainty::DistributionType::LogNormal) {
            occupancy_type_.content_value_is_log_normal_ = true;
        }
        occupancy_type_.content_value_error_ = std::move(value_uncertainty);
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_vehicle_value_uncertainty(ValueUncertainty value_uncertainty) {
        if (value_uncertainty.distribution_type() == ValueUncertainty::DistributionType::LogNormal) {
            occupancy_type_.vehicle_value_is_log_normal_ = true;
        }
        occupancy_type_.vehicle_value_error_ = std::move(value_uncertainty);
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_other_value_uncertainty(ValueUncertainty value_uncertainty) {
        if (value_uncertainty.distribution_type() == ValueUncertainty::DistributionType::LogNormal) {
            occupancy_type_.other_value_is_log_normal_ = true;
        }
        occupancy_type_.other_value_error_ = std::move(value_uncertainty);
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_content_to_structure_value_ratio(ValueRatioWithUncertainty value_ratio_with_uncertainty) {
        occupancy_type_.content_to_structure_value_ratio_ = std::move(value_ratio_with_uncertainty);
        occupancy_type_.use_content_to_structure_value_ratio_ = true;
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_other_to_structure_value_ratio(ValueRatioWithUncertainty value_ratio_with_uncertainty) {
        occupancy_type_.other_to_structure_value_ratio_ = std::move(value_ratio_with_uncertainty);
        occupancy_type_.use_other_to_structure_value_ratio_ = true;
        return std::move(*this);
    }
    OccupancyTypeBuilder&& with_first_floor_elevation_uncertainty(FirstFloorElevationUncertainty first_floor_elevation_uncertainty) {
        if (first_floor_elevation_uncertainty.distribution_type() ==
            FirstFloorElevationUncertainty::DistributionType::LogNormal) {
            occupancy_type_.first_floor_elevation_is_log_normal_ = true;
        }
        occupancy_type_.first_floor_elevation_error_ = std::move(first_floor_elevation_uncertainty);
        return std::move(*this);
    }

   private:
    OccupancyType occupancy_type_;
};

inline OccupancyType::OccupancyTypeBuilder OccupancyType::builder() {
    return OccupancyTypeBuilder(OccupancyType());
}

}  // namespace structures
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_STRUCTURES_OCCUPANCY_TYPE_HPP
