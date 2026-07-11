// ported from: upstream/HEC-FDA/HEC.FDA.Model/structures/FirstFloorElevationUncertainty.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_STRUCTURES_FIRST_FLOOR_ELEVATION_UNCERTAINTY_HPP
#define HECFDA_MODEL_STRUCTURES_FIRST_FLOOR_ELEVATION_UNCERTAINTY_HPP
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/statistics/distributions/i_distribution_enum.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/distributions/triangular.hpp"
#include "hecfda/statistics/distributions/uniform.hpp"
#include "hecfda/statistics/validation.hpp"
namespace hecfda {
namespace model {
namespace structures {

// ported from: FirstFloorElevationUncertainty.cs. C# is `class FirstFloorElevationUncertainty :
// Validation` (same lighter base as ValueRatioWithUncertainty, not ValueUncertainty's
// ValidationErrorLogger), so this inherits hecfda::statistics::Validation directly.
//
// Last of three per-structure uncertainty samplers that a later task's OccupancyType will hold.
// Unlike ValueUncertainty/ValueRatioWithUncertainty, Sample() returns a FEET OFFSET (Normal,
// Triangular, Uniform, Deterministic) or a MULTIPLICATIVE FACTOR (LogNormal) to combine with an
// inventoried first-floor elevation -- see the doc comment on sample() below, transcribed from
// C#. The center of the distribution is hardcoded to 0 (NOT 100/1 like the value samplers), and
// critically there is NO negative clamp: a first-floor-elevation offset may legitimately be
// negative (structure below the reference elevation).
//
// Faithful upstream quirks (deliberately reproduced, do NOT "fix"):
//  - LogNormal is sampled directly via
//    exp(Normal::standard_normal_inverse_cdf(p) * std_or_min_) rather than constructing a
//    LogNormal distribution object, and with NO /100 scaling anywhere -- transcribed exactly as
//    upstream wrote it.
//  - Triangular/Uniform use a NEGATIVE std_or_min_ as their lower bound
//    (Triangular(-std_or_min_, 0, max_), Uniform(-std_or_min_, max_)) -- offsets straddle 0.
//  - AddRules()'s distribution-allow-list rule message says "...can be used for value ratio
//    uncertainty" -- a copy-paste wording quirk inherited verbatim from
//    ValueRatioWithUncertainty's AddRules() (this class is FirstFloorElevationUncertainty, not a
//    value-ratio type); the C# string is transcribed as-is, not corrected.
//  - sample(long, bool)'s deterministic branch has no general "central tendency" fallback: only
//    LogNormal is special-cased (returns 1), every other distribution type -- including Normal
//    and Triangular, which ValueRatioWithUncertainty's analogous branch would route through the
//    UncertainToDeterministicDistributionConverter -- falls through to the hardcoded center (0).
//    C# has no converter call anywhere in this class; transcribed exactly as written.
class FirstFloorElevationUncertainty : public hecfda::statistics::Validation {
   public:
    using DistributionType = hecfda::statistics::distributions::DistributionType;

    // ported from: FirstFloorElevationUncertainty.cs FirstFloorElevationUncertainty() -- "used
    // for deterministic first floor elevations only": Deterministic, std/min 0, max 0 (NOT
    // double.MaxValue -- transcribed exactly as the C# default ctor, which differs from the
    // 3-arg ctor's default).
    FirstFloorElevationUncertainty()
        : std_or_min_(0), max_(0), distribution_type_(DistributionType::Deterministic) {
        add_rules();
    }

    // ported from: FirstFloorElevationUncertainty.cs FirstFloorElevationUncertainty(
    // IDistributionEnum distributionEnum, double standardDeviationOrMinimum,
    // double maximum = double.MaxValue).
    FirstFloorElevationUncertainty(DistributionType distribution_type, double std_or_min,
                                    double max = std::numeric_limits<double>::max())
        : std_or_min_(std_or_min), max_(max), distribution_type_(distribution_type) {
        add_rules();
    }

    // ported from: FirstFloorElevationUncertainty.cs DistributionType getter.
    DistributionType distribution_type() const { return distribution_type_; }

    // ported from: FirstFloorElevationUncertainty.cs internal void GenerateRandomNumbers(int
    // seed, long size). Same pattern as ValueUncertainty::generate_random_numbers /
    // ValueRatioWithUncertainty::generate_random_numbers.
    void generate_random_numbers(int seed, long size) {
        random_numbers_ = hecfda::model::compute::RandomProvider(seed).next_random_sequence(size);
    }

    // ported from: FirstFloorElevationUncertainty.cs public double Sample(double probability).
    //
    // The use of this method will depend on the type of distribution.
    // If Normal, Triangular, or Uniform, the value returned is the feet to add or subtract from
    // the inventoried value.
    // If log normal, then the return value will need to be multiplied by the inventoried value.
    double sample(double probability) const {
        double center_of_distribution = 0;
        double sampled_first_floor_elevation_offset;
        switch (distribution_type_) {
            case DistributionType::Normal: {
                hecfda::statistics::distributions::Normal normal(center_of_distribution,
                                                                    std_or_min_);
                sampled_first_floor_elevation_offset = normal.inverse_cdf(probability);
                break;
            }
            case DistributionType::LogNormal:
                sampled_first_floor_elevation_offset = std::exp(
                    hecfda::statistics::distributions::Normal::standard_normal_inverse_cdf(
                        probability) *
                    std_or_min_);
                break;
            case DistributionType::Triangular: {
                hecfda::statistics::distributions::Triangular triangular(
                    -std_or_min_, center_of_distribution, max_);
                sampled_first_floor_elevation_offset = triangular.inverse_cdf(probability);
                break;
            }
            case DistributionType::Uniform: {
                hecfda::statistics::distributions::Uniform uniform(-std_or_min_, max_);
                sampled_first_floor_elevation_offset = uniform.inverse_cdf(probability);
                break;
            }
            default:
                sampled_first_floor_elevation_offset = center_of_distribution;
                break;
        }
        // NOTE: unlike ValueUncertainty/ValueRatioWithUncertainty, there is no negative clamp
        // here -- a first-floor-elevation offset may legitimately be negative.
        return sampled_first_floor_elevation_offset;
    }

    // ported from: FirstFloorElevationUncertainty.cs public double Sample(long iteration, bool
    // computeIsDeterministic).
    //
    // The use of this method will depend on the type of distribution.
    // If Normal, Triangular, or Uniform, the value returned is the feet to add or subtract from
    // the inventoried value.
    // If log normal, then the return value will need to be multiplied by the inventoried value.
    // All sampling methods include a computeIsDeterministic argument that bypasses the iteration
    // number for the retrieval of the deterministic representation of the variable.
    double sample(long iteration, bool compute_is_deterministic) const {
        double center_of_distribution = 0;
        double sampled_first_floor_elevation_offset;
        if (compute_is_deterministic) {
            if (distribution_type_ == DistributionType::LogNormal) {
                sampled_first_floor_elevation_offset = 1;
            } else {
                sampled_first_floor_elevation_offset = center_of_distribution;
            }
        } else {
            if (random_numbers_.empty()) {
                throw std::runtime_error(
                    "Random numbers by iteration have not yet been generated for this compute but "
                    "the software attempted to sample value uncertainty by iteration.");
            }
            if (iteration < 0 || iteration >= static_cast<long>(random_numbers_.size())) {
                throw std::out_of_range(
                    "The iteration at which value uncertainty was attempted to be sampled is beyond "
                    "the quantity of random numbers sampled. There is a significant conflict between "
                    "the stage-damage convergence criteria and the quantity of iterations being "
                    "computed.");
            }
            switch (distribution_type_) {
                case DistributionType::Normal: {
                    hecfda::statistics::distributions::Normal normal(center_of_distribution,
                                                                        std_or_min_);
                    sampled_first_floor_elevation_offset =
                        normal.inverse_cdf(random_numbers_[static_cast<std::size_t>(iteration)]);
                    break;
                }
                case DistributionType::LogNormal:
                    sampled_first_floor_elevation_offset = std::exp(
                        hecfda::statistics::distributions::Normal::standard_normal_inverse_cdf(
                            random_numbers_[static_cast<std::size_t>(iteration)]) *
                        std_or_min_);
                    break;
                case DistributionType::Triangular: {
                    hecfda::statistics::distributions::Triangular triangular(
                        -std_or_min_, center_of_distribution, max_);
                    sampled_first_floor_elevation_offset = triangular.inverse_cdf(
                        random_numbers_[static_cast<std::size_t>(iteration)]);
                    break;
                }
                case DistributionType::Uniform: {
                    hecfda::statistics::distributions::Uniform uniform(-std_or_min_, max_);
                    sampled_first_floor_elevation_offset = uniform.inverse_cdf(
                        random_numbers_[static_cast<std::size_t>(iteration)]);
                    break;
                }
                default:
                    sampled_first_floor_elevation_offset = center_of_distribution;
                    break;
            }
        }
        // NOTE: unlike ValueUncertainty/ValueRatioWithUncertainty, there is no negative clamp
        // here -- a first-floor-elevation offset may legitimately be negative.
        return sampled_first_floor_elevation_offset;
    }

   private:
    // ported from: FirstFloorElevationUncertainty.cs private void AddRules(). Field-name strings
    // match the C# `nameof(_DistributionType)` /
    // `nameof(_StandardDeviationFromOrFeetBelowInventoryValue)` literals exactly. The first rule's
    // message says "...value ratio uncertainty" -- a copy-paste typo inherited from
    // ValueRatioWithUncertainty's AddRules(), preserved verbatim (see class comment).
    void add_rules() {
        add_single_property_rule(
            "_DistributionType",
            [this]() {
                return distribution_type_ == DistributionType::Normal ||
                       distribution_type_ == DistributionType::Uniform ||
                       distribution_type_ == DistributionType::Deterministic ||
                       distribution_type_ == DistributionType::Triangular;
            },
            "Only Deterministic, Normal, Triangular, and Uniform distributions can be used for "
            "value ratio uncertainty",
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "_StandardDeviationFromOrFeetBelowInventoryValue",
            [this]() { return std_or_min_ >= 0 && max_ >= 0; },
            "First floor elevation uncertainty parameters must be positive",
            hecfda::statistics::ErrorLevel::Fatal);
    }

    double std_or_min_;
    double max_;
    DistributionType distribution_type_;
    std::vector<double> random_numbers_;
};

}  // namespace structures
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_STRUCTURES_FIRST_FLOOR_ELEVATION_UNCERTAINTY_HPP
