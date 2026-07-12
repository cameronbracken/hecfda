// ported from: upstream/HEC-FDA/HEC.FDA.Model/stageDamage/ImpactAreaStageDamage.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_STAGE_DAMAGE_IMPACT_AREA_STAGE_DAMAGE_HPP
#define HECFDA_MODEL_STAGE_DAMAGE_IMPACT_AREA_STAGE_DAMAGE_HPP
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
#include "hecfda/model/paired_data/graphical_uncertain_paired_data.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/model/stage_damage/hydraulic_profiles.hpp"
#include "hecfda/model/structures/inventory.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
namespace hecfda {
namespace model {
namespace stage_damage {

// ported from: ImpactAreaStageDamage.cs. Phase 4 Task 6 ports the GEOMETRY portion only: the
// constructor + EstablishAggregationStages (IdentifyCentralStageFrequencyAtIndexLocation,
// IdentifyMinAndMaxStageWithUncertainty, SetCoordinateQuantity, ComputeStagesAtIndexLocation) and
// the pure static/instance stage-interval helpers those Task-7 compute methods will need
// (ExtrapolateFromAboveAtIndexLocation, ExtrapolateFromBelowStagesAtIndexLocation,
// CalculateIntervals, CalculateIncrementOfStages, CalculateLowerIncrementOfStages). Compute() /
// ComputeDamageWithUncertaintyAllCoordinates / ComputeLower·Middle·UpperStageDamage /
// ProduceZeroDamageFunctions / ProduceImpactAreaStructureDetails and the *ToStrings CSV helpers are
// Task 7 (or later) -- deliberately NOT declared here (see class comment's SEVERANCES section).
//
// OWNERSHIP / MOVE-vs-COPY DECISION (Task 6): unlike GraphicalUncertainPairedData/
// UncertainPairedData (move-only -- they hold vector<unique_ptr<IDistribution>>), this class holds
// no unique_ptr members directly:
//   - Inventory is copyable (Phase 3 Task 6: shared_ptr<map<string,OccupancyType>> +
//     vector<Structure>, both copyable) -- stored BY VALUE, moved into place in the ctor.
//   - HydraulicProfiles is copyable (two plain vector members) -- stored BY VALUE, moved into
//     place in the ctor.
//   - The four frequency-input parameters (analytical_flow_frequency, graphical_frequency,
//     discharge_stage, unregulated_regulated) are all C# NULLABLE REFERENCE fields in the source
//     (`private readonly ContinuousDistribution _AnalyticalFlowFrequency;` etc., assigned directly
//     from the ctor parameter, never copied). This port stores them as non-owning raw pointers,
//     the closest C++ analog of "nullable reference, no ownership transfer, caller manages
//     lifetime" -- copying a GraphicalUncertainPairedData/UncertainPairedData to store by value is
//     not even possible (move-only) and would change the aliasing semantics anyway (the C# ctor
//     never deep-copies these). The caller must keep the referenced objects alive for at least as
//     long as this ImpactAreaStageDamage instance uses them (construction time here; Task 7's
//     Compute() does not re-read them, so post-construction they are unused).
//   - stage_frequency_ is std::optional<PairedData> rather than PairedData, because
//     IdentifyCentralStageFrequencyAtIndexLocation's C# body can fall through every branch and
//     return null (see identify_central_stage_frequency_at_index_location()'s comment) -- PairedData
//     has no ctor that can represent an empty/null curve (require_non_empty() guards reject it), so
//     optional<PairedData> is the faithful "may be null" analog, matching the "IsNull()" pattern used
//     elsewhere in this port (CurveMetaData) but via std::optional since PairedData itself carries no
//     such flag.
// Net effect: every member is copyable (Inventory, HydraulicProfiles, optional<PairedData>, raw
// pointers, ints/doubles), so this class ends up naturally COPYABLE AND MOVABLE via the implicitly
// defaulted special member functions -- no manual copy/move overrides are declared. This is a
// deliberate departure from the "move-only" default this port otherwise uses for classes wrapping
// vector<unique_ptr<IDistribution>>; here nothing in the class's own storage forces that
// restriction.
//
// SEVERANCES (present in the C# file, deliberately NOT ported in this task):
//   - `: PropertyValidationHelper, IDontImplementValidationButMyPropertiesDo` base + Validate() /
//     GetErrorsFromProperties(): no rules/validation-on-construct infrastructure anywhere in this
//     Model-layer port (same severance already established throughout Phase 1-4).
//   - `MessageReport` / `ReportMessage(object, MessageEventArgs)` (MVVM messaging): every C# call
//     site that builds an ErrorMessage and calls ReportMessage(this, ...) followed by an implicit
//     fall-through (the C# method keeps running / returns null rather than throwing) is replaced
//     here by a thrown std::runtime_error carrying the same message text -- see
//     throw_missing_discharge_stage_error()/throw_no_frequency_function_error() below. This is a
//     BEHAVIORAL change (C# limps on with a null _StageFrequency that NREs on first use downstream;
//     this port fails fast at the point of detection with a clear message) but preserves the "this
//     configuration is invalid" signal faithfully.
//   - Compute() and everything it calls (ComputeDamageWithUncertaintyAllCoordinates,
//     ComputeLowerStageDamage, ComputeMiddleStageDamage/InterpolateBetweenProfiles,
//     ComputeUpperStageDamage, ProduceZeroDamageFunctions, CreateConsequenceDistributionResults,
//     DumpDataIntoDistributions, IsTheFunctionNotConverged): Task 7. Not stubbed here (no
//     NotImplementedException placeholder) -- nothing in this task's fixture or Task 7's stated scope
//     needs a compilable-but-throwing Compute() yet, and adding one now would just be dead code to
//     delete/rewrite next task.
//   - ProduceImpactAreaStructureDetails / DamagesToStrings / DepthsToStrings / StagesToStrings: CSV
//     text-report generation -- severed per CLAUDE.md (text/CSV formatting is out of scope
//     throughout this port).
//   - ProgressReporter / Stopwatch parameters: UI progress plumbing, dropped (same severance as
//     every other Monte-Carlo-loop task in this port).
//   - `ContinuousDistribution.ToCoordinates(exceedence: false)`, used by
//     IdentifyCentralStageFrequencyAtIndexLocation's analytical-flow-frequency-with-discharge-stage
//     branch: ContinuousDistribution::to_coordinates is explicitly NOT ported (see
//     continuous_distribution.hpp's DONE_WITH_CONCERNS note -- "a UI/graphing concern"). That one
//     branch throws std::logic_error naming the missing dependency instead of being silently wrong
//     or hand-derived; every other branch (including the analytical-flow-frequency path in
//     IdentifyMinAndMaxStageWithUncertainty, which does NOT need ToCoordinates) is fully ported. No
//     fixture case reaches the unported branch -- Task 6's oracle exercises the graphical
//     UsingStagesNotFlows=true path (TractableStageDamageTests' shape), matching the brief.
class ImpactAreaStageDamage {
   public:
    using PairedData = hecfda::model::paired_data::PairedData;
    using Inventory = hecfda::model::structures::Inventory;
    using GraphicalUncertainPairedData = hecfda::model::paired_data::GraphicalUncertainPairedData;
    using UncertainPairedData = hecfda::model::paired_data::UncertainPairedData;
    using ContinuousDistribution = hecfda::statistics::distributions::ContinuousDistribution;

    // Hard-coded compute settings (transcribed verbatim -- see class comment / task brief).
    static constexpr double kMinProbability = 0.0001;
    static constexpr double kMaxProbability = 0.9999;
    static constexpr double kFeetPerCoordinate = 0.25;
    static constexpr int kMinimumExtrapolationCoordinates = 4;
    static constexpr int kMinimumInterpolationCoordinates = 2;

    // ported from: ImpactAreaStageDamage.cs's ctor. `hydroParentDirectory` (disk path, used only by
    // the severed CSV-detail methods) is dropped -- no analog once ProduceImpactAreaStructureDetails
    // is out of scope. `hydraulicDataset` -> `hydraulics` (Task 5's HydraulicProfiles, the
    // hydraulics-as-arrays replacement for the disk-backed HydraulicDataset). Every frequency-input
    // parameter defaults to nullptr, matching the C# optional-parameter defaults of null.
    // `using_mock_data` skips GetInventoryTrimmedToImpactArea, matching every fixture/test upstream
    // that exercises this ctor (TractableStageDamageTests, StageDamageShould all pass
    // usingMockData: true).
    ImpactAreaStageDamage(int impact_area_id, Inventory inventory, HydraulicProfiles hydraulics,
                          int analysis_year = 9999,
                          ContinuousDistribution* analytical_flow_frequency = nullptr,
                          GraphicalUncertainPairedData* graphical_frequency = nullptr,
                          UncertainPairedData* discharge_stage = nullptr,
                          UncertainPairedData* unregulated_regulated = nullptr,
                          bool using_mock_data = false)
        : analytical_flow_frequency_(analytical_flow_frequency),
          graphical_frequency_(graphical_frequency),
          discharge_stage_(discharge_stage),
          unregulated_regulated_(unregulated_regulated),
          impact_area_id_(impact_area_id),
          analysis_year_(analysis_year),
          inventory_(using_mock_data ? std::move(inventory)
                                     : inventory.get_inventory_trimmed_to_impact_area(impact_area_id)),
          hydraulics_(std::move(hydraulics)) {
        establish_aggregation_stages();
    }

    int impact_area_id() const { return impact_area_id_; }
    int analysis_year() const { return analysis_year_; }
    const Inventory& inventory() const { return inventory_; }
    const HydraulicProfiles& hydraulics() const { return hydraulics_; }

    double min_stage_for_area() const { return min_stage_for_area_; }
    double max_stage_for_area() const { return max_stage_for_area_; }
    int bottom_extrapolation_points() const { return bottom_extrapolation_points_; }
    int central_interpolation_points() const { return central_interpolation_points_; }
    int top_extrapolation_points() const { return top_extrapolation_points_; }

    // ported from: ImpactAreaStageDamage.cs's `private PairedData _StageFrequency` field getter
    // usage. Throws std::bad_optional_access if the C# central-stage-frequency computation would
    // have returned null (see identify_central_stage_frequency_at_index_location()) -- every branch
    // this task's fixture exercises leaves it populated.
    const PairedData& stage_frequency() const { return stage_frequency_.value(); }

    // ported from: ImpactAreaStageDamage.cs private double[] ComputeStagesAtIndexLocation(List<double>
    // profileProbabilities). Made PUBLIC here (C# private) so both this task's fixture and Task 7's
    // Compute() (which calls it once per Compute() invocation, C# line
    // `_StagesAtIndexLocation = ComputeStagesAtIndexLocation(wsesAtEachStructureByProfile.Item1);`)
    // can reach it directly.
    //
    // TYPE-FIDELITY NOTE: the C# body mixes float and double arithmetic in a way that changes the
    // result's bit pattern versus doing everything in double -- `float interval = ... / int`,
    // `i * interval` (int * float => float, NOT promoted to double until the final `+`), and
    // symmetrically for `upperInterval * (topPoints - i)`. This port reproduces those exact
    // intermediate float32 truncations (see the `static_cast<float>` casts below) rather than
    // computing everything in double, to stay bit-reproducible against the real C#.
    std::vector<double> compute_stages_at_index_location(const std::vector<double>& profile_probabilities) const {
        const PairedData& stage_frequency = stage_frequency_.value();
        std::size_t num_profiles = profile_probabilities.size();
        if (num_profiles == 0) {
            throw std::invalid_argument("compute_stages_at_index_location: profile_probabilities is empty");
        }
        // C#: `int quantityStages = _BottomExtrapolationPoints + _TopExtrapolationPoints +
        // (profileProbabilities.Count - 1) * _CentralInterpolationPoints;` -- kept as signed int
        // arithmetic (matching C#'s int math) before converting to the vector's unsigned size, so a
        // pathological negative quantity fails loudly here rather than silently wrapping to a huge
        // size_t allocation.
        int quantity_stages = bottom_extrapolation_points_ + top_extrapolation_points_ +
                               (static_cast<int>(num_profiles) - 1) * central_interpolation_points_;
        if (quantity_stages < 0) {
            throw std::invalid_argument("compute_stages_at_index_location: computed a negative stage count");
        }
        std::vector<double> stages(static_cast<std::size_t>(quantity_stages));

        // extrapolate lower stages
        double max_probability =
            *std::max_element(profile_probabilities.begin(), profile_probabilities.end());
        double stage_at_probability_of_lowest_profile = stage_frequency.f(1.0 - max_probability);
        float index_station_lower_stage_delta =
            static_cast<float>(stage_at_probability_of_lowest_profile - min_stage_for_area_);
        float interval = index_station_lower_stage_delta / static_cast<float>(bottom_extrapolation_points_);
        std::size_t stage_index = 0;
        for (int i = 0; i < bottom_extrapolation_points_ + 1; ++i) {
            float i_times_interval = static_cast<float>(i) * interval;
            stages[stage_index] = min_stage_for_area_ + static_cast<double>(i_times_interval);
            ++stage_index;
        }

        // interpolate intermediate stages
        for (std::size_t i = 1; i < num_profiles; ++i) {
            double previous_probability = profile_probabilities[i - 1];
            double current_probability = profile_probabilities[i];

            for (int j = 0; j < central_interpolation_points_; ++j) {
                double previous_stage_at_index_location = stage_frequency.f(1.0 - previous_probability);
                double current_stage_at_index_location = stage_frequency.f(1.0 - current_probability);
                double stage_delta_at_index_location =
                    current_stage_at_index_location - previous_stage_at_index_location;
                double interval_at_index_location =
                    stage_delta_at_index_location / static_cast<double>(central_interpolation_points_);
                double stage_at_index_location =
                    previous_stage_at_index_location + interval_at_index_location * static_cast<double>(j + 1);
                stages[stage_index] = stage_at_index_location;
                ++stage_index;
            }
        }

        // extrapolate upper stages
        double min_probability =
            *std::min_element(profile_probabilities.begin(), profile_probabilities.end());
        double stage_at_probability_of_highest_profile = stage_frequency.f(1.0 - min_probability);
        float index_station_upper_stage_delta =
            static_cast<float>(max_stage_for_area_ - stage_at_probability_of_highest_profile);
        float upper_interval = index_station_upper_stage_delta / static_cast<float>(top_extrapolation_points_);
        for (int i = 1; i < top_extrapolation_points_; ++i) {
            float upper_interval_times_steps =
                upper_interval * static_cast<float>(top_extrapolation_points_ - i);
            stages[stage_index] = max_stage_for_area_ - static_cast<double>(upper_interval_times_steps);
            ++stage_index;
        }

        return stages;
    }

    // ported from: ImpactAreaStageDamage.cs public static float[]
    // ExtrapolateFromAboveAtIndexLocation(float[] stagesAtStructuresHighestProfile, float
    // upperInterval, int stepCount). Pure, static (public/static in C# too -- "this is public and
    // static for testing"). Pinned literal (StageDamageShould.ExtrapolateFromAboveShould):
    // {5,4,3}, upperInterval=1, stepCount=5 -> {10,9,8}.
    static std::vector<float> extrapolate_from_above_at_index_location(
        const std::vector<float>& stages_at_structures_highest_profile, float upper_interval, int step_count) {
        std::vector<float> extrapolated_stages(stages_at_structures_highest_profile.size());
        for (std::size_t i = 0; i < stages_at_structures_highest_profile.size(); ++i) {
            extrapolated_stages[i] =
                stages_at_structures_highest_profile[i] + upper_interval * static_cast<float>(step_count);
        }
        return extrapolated_stages;
    }

    // ported from: ImpactAreaStageDamage.cs public static float[]
    // ExtrapolateFromBelowStagesAtIndexLocation(float[] WSEsAtLowest, float interval, int i, int
    // numInterpolatedStagesToCompute). Pinned literal (StageDamageShould.ExtrapolateFromBelowShould):
    // {500,400,300}, interval=1, i=5, numInterpolatedStagesToCompute=50 -> {455,355,255}.
    static std::vector<float> extrapolate_from_below_stages_at_index_location(
        const std::vector<float>& wses_at_lowest, float interval, int i, int num_interpolated_stages_to_compute) {
        std::vector<float> extrapolated_stages(wses_at_lowest.size());
        for (std::size_t j = 0; j < wses_at_lowest.size(); ++j) {
            extrapolated_stages[j] =
                wses_at_lowest[j] - interval * static_cast<float>(num_interpolated_stages_to_compute - i);
        }
        return extrapolated_stages;
    }

    // ported from: ImpactAreaStageDamage.cs private float CalculateLowerIncrementOfStages(List<double>
    // profileProbabilities). Instance method (reads stage_frequency_/min_stage_for_area_/
    // bottom_extrapolation_points_); reproduces the SAME bottom-interval computation as the first
    // block of compute_stages_at_index_location() above (duplicated in the C# source too -- not
    // factored out upstream, transcribed verbatim rather than "DRY"-ing the two call sites together).
    // Needed by Task 7's ComputeLowerStageDamage; ported now per the brief.
    float calculate_lower_increment_of_stages(const std::vector<double>& profile_probabilities) const {
        const PairedData& stage_frequency = stage_frequency_.value();
        double max_probability =
            *std::max_element(profile_probabilities.begin(), profile_probabilities.end());
        double stage_at_probability_of_lowest_profile = stage_frequency.f(1.0 - max_probability);
        float index_station_lower_stage_delta =
            static_cast<float>(stage_at_probability_of_lowest_profile - min_stage_for_area_);
        float interval = index_station_lower_stage_delta / static_cast<float>(bottom_extrapolation_points_);
        return interval;
    }

    // ported from: ImpactAreaStageDamage.cs private float[] CalculateIntervals(float[]
    // previousStagesAtStructures, float[] currentStagesAtStructures). Instance method (reads
    // central_interpolation_points_). Needed by Task 7's InterpolateBetweenProfiles.
    std::vector<float> calculate_intervals(const std::vector<float>& previous_stages_at_structures,
                                            const std::vector<float>& current_stages_at_structures) const {
        std::vector<float> intervals(previous_stages_at_structures.size());
        for (std::size_t j = 0; j < previous_stages_at_structures.size(); ++j) {
            intervals[j] = (current_stages_at_structures[j] - previous_stages_at_structures[j]) /
                           static_cast<float>(central_interpolation_points_);
        }
        return intervals;
    }

    // ported from: ImpactAreaStageDamage.cs private static float[] CalculateIncrementOfStages(float[]
    // previousStagesAtStructures, float[] intervalsAtStructures, int interpolatorIndex). Pure,
    // static. Needed by Task 7's InterpolateBetweenProfiles.
    static std::vector<float> calculate_increment_of_stages(const std::vector<float>& previous_stages_at_structures,
                                                              const std::vector<float>& intervals_at_structures,
                                                              int interpolator_index) {
        std::vector<float> stages(intervals_at_structures.size());
        for (std::size_t m = 0; m < stages.size(); ++m) {
            stages[m] =
                previous_stages_at_structures[m] + intervals_at_structures[m] * static_cast<float>(interpolator_index);
        }
        return stages;
    }

   private:
    // ported from: ImpactAreaStageDamage.cs private void EstablishAggregationStages().
    void establish_aggregation_stages() {
        stage_frequency_ = identify_central_stage_frequency_at_index_location();
        identify_min_and_max_stage_with_uncertainty();
        set_coordinate_quantity();
    }

    // ported from: ImpactAreaStageDamage.cs private PairedData
    // IdentifyCentralStageFrequencyAtIndexLocation(). Returns std::nullopt everywhere the C# method
    // falls through to its final `return null;` (analytical-flow-frequency-without-discharge-stage;
    // graphical-frequency-not-using-stages-without-discharge-stage; no frequency function at all).
    // See class comment's SEVERANCES note for the one branch (analytical flow frequency WITH a
    // discharge-stage function) that throws instead of computing, because it needs the unported
    // ContinuousDistribution::to_coordinates().
    std::optional<PairedData> identify_central_stage_frequency_at_index_location() const {
        constexpr long kFakeIterationNumber = 1;  // "...NotUsedInThisPartOfTheComputeBecauseItIsDeterministic"
        constexpr bool kDeterministic = true;
        if (analytical_flow_frequency_ != nullptr) {
            if (discharge_stage_ != nullptr) {
                throw std::logic_error(
                    "ImpactAreaStageDamage: the analytical-flow-frequency central-stage-frequency "
                    "path requires ContinuousDistribution::to_coordinates(exceedance=false), which "
                    "is not ported (see continuous_distribution.hpp's DONE_WITH_CONCERNS note)");
            }
            // C#: outer `if` taken, inner `if` not -- falls out of the whole if/else-if chain to the
            // trailing `return null;` without evaluating the `else if (_GraphicalFrequency != null)`
            // branch at all.
            return std::nullopt;
        }
        if (graphical_frequency_ != nullptr) {
            if (graphical_frequency_->graphical_distribution_with_less_simple().using_stages_not_flows()) {
                return graphical_frequency_->sample_paired_data(0.5);
            }
            if (discharge_stage_ != nullptr) {
                PairedData flow_frequency_paired_data =
                    graphical_frequency_->sample_paired_data(kFakeIterationNumber, kDeterministic);
                if (unregulated_regulated_ != nullptr) {
                    flow_frequency_paired_data =
                        unregulated_regulated_->sample_paired_data(kFakeIterationNumber, kDeterministic)
                            .compose(flow_frequency_paired_data);
                }
                return discharge_stage_->sample_paired_data(kFakeIterationNumber, kDeterministic)
                    .compose(flow_frequency_paired_data);
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    // ported from: ImpactAreaStageDamage.cs private void IdentifyMinAndMaxStageWithUncertainty().
    // The two C# "no function found" ErrorMessage/ReportMessage branches are replaced with thrown
    // std::runtime_error carrying the same message text (see class comment's MessageReport
    // severance note).
    void identify_min_and_max_stage_with_uncertainty() {
        if (analytical_flow_frequency_ != nullptr) {
            if (discharge_stage_ != nullptr) {
                PairedData min_stages_on_rating = discharge_stage_->sample_paired_data(kMinProbability);
                PairedData max_stages_on_rating = discharge_stage_->sample_paired_data(kMaxProbability);

                double min_flow = analytical_flow_frequency_->inverse_cdf(kMinProbability);
                double max_flow = analytical_flow_frequency_->inverse_cdf(kMaxProbability);

                if (unregulated_regulated_ != nullptr) {
                    min_flow = unregulated_regulated_->sample_paired_data(kMinProbability).f(min_flow);
                    max_flow = unregulated_regulated_->sample_paired_data(kMaxProbability).f(max_flow);
                }

                min_stage_for_area_ = min_stages_on_rating.f(min_flow);
                max_stage_for_area_ = max_stages_on_rating.f(max_flow);
            } else {
                throw_missing_discharge_stage_error();
            }
        } else if (graphical_frequency_ != nullptr) {
            if (graphical_frequency_->graphical_distribution_with_less_simple().using_stages_not_flows()) {
                PairedData min_stages = graphical_frequency_->sample_paired_data(kMinProbability);
                min_stage_for_area_ = min_stages.yvals().front();
                PairedData max_stages = graphical_frequency_->sample_paired_data(kMaxProbability);
                max_stage_for_area_ = max_stages.yvals().back();
            } else {
                if (discharge_stage_ != nullptr) {
                    PairedData min_flows = graphical_frequency_->sample_paired_data(kMinProbability);
                    double min_flow = min_flows.yvals().front();
                    PairedData max_flows = graphical_frequency_->sample_paired_data(kMaxProbability);
                    double max_flow = max_flows.yvals().back();

                    if (unregulated_regulated_ != nullptr) {
                        min_flow = unregulated_regulated_->sample_paired_data(kMinProbability).f(min_flow);
                        max_flow = unregulated_regulated_->sample_paired_data(kMaxProbability).f(max_flow);
                    }

                    PairedData min_stages = discharge_stage_->sample_paired_data(kMinProbability);
                    PairedData max_stages = discharge_stage_->sample_paired_data(kMaxProbability);

                    min_stage_for_area_ = min_stages.f(min_flow);
                    max_stage_for_area_ = max_stages.f(max_flow);
                } else {
                    throw_missing_discharge_stage_error();
                }
            }
        } else {
            throw_no_frequency_function_error();
        }
    }

    // ported from: ImpactAreaStageDamage.cs private void SetCoordinateQuantity().
    //
    // Convert.ToInt32(Math.Ceiling(x)) NOTE: Math.Ceiling always returns a double that is already
    // mathematically integral (no fractional part, for any finite x whose ceiling fits a double's
    // 52-bit mantissa), so Convert.ToInt32's banker's-rounding tie-break rule never actually applies
    // here -- there is no ".5" fractional value left to round. `static_cast<int>(std::ceil(x))` is
    // therefore a faithful transcription (a plain truncating cast of an already-integral value), NOT
    // a place where C#/C++ rounding semantics could diverge.
    void set_coordinate_quantity() {
        const PairedData& stage_frequency = stage_frequency_.value();
        const std::vector<double>& probabilities = hydraulics_.profile_probabilities();

        // set bottom coordinate quantity
        double stage_at_aep_of_most_frequent_hydraulics_profile = stage_frequency.f(1.0 - probabilities.front());
        double range_of_stages_at_bottom = stage_at_aep_of_most_frequent_hydraulics_profile - min_stage_for_area_;
        bottom_extrapolation_points_ = static_cast<int>(std::ceil(range_of_stages_at_bottom / kFeetPerCoordinate));
        if (bottom_extrapolation_points_ < kMinimumExtrapolationCoordinates) {
            bottom_extrapolation_points_ = kMinimumExtrapolationCoordinates;
        }

        // set middle coordinate quantity
        double stage_at_aep_of_least_frequent_hydraulics_profile = stage_frequency.f(1.0 - probabilities.back());
        double middle_range =
            stage_at_aep_of_least_frequent_hydraulics_profile - stage_at_aep_of_most_frequent_hydraulics_profile;
        central_interpolation_points_ = static_cast<int>(
            std::ceil((middle_range / kFeetPerCoordinate) / static_cast<double>(probabilities.size() - 1)));
        if (central_interpolation_points_ < kMinimumInterpolationCoordinates) {
            central_interpolation_points_ = kMinimumInterpolationCoordinates;
        }

        // set top coordinate quantity
        double range_of_stages_at_top = max_stage_for_area_ - stage_at_aep_of_least_frequent_hydraulics_profile;
        top_extrapolation_points_ = static_cast<int>(std::ceil(range_of_stages_at_top / kFeetPerCoordinate));
        if (top_extrapolation_points_ < kMinimumExtrapolationCoordinates) {
            top_extrapolation_points_ = kMinimumExtrapolationCoordinates;
        }
    }

    // SEVERANCE helpers (see class comment's MessageReport note): both messages transcribed verbatim
    // from the C# ErrorMessage text (minus the trailing Environment.NewLine, not meaningful to an
    // exception message).
    [[noreturn]] static void throw_missing_discharge_stage_error() {
        throw std::runtime_error(
            "A stage-discharge function must accompany a flow-frequency function but no such "
            "function was found. Stage-damage compute aborted");
    }
    [[noreturn]] static void throw_no_frequency_function_error() {
        throw std::runtime_error(
            "At this time, HEC-FDA does not allow a stage-damage compute without a frequency "
            "function. Stage-damage compute aborted");
    }

    // Non-owning: see class comment's OWNERSHIP note. Nullable, mirroring the C#'s nullable
    // reference fields.
    ContinuousDistribution* analytical_flow_frequency_;
    GraphicalUncertainPairedData* graphical_frequency_;
    UncertainPairedData* discharge_stage_;
    UncertainPairedData* unregulated_regulated_;

    int impact_area_id_;
    int analysis_year_;
    Inventory inventory_;
    HydraulicProfiles hydraulics_;

    // ported from: ImpactAreaStageDamage.cs's `_ConvergenceCriteria` field initializer
    // (minIterations: 1000, maxIterations: 5000). Not read by any geometry method in this task --
    // Task 7's Compute() is the first consumer (Inventory::generate_random_numbers /
    // CreateConsequenceDistributionResults). Declared here now so the ctor's shape doesn't change
    // again in Task 7.
    hecfda::statistics::ConvergenceCriteria convergence_criteria_{1000, 5000};

    std::optional<PairedData> stage_frequency_;
    double min_stage_for_area_ = 0.0;
    double max_stage_for_area_ = 0.0;
    int bottom_extrapolation_points_ = 0;
    int central_interpolation_points_ = 0;
    int top_extrapolation_points_ = 0;
};

}  // namespace stage_damage
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_STAGE_DAMAGE_IMPACT_AREA_STAGE_DAMAGE_HPP
