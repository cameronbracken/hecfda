// ported from: HEC.FDA.Model/metrics/Threshold.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_THRESHOLD_HPP
#define HECFDA_MODEL_METRICS_THRESHOLD_HPP
#include <utility>
#include "hecfda/model/metrics/system_performance_results.hpp"
#include "hecfda/model/metrics/threshold_enum.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: Threshold.cs `public class Threshold`. The per-threshold container
// PerformanceByThresholds (below) holds one of: each carries the threshold's identity
// (ThresholdType/ThresholdValue/ThresholdID) plus its own SystemPerformanceResults (Phase 5 Task
// 2), fed per-iteration observations by the EAD compute loop (Phase 5 Tasks 9/10).
//
// Move-only: SystemPerformanceResults is itself move-only (its `_SystemResponseFunction` is
// std::optional<UncertainPairedData>, and UncertainPairedData holds unique_ptr<IDistribution> --
// see uncertain_paired_data.hpp / system_performance_results.hpp). A Threshold member of that type
// makes Threshold move-only too, matching the "fresh construction per use" convention already
// established for SystemPerformanceResults/UncertainPairedData.
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//  - The private `Threshold(int, ThresholdEnum, double, SystemPerformanceResults)` ctor: exists
//    solely to feed `ReadFromXML`'s reconstruction, dropped alongside it (repo-wide XML severance).
//  - `WriteToXML()`/`static ReadFromXML(XElement)` and their `GetXMLTagFromProperty`/
//    `ThresholdEnumFromString` reflection-over-`[StoredProperty]` helpers: XML (de)serialization +
//    reflection, no equivalent surface in this port (repo-wide XML/reflection severance).
class Threshold {
   public:
    using ConvergenceCriteria = hecfda::statistics::ConvergenceCriteria;
    using UncertainPairedData = hecfda::model::paired_data::UncertainPairedData;

    // Move-only: system_performance_results_ is SystemPerformanceResults, itself move-only (see
    // class comment).
    Threshold(Threshold&&) = default;
    Threshold& operator=(Threshold&&) = default;
    Threshold(const Threshold&) = delete;
    Threshold& operator=(const Threshold&) = delete;

    // ported from: Threshold.cs `public Threshold()` -- the null/dummy ctor. ThresholdID = 9999,
    // ThresholdValue defaults to 0 (C# leaves it unset, i.e. double's default), IsNull = true.
    Threshold()
        : threshold_type_(ThresholdEnum::DefaultExteriorStage),
          system_performance_results_(),
          threshold_id_(9999),
          is_null_(true) {}

    // ported from: Threshold.cs `public Threshold(int thresholdID, ConvergenceCriteria c,
    // ThresholdEnum thresholdType = 0, double thresholdValue = 0)`.
    Threshold(int threshold_id, ConvergenceCriteria c,
              ThresholdEnum threshold_type = static_cast<ThresholdEnum>(0), double threshold_value = 0)
        : threshold_type_(threshold_type),
          threshold_value_(threshold_value),
          system_performance_results_(c),
          threshold_id_(threshold_id),
          is_null_(false) {}

    // ported from: Threshold.cs `public Threshold(int thresholdID, UncertainPairedData
    // systemResponseCurve, ConvergenceCriteria c, ThresholdEnum thresholdType = 0, double
    // thresholdValue = 0)`.
    Threshold(int threshold_id, UncertainPairedData system_response_curve, ConvergenceCriteria c,
              ThresholdEnum threshold_type = static_cast<ThresholdEnum>(0), double threshold_value = 0)
        : threshold_type_(threshold_type),
          threshold_value_(threshold_value),
          system_performance_results_(std::move(system_response_curve), c),
          threshold_id_(threshold_id),
          is_null_(false) {}

    ThresholdEnum threshold_type() const { return threshold_type_; }
    double threshold_value() const { return threshold_value_; }
    int threshold_id() const { return threshold_id_; }
    bool is_null() const { return is_null_; }
    SystemPerformanceResults& system_performance_results() { return system_performance_results_; }
    const SystemPerformanceResults& system_performance_results() const { return system_performance_results_; }

    // ported from: Threshold.cs `public bool Equals(Threshold incomingThreshold)`. Faithful quirk:
    // the C# condition ORs `!thresholdIDIsTheSame` twice (`... || !thresholdIDIsTheSame ||
    // !thresholdValueIsTheSame || !thresholdIDIsTheSame || ...`) -- harmless (OR'ing the same
    // boolean twice changes nothing), transcribed here as a single term since the duplicate has no
    // behavioral effect.
    bool equals(const Threshold& incoming_threshold) const {
        bool threshold_type_is_the_same = threshold_type_ == incoming_threshold.threshold_type_;
        bool threshold_value_is_the_same = threshold_value_ == incoming_threshold.threshold_value_;
        bool threshold_id_is_the_same = threshold_id_ == incoming_threshold.threshold_id_;
        bool project_performance_is_the_same =
            system_performance_results_.equals(incoming_threshold.system_performance_results_);
        if (!threshold_type_is_the_same || !threshold_id_is_the_same || !threshold_value_is_the_same ||
            !project_performance_is_the_same) {
            return false;
        }
        return true;
    }

   private:
    ThresholdEnum threshold_type_;
    double threshold_value_ = 0.0;
    SystemPerformanceResults system_performance_results_;
    int threshold_id_ = 0;
    bool is_null_ = false;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_THRESHOLD_HPP
