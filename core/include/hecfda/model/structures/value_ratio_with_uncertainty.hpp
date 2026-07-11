// ported from: upstream/HEC-FDA/HEC.FDA.Model/structures/ValueRatioWithUncertainty.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_STRUCTURES_VALUE_RATIO_WITH_UNCERTAINTY_HPP
#define HECFDA_MODEL_STRUCTURES_VALUE_RATIO_WITH_UNCERTAINTY_HPP
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/statistics/distributions/i_distribution_enum.hpp"
#include "hecfda/statistics/distributions/lognormal.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/statistics/distributions/triangular.hpp"
#include "hecfda/statistics/distributions/uncertain_to_deterministic_converter.hpp"
#include "hecfda/statistics/distributions/uniform.hpp"
#include "hecfda/statistics/validation.hpp"
namespace hecfda {
namespace model {
namespace structures {

// ported from: ValueRatioWithUncertainty.cs. C# is `class ValueRatioWithUncertainty : Validation`
// (unlike ValueUncertainty's ValidationErrorLogger base -- see value_uncertainty.hpp -- this class
// extends Validation directly), so this inherits hecfda::statistics::Validation directly too.
//
// Second of three per-structure uncertainty samplers (of three) that a later task's OccupancyType
// will hold: content-to-structure / other-to-structure VALUE RATIOS. Unlike ValueUncertainty,
// Sample() returns the ratio itself with NO /100 scaling anywhere in the C# source; transcribed
// as-is.
//
// Faithful upstream quirks (deliberately reproduced, do NOT "fix"):
//  - The `(double deterministicValueRatio)` convenience ctor sets `_Max = 0` (not
//    double.MaxValue like the other two ctors) -- irrelevant in practice because
//    MaxGreaterThanCentral() always returns true for Deterministic (bypasses the _Max vs.
//    _CentralTendency comparison), but transcribed exactly as upstream wrote it.
//  - AddRules registers all four checks unconditionally, including ones that are semantically
//    moot for Deterministic (MaxGreaterThanCentral bypasses via an explicit distribution-type
//    check; MinLessThanCentral bypasses for everything except Triangular). Both bypass rules are
//    still keyed to the "_Max"/"_StandardDeviationOrMin" property-rule names respectively, exactly
//    as the C# `nameof(_Max)` / `nameof(_StandardDeviationOrMin)` literals do -- there is no
//    nameof(_CentralTendency) or nameof(_DistributionType) rule even though those predicates read
//    those fields too.
//  - Sample(double) and Sample(long, bool) each independently clamp a negative sampled ratio to 0
//    at the end (no shared helper in C#), mirrored as two separate clamps to stay line-for-line
//    diffable against the C# source (same rationale as ValueUncertainty).
class ValueRatioWithUncertainty : public hecfda::statistics::Validation {
   public:
    using DistributionType = hecfda::statistics::distributions::DistributionType;

    // ported from: ValueRatioWithUncertainty.cs ValueRatioWithUncertainty() -- default is
    // Deterministic, std/min 0, central 0, max double.MaxValue.
    ValueRatioWithUncertainty()
        : std_or_min_(0),
          central_tendency_(0),
          max_(std::numeric_limits<double>::max()),
          distribution_type_(DistributionType::Deterministic) {
        add_rules();
    }

    // ported from: ValueRatioWithUncertainty.cs ValueRatioWithUncertainty(IDistributionEnum,
    // double, double, double = double.MaxValue).
    ValueRatioWithUncertainty(DistributionType distribution_type, double std_or_min,
                               double central_tendency,
                               double max = std::numeric_limits<double>::max())
        : std_or_min_(std_or_min),
          central_tendency_(central_tendency),
          max_(max),
          distribution_type_(distribution_type) {
        add_rules();
    }

    // ported from: ValueRatioWithUncertainty.cs ValueRatioWithUncertainty(double
    // deterministicValueRatio) -- always Deterministic, std/min 0, max 0 (see class comment).
    explicit ValueRatioWithUncertainty(double deterministic_value_ratio)
        : std_or_min_(0),
          central_tendency_(deterministic_value_ratio),
          max_(0),
          distribution_type_(DistributionType::Deterministic) {
        add_rules();
    }

    // ported from: ValueRatioWithUncertainty.cs (no explicit getter, but distribution type is
    // read by callers of this port the same way ValueUncertainty::distribution_type() is).
    DistributionType distribution_type() const { return distribution_type_; }

    // ported from: ValueRatioWithUncertainty.cs internal void GenerateRandomNumbers(int seed,
    // long size). Same pattern as ValueUncertainty::generate_random_numbers.
    void generate_random_numbers(int seed, long size) {
        random_numbers_ = hecfda::model::compute::RandomProvider(seed).next_random_sequence(size);
    }

    // ported from: ValueRatioWithUncertainty.cs public double Sample(double probability).
    double sample(double probability) const {
        double sampled_value_ratio;
        switch (distribution_type_) {
            case DistributionType::Normal: {
                hecfda::statistics::distributions::Normal normal(central_tendency_, std_or_min_);
                sampled_value_ratio = normal.inverse_cdf(probability);
                break;
            }
            case DistributionType::LogNormal: {
                hecfda::statistics::distributions::LogNormal log_normal(central_tendency_,
                                                                          std_or_min_);
                sampled_value_ratio = log_normal.inverse_cdf(probability);
                break;
            }
            case DistributionType::Triangular: {
                hecfda::statistics::distributions::Triangular triangular(std_or_min_,
                                                                           central_tendency_, max_);
                sampled_value_ratio = triangular.inverse_cdf(probability);
                break;
            }
            case DistributionType::Uniform: {
                hecfda::statistics::distributions::Uniform uniform(std_or_min_, max_);
                sampled_value_ratio = uniform.inverse_cdf(probability);
                break;
            }
            default:
                sampled_value_ratio = central_tendency_;
                break;
        }
        // do not allow for negative value ratios
        if (sampled_value_ratio < 0) {
            sampled_value_ratio = 0;
        }
        return sampled_value_ratio;
    }

    // ported from: ValueRatioWithUncertainty.cs public double Sample(long iteration, bool
    // computeIsDeterministic).
    //
    // All sampling methods include a computeIsDeterministic argument that bypasses the iteration
    // number for the retrieval of the deterministic representation of the variable.
    double sample(long iteration, bool compute_is_deterministic) const {
        double sampled_value_ratio;
        if (compute_is_deterministic) {
            switch (distribution_type_) {
                case DistributionType::LogNormal: {
                    hecfda::statistics::distributions::LogNormal log_normal(central_tendency_,
                                                                              std_or_min_);
                    hecfda::statistics::distributions::Deterministic deterministic_log_normal =
                        hecfda::statistics::distributions::convert_distribution_to_deterministic(
                            log_normal);
                    sampled_value_ratio = deterministic_log_normal.inverse_cdf(0.5);
                    break;
                }
                case DistributionType::Uniform: {
                    hecfda::statistics::distributions::Uniform uniform(std_or_min_, max_);
                    hecfda::statistics::distributions::Deterministic deterministic_uniform =
                        hecfda::statistics::distributions::convert_distribution_to_deterministic(
                            uniform);
                    sampled_value_ratio = deterministic_uniform.inverse_cdf(0.5);
                    break;
                }
                default:
                    sampled_value_ratio = central_tendency_;
                    break;
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
                    hecfda::statistics::distributions::Normal normal(central_tendency_,
                                                                       std_or_min_);
                    sampled_value_ratio =
                        normal.inverse_cdf(random_numbers_[static_cast<std::size_t>(iteration)]);
                    break;
                }
                case DistributionType::LogNormal: {
                    hecfda::statistics::distributions::LogNormal log_normal(central_tendency_,
                                                                              std_or_min_);
                    sampled_value_ratio = log_normal.inverse_cdf(
                        random_numbers_[static_cast<std::size_t>(iteration)]);
                    break;
                }
                case DistributionType::Triangular: {
                    hecfda::statistics::distributions::Triangular triangular(
                        std_or_min_, central_tendency_, max_);
                    sampled_value_ratio = triangular.inverse_cdf(
                        random_numbers_[static_cast<std::size_t>(iteration)]);
                    break;
                }
                case DistributionType::Uniform: {
                    hecfda::statistics::distributions::Uniform uniform(std_or_min_, max_);
                    sampled_value_ratio = uniform.inverse_cdf(
                        random_numbers_[static_cast<std::size_t>(iteration)]);
                    break;
                }
                default:
                    sampled_value_ratio = central_tendency_;
                    break;
            }
        }
        // do not allow for negative value ratios
        if (sampled_value_ratio < 0) {
            sampled_value_ratio = 0;
        }
        return sampled_value_ratio;
    }

   private:
    // ported from: ValueRatioWithUncertainty.cs private void AddRules().
    void add_rules() {
        add_single_property_rule(
            "_StandardDeviationOrMin",
            [this]() { return std_or_min_ >= 0 && max_ >= 0 && central_tendency_ >= 0; },
            "Value ratio parameter values must be non-negative",
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "_Max", [this]() { return max_ >= std_or_min_; }, "The max must be larger than the minimum",
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "_Max", [this]() { return max_greater_than_central(); },
            "The max must be larger than the central tendency", hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "_StandardDeviationOrMin", [this]() { return min_less_than_central(); },
            "The min must be less than the central tendency", hecfda::statistics::ErrorLevel::Fatal);
    }

    // ported from: ValueRatioWithUncertainty.cs private bool MaxGreaterThanCentral().
    bool max_greater_than_central() const {
        if (distribution_type_ == DistributionType::Deterministic) {
            return true;  // because this doesn't matter for deterministic
        }
        return max_ >= central_tendency_;
    }

    // ported from: ValueRatioWithUncertainty.cs private bool MinLessThanCentral().
    bool min_less_than_central() const {
        if (distribution_type_ == DistributionType::Triangular) {
            return std_or_min_ <= central_tendency_;
        }
        return true;  // because this doesn't matter for deterministic or normal
    }

    double std_or_min_;
    double central_tendency_;
    double max_;
    DistributionType distribution_type_;
    std::vector<double> random_numbers_;
};

}  // namespace structures
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_STRUCTURES_VALUE_RATIO_WITH_UNCERTAINTY_HPP
