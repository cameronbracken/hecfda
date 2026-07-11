// ported from: upstream/HEC-FDA/HEC.FDA.Model/structures/ValueUncertainty.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_STRUCTURES_VALUE_UNCERTAINTY_HPP
#define HECFDA_MODEL_STRUCTURES_VALUE_UNCERTAINTY_HPP
#include <cmath>
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

// ported from: ValueUncertainty.cs. C# is `class ValueUncertainty : ValidationErrorLogger`;
// ValidationErrorLogger : Validation is a thin WPF-messaging layer with nothing this port needs
// (same rationale already applied to ContinuousDistribution -- see continuous_distribution.hpp),
// so this inherits hecfda::statistics::Validation directly.
//
// One per-structure uncertainty sampler (of three) that a later task's OccupancyType will hold.
// `Sample` returns a raw "value offset": for Normal/Triangular/Uniform it is a percent-of-value
// multiplier centered on 1 (100%); for LogNormal it is a factor the caller must combine
// differently (see the doc comment on Sample below, transcribed from C#).
//
// Faithful upstream quirks (deliberately reproduced, do NOT "fix"):
//  - AddRules() validates distribution_type is one of {Normal, Uniform, Deterministic,
//    Triangular} -- LogNormal is NOT in that allow-list, even though Sample()'s switch has a
//    live LogNormal case. A LogNormal ValueUncertainty is thus "invalid" per validate() but
//    still functionally sampleable; transcribed exactly as upstream wrote it, not reconciled.
//  - The rule message "The percent of inventory value must be greaeter than or equal to zero."
//    misspells "greater" as "greaeter" in the real C# string; kept verbatim.
//  - Property-rule names below are the literal C# `nameof(_DistributionType)` /
//    `nameof(_PercentOfInventoryValueStandardDeviationOrMin)` /
//    `nameof(_PercentOfInventoryValueMax)` field names (there are no public property wrappers
//    for the latter two), not PascalCase property names.
//  - Sample(double) and Sample(long, bool) each independently clamp a negative sampled offset to
//    0 at the end (no shared helper in C#); mirrored as two separate clamps here rather than
//    factored into one, to keep the two methods line-for-line diffable against the C# source.
class ValueUncertainty : public hecfda::statistics::Validation {
   public:
    using DistributionType = hecfda::statistics::distributions::DistributionType;

    // ported from: ValueUncertainty.cs ValueUncertainty() -- default is Deterministic, min/std 0,
    // max 100.
    ValueUncertainty()
        : distribution_type_(DistributionType::Deterministic),
          std_or_min_(0),
          max_(100) {
        add_rules();
    }

    // ported from: ValueUncertainty.cs ValueUncertainty(IDistributionEnum, double, double = 100).
    ValueUncertainty(DistributionType distribution_type, double std_or_min, double max = 100)
        : distribution_type_(distribution_type), std_or_min_(std_or_min), max_(max) {
        add_rules();
    }

    // ported from: ValueUncertainty.cs DistributionType getter.
    DistributionType distribution_type() const { return distribution_type_; }

    // ported from: ValueUncertainty.cs internal void GenerateRandomNumbers(int seed, long size).
    // C#: `Random random = new Random(seed); for i: randos[i] = random.NextDouble();`. Delegated
    // to RandomProvider(seed).next_random_sequence(size), the same pattern
    // UncertainPairedData::generate_random_numbers uses (verified elsewhere in this port to
    // reproduce `new Random(seed).NextDouble()`'s seeded stream call-for-call). C#'s method is
    // `internal`; exposed as public here since nothing in this header-only port has an assembly
    // boundary to enforce that visibility.
    void generate_random_numbers(int seed, long size) {
        random_numbers_ = hecfda::model::compute::RandomProvider(seed).next_random_sequence(size);
    }

    // ported from: ValueUncertainty.cs public double Sample(double probability).
    //
    // The use of this method will depend on the type of distribution.
    // If Normal, Triangular, or Uniform, the value returned is the percent of inventory value to
    // add or subtract from the inventoried value.
    // If log normal, then the return value will need to be multiplied by the inventoried value.
    double sample(double probability) const {
        double center_of_distribution = 100;
        double sampled_value_offset;
        switch (distribution_type_) {
            case DistributionType::Normal: {
                hecfda::statistics::distributions::Normal normal(center_of_distribution / 100,
                                                                   (std_or_min_ / 100));
                sampled_value_offset = normal.inverse_cdf(probability);
                break;
            }
            case DistributionType::LogNormal:
                sampled_value_offset = std::exp(
                    hecfda::statistics::distributions::Normal::standard_normal_inverse_cdf(
                        probability) *
                    (std_or_min_ / 100));
                break;
            case DistributionType::Triangular: {
                hecfda::statistics::distributions::Triangular triangular(
                    std_or_min_ / 100, center_of_distribution / 100, max_ / 100);
                sampled_value_offset = triangular.inverse_cdf(probability);
                break;
            }
            case DistributionType::Uniform: {
                hecfda::statistics::distributions::Uniform uniform(std_or_min_ / 100, max_ / 100);
                sampled_value_offset = uniform.inverse_cdf(probability);
                break;
            }
            default:
                sampled_value_offset = center_of_distribution / 100;
                break;
        }
        if (sampled_value_offset < 0) {
            sampled_value_offset = 0;
        }
        return sampled_value_offset;
    }

    // ported from: ValueUncertainty.cs public double Sample(long iteration, bool
    // computeIsDeterministic).
    //
    // All sampling methods include a computeIsDeterministic argument that bypasses the iteration
    // number for the retrieval of the deterministic representation of the variable.
    double sample(long iteration, bool compute_is_deterministic) const {
        double center_of_distribution = 100;
        double sampled_value_offset;
        if (compute_is_deterministic) {
            sampled_value_offset = center_of_distribution / 100;
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
                    hecfda::statistics::distributions::Normal normal(center_of_distribution / 100,
                                                                       (std_or_min_ / 100));
                    sampled_value_offset =
                        normal.inverse_cdf(random_numbers_[static_cast<std::size_t>(iteration)]);
                    break;
                }
                case DistributionType::LogNormal:
                    sampled_value_offset = std::exp(
                        hecfda::statistics::distributions::Normal::standard_normal_inverse_cdf(
                            random_numbers_[static_cast<std::size_t>(iteration)]) *
                        (std_or_min_ / 100));
                    break;
                case DistributionType::Triangular: {
                    hecfda::statistics::distributions::Triangular triangular(
                        std_or_min_ / 100, center_of_distribution / 100, max_ / 100);
                    sampled_value_offset =
                        triangular.inverse_cdf(random_numbers_[static_cast<std::size_t>(iteration)]);
                    break;
                }
                case DistributionType::Uniform: {
                    hecfda::statistics::distributions::Uniform uniform(std_or_min_ / 100,
                                                                         max_ / 100);
                    sampled_value_offset =
                        uniform.inverse_cdf(random_numbers_[static_cast<std::size_t>(iteration)]);
                    break;
                }
                default:
                    sampled_value_offset = center_of_distribution / 100;
                    break;
            }
        }
        if (sampled_value_offset < 0) {
            sampled_value_offset = 0;
        }
        return sampled_value_offset;
    }

   private:
    // ported from: ValueUncertainty.cs private void AddRules(). Field-name strings match the C#
    // `nameof(_DistributionType)` / `nameof(_PercentOfInventoryValueStandardDeviationOrMin)` /
    // `nameof(_PercentOfInventoryValueMax)` literals exactly, including the "greaeter" typo in
    // the second rule's message (see class comment).
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
            "value uncertainty",
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "_PercentOfInventoryValueStandardDeviationOrMin",
            [this]() { return std_or_min_ >= 0; },
            "The percent of inventory value must be greaeter than or equal to zero.",
            hecfda::statistics::ErrorLevel::Fatal);
        add_single_property_rule(
            "_PercentOfInventoryValueMax", [this]() { return max_ >= 100; },
            "The max percent of the inventory value must be greater than or equal to 100",
            hecfda::statistics::ErrorLevel::Fatal);
    }

    DistributionType distribution_type_;
    double std_or_min_;
    double max_;
    std::vector<double> random_numbers_;
};

}  // namespace structures
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_STRUCTURES_VALUE_UNCERTAINTY_HPP
