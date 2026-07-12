// ported from: HEC.FDA.Model/metrics/PerformanceByThresholds.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_PERFORMANCE_BY_THRESHOLDS_HPP
#define HECFDA_MODEL_METRICS_PERFORMANCE_BY_THRESHOLDS_HPP
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/threshold.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: PerformanceByThresholds.cs `public class PerformanceByThresholds :
// ValidationErrorLogger` -- the container of Threshold results
// (ImpactAreaScenarioResults, Phase 5 Task 6, holds one of these; the EAD compute, Phase 5 Task 9,
// populates it).
//
// Move-only: list_of_thresholds_ is std::vector<Threshold>, and Threshold is itself move-only (see
// threshold.hpp), so this class is move-only too (matches the container-of-move-only-elements
// convention already established by SystemPerformanceResults/UncertainPairedData).
//
// SEVERANCES (present in the C# file, deliberately NOT ported here):
//  - `: ValidationErrorLogger` base class: no MVVM base/validation-log infrastructure in this port
//    (repo-wide MVVM severance).
//  - The private `PerformanceByThresholds(List<Threshold>)` ctor: exists solely to feed
//    `ReadFromXML`'s reconstruction, dropped alongside it.
//  - `WriteToXML()`/`static ReadFromXML(XElement)`: XML (de)serialization, no equivalent surface in
//    this port (repo-wide XML severance).
//  - `GetThreshold`'s MVVM `ErrorMessage`/`ReportMessage(this, new MessageEventArgs(errorMessage))`
//    call on a lookup miss: C# still logs-and-returns a dummy `Threshold()` fallback. This port
//    throws `std::runtime_error` on a miss instead, matching the repo-wide convention for severed
//    MVVM-`ReportMessage` misses (see `SystemPerformanceResults::get_assurance` /
//    `StudyAreaConsequencesBinned::require_consequence_result`).
//
// FAITHFUL UPSTREAM QUIRK, reproduced verbatim (see equals()): C#'s `Equals` nests a `foreach` of
// `ListOfThresholds` inside a `foreach` of the incoming list, only ever reassigning `success` when
// a matching `ThresholdID` pair is found, and never `break`s out of the OUTER loop on a mismatch
// (only the inner one). Consequences transcribed as-is:
//  (1) if no ID pair matches at all, `success` is vacuously left at its initial `true`.
//  (2) a later matching pair can silently overwrite an earlier mismatch's `false` back to `true`,
//      since only the inner loop breaks -- the outer loop keeps going and keeps reassigning
//      `success`.
class PerformanceByThresholds {
   public:
    // Move-only: list_of_thresholds_ holds Threshold, itself move-only (see class comment).
    PerformanceByThresholds(PerformanceByThresholds&&) = default;
    PerformanceByThresholds& operator=(PerformanceByThresholds&&) = default;
    PerformanceByThresholds(const PerformanceByThresholds&) = delete;
    PerformanceByThresholds& operator=(const PerformanceByThresholds&) = delete;

    // ported from: PerformanceByThresholds.cs `public PerformanceByThresholds()`.
    PerformanceByThresholds() = default;

    // ported from: PerformanceByThresholds.cs `public PerformanceByThresholds(bool isNull)` -- adds
    // a single dummy Threshold (ThresholdID 9999) to an otherwise-empty list.
    explicit PerformanceByThresholds(bool is_null) : is_null_(is_null) { list_of_thresholds_.push_back(Threshold()); }

    bool is_null() const { return is_null_; }
    std::vector<Threshold>& list_of_thresholds() { return list_of_thresholds_; }
    const std::vector<Threshold>& list_of_thresholds() const { return list_of_thresholds_; }

    // ported from: PerformanceByThresholds.cs `public void AddThreshold(Threshold threshold)`.
    void add_threshold(Threshold threshold) { list_of_thresholds_.push_back(std::move(threshold)); }

    // ported from: PerformanceByThresholds.cs `public Threshold GetThreshold(int thresholdID)`.
    // See the class comment's SEVERANCES note: throws on a miss instead of C#'s
    // log-and-return-dummy-fallback.
    Threshold& get_threshold(int threshold_id) {
        for (Threshold& threshold : list_of_thresholds_) {
            if (threshold.threshold_id() == threshold_id) {
                return threshold;
            }
        }
        throw std::runtime_error(
            "PerformanceByThresholds::get_threshold: no threshold found for threshold_id=" +
            std::to_string(threshold_id) +
            " (mirrors C# ReportMessage(Fatal)+dummy-fallback, severed here per repo convention -- "
            "see SystemPerformanceResults::get_assurance)");
    }
    const Threshold& get_threshold(int threshold_id) const {
        for (const Threshold& threshold : list_of_thresholds_) {
            if (threshold.threshold_id() == threshold_id) {
                return threshold;
            }
        }
        throw std::runtime_error(
            "PerformanceByThresholds::get_threshold: no threshold found for threshold_id=" +
            std::to_string(threshold_id) +
            " (mirrors C# ReportMessage(Fatal)+dummy-fallback, severed here per repo convention -- "
            "see SystemPerformanceResults::get_assurance)");
    }

    // ported from: PerformanceByThresholds.cs `public bool Equals(PerformanceByThresholds
    // incomingPerformanceByThresholds)`. See the class comment's FAITHFUL UPSTREAM QUIRK note.
    bool equals(const PerformanceByThresholds& incoming_performance_by_thresholds) const {
        bool success = true;
        for (const Threshold& threshold : list_of_thresholds_) {
            for (const Threshold& input_threshold : incoming_performance_by_thresholds.list_of_thresholds_) {
                if (threshold.threshold_id() == input_threshold.threshold_id()) {
                    success = threshold.equals(input_threshold);
                    if (!success) {
                        break;
                    }
                }
            }
        }
        return success;
    }

   private:
    bool is_null_ = false;
    std::vector<Threshold> list_of_thresholds_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_PERFORMANCE_BY_THRESHOLDS_HPP
