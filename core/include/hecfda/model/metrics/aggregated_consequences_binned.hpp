// ported from: HEC.FDA.Model/metrics/AggregatedConsequencesBinned.cs @ f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_METRICS_AGGREGATED_CONSEQUENCES_BINNED_HPP
#define HECFDA_MODEL_METRICS_AGGREGATED_CONSEQUENCES_BINNED_HPP
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/aggregated_consequences_by_quantile.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/histograms/dynamic_histogram.hpp"
namespace hecfda {
namespace model {
namespace metrics {

// ported from: AggregatedConsequencesBinned.cs `public class AggregatedConsequencesBinned`. The
// histogram-staging Monte Carlo accumulator for one (damageCategory, assetCategory, impactArea,
// consequenceType, riskType) combo: realizations are written into a fixed-size staging buffer via
// add_consequence_realization, then batched into a lazily-constructed DynamicHistogram (and a
// parallel width-1 "damaged element count" histogram) via put_data_into_histogram.
//
// Histogram member type, a deliberate port deviation: the C# properties are typed `IHistogram`
// (an interface). In this port IHistogram (statistics::histograms::IHistogram) is a standalone
// interface that does NOT extend IDistribution (see i_histogram.hpp's class comment) -- so an
// IHistogram* here would NOT expose inverse_cdf/pdf (those live on IDistribution, reached via
// DynamicHistogram's other base ContinuousDistribution) or equals (IDistribution::equals). Since
// DynamicHistogram is the only concrete IHistogram in the port, the members below are typed as
// `std::unique_ptr<statistics::histograms::DynamicHistogram>` (deferred/nullable, matching the C#
// `{ get; private set; }` property that starts null) rather than routed through the interface.
//
// Staged-array quirk, transcribed verbatim: put_data_into_histogram() computes the initial bin
// width from the min/max of the ENTIRE temp_results_ staging array, not just the slots actually
// written by add_consequence_realization. Unwritten slots are 0.0 (C# `new double[n]` default), so
// unless every one of the IterationCount slots was staged, 0.0 participates in the min/max/range
// computation alongside the real realizations -- e.g. an all-positive realization set still gets
// min_ == 0.0 from the untouched tail. Faithful to upstream; see the "wide_range" fixture case,
// which exercises this deliberately.
//
// DONE_WITH_CONCERNS (scoped out, not ported; documented per repo convention):
//  - WriteToXML() / ReadFromXML(XElement): XML (de)serialization, no equivalent surface in this
//    port (matches the repo-wide XML severance elsewhere, e.g. convergence_criteria.hpp).
//  - The two "null/dummy" ctors -- `AggregatedConsequencesBinned(int impactAreaID,
//    ConsequenceType, RiskType)` and `AggregatedConsequencesBinned(string, string, int,
//    ConsequenceType = Damage, RiskType = Fail)` (both set IsNull = true) -- construct
//    `ConsequenceHistogram = new DynamicHistogram()`, the C# parameterless "ARBITRARY histogram"
//    ctor that dynamic_histogram.hpp's own DONE_WITH_CONCERNS explicitly declined to port (a
//    serialization/placeholder helper, not a data-collection surface). Porting either dummy ctor
//    here would require porting that placeholder ctor first; neither is needed by this task's
//    compute path (Task 4/7 construct via the ConvergenceCriteria ctor below). Deferred, not
//    permanently severed -- revisit if a later task needs an explicit "null" sentinel instance.
//    UN-SEVERED Phase 6 Task 10 (see the `(string, string, int, ConsequenceType, RiskType)` ctor
//    below): `AlternativeComparisonReport::compute_distribution_ead_reduced` needed exactly this
//    "no counterpart" placeholder. The OTHER dummy ctor (`(int, ConsequenceType, RiskType)`, which
//    hardcodes DamageCategory/AssetCategory to "UNASSIGNED") remains un-ported -- still no caller
//    needs it.
//  - `AggregatedConsequencesBinned(string, string, IHistogram histogram, int, ConsequenceType =
//    Damage, RiskType = Fail)` (reconstructs from an already-built histogram, reading
//    ConvergenceCriteria off it): not required by this task; trivial to add later (it needs no
//    unported dependency) if a caller needs to wrap a pre-built histogram. UN-SEVERED Phase 6 Task
//    9 (see that ctor below) -- `Alternative`'s own upstream unit tests construct
//    `AggregatedConsequencesBinned` directly from a pre-built `DynamicHistogram` this way.
//  - The C# `RegionID` field's initializer `= utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE`
//    (-999): every ported ctor (only the compute ctor below) unconditionally overwrites RegionID
//    with the impactAreaID argument in the ctor body, so the initializer value is never actually
//    observable here. Not inlined as a constant since nothing reads it.
class AggregatedConsequencesBinned {
   public:
    // ported from: AggregatedConsequencesBinned.cs field constant.
    static constexpr int INITIAL_BIN_QUANTITY = 500;

    // ported from: AggregatedConsequencesBinned.cs `public AggregatedConsequencesBinned(string
    // damageCategory, string assetCategory, ConvergenceCriteria convergenceCriteria, int
    // impactAreaID, ConsequenceType consequenceType, RiskType riskType = RiskType.Fail)` -- the
    // compute ctor. Defers histogram construction (histogram_not_constructed_ = true) and sizes
    // both staging arrays to convergenceCriteria.IterationCount, matching C# exactly.
    AggregatedConsequencesBinned(std::string damage_category, std::string asset_category,
                                  statistics::ConvergenceCriteria convergence_criteria,
                                  int impact_area_id, ConsequenceType consequence_type,
                                  RiskType risk_type = RiskType::Fail)
        : damage_category_(std::move(damage_category)),
          asset_category_(std::move(asset_category)),
          consequence_type_(consequence_type),
          risk_type_(risk_type),
          region_id_(impact_area_id),
          is_null_(false),
          convergence_criteria_(convergence_criteria),
          temp_results_(static_cast<std::size_t>(convergence_criteria.iteration_count()), 0.0),
          temp_counts_(static_cast<std::size_t>(convergence_criteria.iteration_count()), 0.0),
          histogram_not_constructed_(true) {}

    // ported from: AggregatedConsequencesBinned.cs `public AggregatedConsequencesBinned(string
    // damageCategory, string assetCategory, IHistogram histogram, int impactAreaID, ConsequenceType
    // consequenceType = ConsequenceType.Damage, RiskType riskType = RiskType.Fail)` -- reconstructs
    // from an already-built histogram. UN-SEVERED Phase 6 Task 9 (the class comment's
    // DONE_WITH_CONCERNS originally deferred this: "not required by this task; trivial to add
    // later"): `Alternative`'s own upstream unit tests (AlternativeTest.cs, e.g.
    // `LifeLossResultsExcludedFromEqad`) construct `AggregatedConsequencesBinned` directly from a
    // pre-built `DynamicHistogram` this way, so this task's fixture needs the same surface.
    // `damaged_element_quantity_histogram_` is left null/unset here, matching C# leaving
    // `DamagedElementQuantityHistogram` at its default null (never assigned in this ctor).
    // Takes ownership of the histogram via `unique_ptr` (the value-semantics analogue of C#'s
    // `ConsequenceHistogram = histogram` reference assignment) rather than a raw/const reference,
    // matching every other move-only-member ctor in this port. `temp_results_`/`temp_counts_` are
    // still allocated and sized to `convergence_criteria.iteration_count()` per the C# ctor body
    // (dead storage here: `histogram_not_constructed_` stays false, so
    // `put_data_into_histogram()`'s branch that would consume them never runs) -- kept anyway for
    // faithfulness, matching how the C# ctor allocates `_TempResults`/`_TempCounts` unconditionally
    // too.
    AggregatedConsequencesBinned(std::string damage_category, std::string asset_category,
                                  std::unique_ptr<statistics::histograms::DynamicHistogram> histogram,
                                  int impact_area_id,
                                  ConsequenceType consequence_type = ConsequenceType::Damage,
                                  RiskType risk_type = RiskType::Fail)
        : damage_category_(std::move(damage_category)),
          asset_category_(std::move(asset_category)),
          consequence_type_(consequence_type),
          risk_type_(risk_type),
          region_id_(impact_area_id),
          is_null_(false),
          convergence_criteria_(histogram->convergence_criteria()),
          temp_results_(static_cast<std::size_t>(convergence_criteria_.iteration_count()), 0.0),
          temp_counts_(static_cast<std::size_t>(convergence_criteria_.iteration_count()), 0.0),
          histogram_not_constructed_(false),
          consequence_histogram_(std::move(histogram)) {}

    // ported from: AggregatedConsequencesBinned.cs `public AggregatedConsequencesBinned(string
    // damageCategory, string assetCategory, int impactAreaID, ConsequenceType consequenceType =
    // ConsequenceType.Damage, RiskType riskType = RiskType.Fail)` -- the "null"/dummy ctor.
    // UN-SEVERED Phase 6 Task 10: `AlternativeComparisonReport::compute_distribution_ead_reduced`
    // needs an explicit "no counterpart" placeholder `AggregatedConsequencesBinned` when a
    // with-project (or without-project) consequence result has no matching counterpart on the
    // other side -- exactly the scenario this class's original DONE_WITH_CONCERNS deferred ("not
    // needed by this task's compute path... revisit if a later task needs an explicit null
    // sentinel instance"). That later task has arrived.
    //
    // C#'s `ConsequenceHistogram = new DynamicHistogram()` / `DamagedElementQuantityHistogram =
    // new DynamicHistogram()` both call the parameterless "ARBITRARY histogram" ctor
    // (dynamic_histogram.hpp's own DONE_WITH_CONCERNS explicitly declined to port it as a
    // standalone ctor -- "a serialization/placeholder helper, not a data-collection surface").
    // Rather than adding that ctor to DynamicHistogram, this port reproduces its documented effect
    // (`BinWidth = DEFAULT_BIN_WIDTH; _minHasNotBeenSet = true; ConvergenceCriteria = new
    // ConvergenceCriteria(); ten AddObservationToHistogram(0) calls`) inline via the existing
    // public `DynamicHistogram(double bin_width, ConvergenceCriteria)` ctor (which already sets
    // min_has_not_been_set_ = true, matching `_minHasNotBeenSet = true`) plus ten
    // `add_observation_to_histogram(0.0)` calls -- byte-identical resulting histogram state, built
    // from already-ported public surface instead of a new dedicated ctor. Note: DamageCategory/
    // AssetCategory are NOT overwritten to "UNASSIGNED" here (unlike the sibling `(int, ConsequenceType,
    // RiskType)` dummy ctor a few lines above, which this port declined to add -- not needed by any
    // caller yet) -- this ctor stores the given strings verbatim, matching the C# source exactly.
    AggregatedConsequencesBinned(std::string damage_category, std::string asset_category, int impact_area_id,
                                  ConsequenceType consequence_type = ConsequenceType::Damage,
                                  RiskType risk_type = RiskType::Fail)
        : damage_category_(std::move(damage_category)),
          asset_category_(std::move(asset_category)),
          consequence_type_(consequence_type),
          risk_type_(risk_type),
          region_id_(impact_area_id),
          is_null_(true),
          convergence_criteria_(),
          temp_results_(static_cast<std::size_t>(convergence_criteria_.iteration_count()), 0.0),
          temp_counts_(static_cast<std::size_t>(convergence_criteria_.iteration_count()), 0.0),
          histogram_not_constructed_(false),
          consequence_histogram_(make_arbitrary_histogram()),
          damaged_element_quantity_histogram_(make_arbitrary_histogram()) {}

    const std::string& damage_category() const { return damage_category_; }
    const std::string& asset_category() const { return asset_category_; }
    ConsequenceType consequence_type() const { return consequence_type_; }
    RiskType risk_type() const { return risk_type_; }
    int region_id() const { return region_id_; }
    bool is_null() const { return is_null_; }
    const statistics::ConvergenceCriteria& convergence_criteria() const { return convergence_criteria_; }

    // Convergence-support accessors (Task 4: StudyAreaConsequencesBinned's
    // results_are_converged/remaining_iterations call is_histogram_converged/
    // estimate_iterations_remaining/histogram_is_zero_valued, all non-const on DynamicHistogram,
    // hence the non-const overload). nullptr until put_data_into_histogram() has run once, exactly
    // like the C# `ConsequenceHistogram` property being null before then.
    statistics::histograms::DynamicHistogram* consequence_histogram() {
        return consequence_histogram_.get();
    }
    const statistics::histograms::DynamicHistogram* consequence_histogram() const {
        return consequence_histogram_.get();
    }
    statistics::histograms::DynamicHistogram* damaged_element_quantity_histogram() {
        return damaged_element_quantity_histogram_.get();
    }
    const statistics::histograms::DynamicHistogram* damaged_element_quantity_histogram() const {
        return damaged_element_quantity_histogram_.get();
    }

    // ported from: AggregatedConsequencesBinned.cs `internal void PutDataIntoHistogram()`. See the
    // class comment for the staged-array min/max quirk this transcribes verbatim.
    void put_data_into_histogram() {
        if (histogram_not_constructed_) {
            double max = *std::max_element(temp_results_.begin(), temp_results_.end());
            double min = *std::min_element(temp_results_.begin(), temp_results_.end());
            double range = max - min;
            double bin_width;
            if (range < INITIAL_BIN_QUANTITY) {
                bin_width = statistics::histograms::DynamicHistogram::DEFAULT_BIN_WIDTH;
            } else {
                bin_width = range / INITIAL_BIN_QUANTITY;
            }
            consequence_histogram_ = std::make_unique<statistics::histograms::DynamicHistogram>(
                bin_width, convergence_criteria_);
            damaged_element_quantity_histogram_ =
                std::make_unique<statistics::histograms::DynamicHistogram>(1.0, convergence_criteria_);
            histogram_not_constructed_ = false;
        }
        consequence_histogram_->add_observations_to_histogram(temp_results_);
        damaged_element_quantity_histogram_->add_observations_to_histogram(temp_counts_);
        // ported from: C# `Array.Clear(_TempResults)` -- note only _TempResults is cleared;
        // _TempCounts is NOT, transcribed verbatim (faithful upstream asymmetry).
        std::fill(temp_results_.begin(), temp_results_.end(), 0.0);
    }

    // ported from: AggregatedConsequencesBinned.cs `internal void AddConsequenceRealization(double
    // damageRealization, long iteration = 1, int damagedElementsCount = 0)`.
    void add_consequence_realization(double damage_realization, std::int64_t iteration = 1,
                                      int damaged_elements_count = 0) {
        temp_results_[static_cast<std::size_t>(iteration)] = damage_realization;
        temp_counts_[static_cast<std::size_t>(iteration)] = static_cast<double>(damaged_elements_count);
    }

    // ported from: AggregatedConsequencesBinned.cs `internal double
    // SampleMeanExpectedAnnualConsequences()`.
    double sample_mean_expected_annual_consequences() const {
        return consequence_histogram_->sample_mean();
    }

    // ported from: AggregatedConsequencesBinned.cs `internal double
    // ConsequenceExceededWithProbabilityQ(double exceedanceProbability)`.
    double consequence_exceeded_with_probability_q(double exceedance_probability) const {
        double non_exceedance_probability = 1 - exceedance_probability;
        return consequence_histogram_->inverse_cdf(non_exceedance_probability);
    }

    // ported from: AggregatedConsequencesBinned.cs `internal double
    // QuantityExceededWithProbabilityQ(double exceedanceProbability)`.
    double quantity_exceeded_with_probability_q(double exceedance_probability) const {
        double non_exceedance_probability = 1 - exceedance_probability;
        return damaged_element_quantity_histogram_->inverse_cdf(non_exceedance_probability);
    }

    // ported from: AggregatedConsequencesBinned.cs `internal bool
    // Equals(AggregatedConsequencesBinned damageResult)` -- compares ONLY the consequence
    // histograms, transcribed verbatim (the damaged-element-quantity histogram, categories,
    // region, consequence/risk type are NOT compared, matching upstream).
    bool equals(const AggregatedConsequencesBinned& other) const {
        return consequence_histogram_->equals(*other.consequence_histogram_);
    }

    // ported from: AggregatedConsequencesBinned.cs `public static AggregatedConsequencesByQuantile
    // ConvertToSingleEmpiricalDistributionOfConsequences(AggregatedConsequencesBinned
    // consequenceDistributionResult)`. UN-SEVERED Phase 6 Task 4 (deferred at Phase 4 Task 3, see
    // this class's DONE_WITH_CONCERNS history -- it depended on DynamicHistogram::
    // convert_to_empirical_distribution, un-severed Phase 6 Task 1, and on
    // AggregatedConsequencesByQuantile, ported Phase 6 Task 2). The C# version is `static`, taking
    // the source object as its sole parameter; ported here as a `const` instance method (using
    // `this` in place of the C# parameter, per this task's brief) -- semantically identical, since
    // the C# body only ever reads off `consequenceDistributionResult`. Field mapping transcribed in
    // the exact order the C# ctor call lists them: DamageCategory, AssetCategory, the converted
    // Empirical, RegionID, ConsequenceType, RiskType.
    AggregatedConsequencesByQuantile convert_to_single_empirical_distribution_of_consequences() const {
        statistics::distributions::Empirical empirical = consequence_histogram_->convert_to_empirical_distribution();
        return AggregatedConsequencesByQuantile(damage_category_, asset_category_, std::move(empirical),
                                                 region_id_, consequence_type_, risk_type_);
    }

   private:
    // ported from: DynamicHistogram.cs `public DynamicHistogram()` (the "ARBITRARY histogram"
    // ctor) -- see the "null"/dummy ctor's comment above for why this is reproduced inline here
    // rather than as a standalone DynamicHistogram ctor. Builds a fresh histogram each call (a
    // move-only unique_ptr can't be shared between the two dummy-ctor members).
    static std::unique_ptr<statistics::histograms::DynamicHistogram> make_arbitrary_histogram() {
        auto histogram = std::make_unique<statistics::histograms::DynamicHistogram>(
            statistics::histograms::DynamicHistogram::DEFAULT_BIN_WIDTH, statistics::ConvergenceCriteria());
        for (int i = 0; i < 10; ++i) {
            histogram->add_observation_to_histogram(0.0);
        }
        return histogram;
    }

    std::string damage_category_;
    std::string asset_category_;
    ConsequenceType consequence_type_;
    RiskType risk_type_;
    int region_id_;
    bool is_null_;
    statistics::ConvergenceCriteria convergence_criteria_;
    std::vector<double> temp_results_;
    std::vector<double> temp_counts_;
    bool histogram_not_constructed_;
    std::unique_ptr<statistics::histograms::DynamicHistogram> consequence_histogram_;
    std::unique_ptr<statistics::histograms::DynamicHistogram> damaged_element_quantity_histogram_;
};

}  // namespace metrics
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_METRICS_AGGREGATED_CONSEQUENCES_BINNED_HPP
