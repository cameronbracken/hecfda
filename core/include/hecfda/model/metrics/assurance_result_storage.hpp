// ported from: HEC.FDA.Model/metrics/AssuranceResultStorage.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_ASSURANCE_RESULT_STORAGE_HPP
#define HECFDA_MODEL_METRICS_ASSURANCE_RESULT_STORAGE_HPP
#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/histograms/dynamic_histogram.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: AssuranceResultStorage.cs `public class AssuranceResultStorage`. The
// histogram-staging Monte Carlo accumulator for one assurance metric (e.g. conditional
// non-exceedance probability against a threshold): realizations are written into a fixed-size
// staging buffer via add_observation, then batched into AssuranceHistogram via
// put_data_into_histogram. Unlike AggregatedConsequencesBinned (the closest sibling pattern, see
// aggregated_consequences_binned.hpp), the histogram here is constructed EAGERLY in the public
// ctor (`new DynamicHistogram(binWidth, convergenceCriteria)`), not deferred -- so
// assurance_histogram_ is a plain by-value member, not a nullable/lazy unique_ptr.
//
// AddObservation's `iteration` parameter, verified against the pinned source rather than assumed:
// C# declares `public void AddObservation(double result, int iteration)` -- `int`, not `long`.
// Transcribed as `int` here.
//
// DONE_WITH_CONCERNS (scoped out, not ported; documented per repo convention):
//  - `internal AssuranceResultStorage(string dummyAsuranceType, double
//    standardNonExceedanceProbability)`: the "dummy" error-handling ctor. It constructs
//    `AssuranceHistogram = new DynamicHistogram()`, the C# parameterless "ARBITRARY histogram"
//    ctor that dynamic_histogram.hpp's own DONE_WITH_CONCERNS explicitly declined to port (a
//    serialization/placeholder helper, not a data-collection surface). Not needed by this task's
//    compute path (the public ConvergenceCriteria ctor below). Deferred, not permanently
//    severed -- revisit if a later task needs an explicit "null"/error sentinel instance.
//  - `private AssuranceResultStorage(string, double, DynamicHistogram)` and
//    `WriteToXML()`/`ReadFromXML(XElement)`: the private ctor exists solely to feed ReadFromXML's
//    reconstruction; both are XML (de)serialization with no equivalent surface in this port
//    (matches the repo-wide XML severance elsewhere, e.g. convergence_criteria.hpp).
class AssuranceResultStorage {
   public:
    // ported from: AssuranceResultStorage.cs `public AssuranceResultStorage(string assuranceType,
    // double binWidth, ConvergenceCriteria convergenceCriteria, double
    // standardNonExceedanceProbabilityForAssuranceOfTargetOrLevee = 0)` -- the compute ctor. Sizes
    // the staging array to convergenceCriteria.IterationCount and constructs AssuranceHistogram
    // immediately (eager, not deferred), matching C# field-init order verbatim.
    AssuranceResultStorage(std::string assurance_type, double bin_width,
                            statistics::ConvergenceCriteria convergence_criteria,
                            double standard_non_exceedance_probability = 0)
        : standard_non_exceedance_probability_(standard_non_exceedance_probability),
          temp_results_(static_cast<std::size_t>(convergence_criteria.iteration_count()), 0.0),
          assurance_histogram_(bin_width, convergence_criteria),
          assurance_type_(std::move(assurance_type)) {}

    const std::string& assurance_type() const { return assurance_type_; }
    const statistics::histograms::DynamicHistogram& assurance_histogram() const {
        return assurance_histogram_;
    }
    statistics::histograms::DynamicHistogram& assurance_histogram() { return assurance_histogram_; }
    double standard_non_exceedance_probability() const { return standard_non_exceedance_probability_; }

    // ported from: AssuranceResultStorage.cs `public bool
    // Equals(AssuranceResultStorage incomingAssuranceResultStorage)`. Field-by-field, early-return
    // on mismatch, transcribed in the same order. StandardNonExceedanceProbability uses plain `==`
    // in the C# source (not `.Equals`, unlike ConsequenceResult's NaN-aware comparisons) --
    // transcribed verbatim.
    bool equals(const AssuranceResultStorage& incoming_assurance_result_storage) const {
        if (assurance_type_ == incoming_assurance_result_storage.assurance_type_) {
            if (standard_non_exceedance_probability_ ==
                incoming_assurance_result_storage.standard_non_exceedance_probability_) {
                if (assurance_histogram_.equals(incoming_assurance_result_storage.assurance_histogram_)) {
                    return true;
                }
            }
        }
        return false;
    }

    // ported from: AssuranceResultStorage.cs `public void AddObservation(double result, int
    // iteration)`.
    void add_observation(double result, int iteration) {
        temp_results_[static_cast<std::size_t>(iteration)] = result;
    }

    // ported from: AssuranceResultStorage.cs `public void PutDataIntoHistogram()`. Flush the
    // staged array into AssuranceHistogram, then clear the staging array (order transcribed
    // verbatim -- flush THEN clear).
    void put_data_into_histogram() {
        assurance_histogram_.add_observations_to_histogram(temp_results_);
        std::fill(temp_results_.begin(), temp_results_.end(), 0.0);
    }

   private:
    double standard_non_exceedance_probability_;
    std::vector<double> temp_results_;
    statistics::histograms::DynamicHistogram assurance_histogram_;
    std::string assurance_type_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_ASSURANCE_RESULT_STORAGE_HPP
