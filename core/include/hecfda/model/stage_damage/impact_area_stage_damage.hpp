// ported from: upstream/HEC-FDA/HEC.FDA.Model/stageDamage/ImpactAreaStageDamage.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_STAGE_DAMAGE_IMPACT_AREA_STAGE_DAMAGE_HPP
#define HECFDA_MODEL_STAGE_DAMAGE_IMPACT_AREA_STAGE_DAMAGE_HPP
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/aggregated_consequences_binned.hpp"
#include "hecfda/model/metrics/consequence_result.hpp"
#include "hecfda/model/metrics/consequence_type.hpp"
#include "hecfda/model/metrics/study_area_consequences_binned.hpp"
#include "hecfda/model/paired_data/graphical_uncertain_paired_data.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
#include "hecfda/model/stage_damage/hydraulic_profiles.hpp"
#include "hecfda/model/structures/deterministic_occupancy_type.hpp"
#include "hecfda/model/structures/inventory.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
#include "hecfda/statistics/histograms/dynamic_histogram.hpp"
#include "hecfda/statistics/validation.hpp"
namespace hecfda {
namespace model {
namespace stage_damage {

// ported from: ImpactAreaStageDamage.cs. Phase 4 Task 6 ported the GEOMETRY portion: the
// constructor + EstablishAggregationStages (IdentifyCentralStageFrequencyAtIndexLocation,
// IdentifyMinAndMaxStageWithUncertainty, SetCoordinateQuantity, ComputeStagesAtIndexLocation) and
// the pure static/instance stage-interval helpers Task 7's compute methods need
// (ExtrapolateFromAboveAtIndexLocation, ExtrapolateFromBelowStagesAtIndexLocation,
// CalculateIntervals, CalculateIncrementOfStages, CalculateLowerIncrementOfStages). Phase 4 Task 7
// (this task) adds Compute() / ComputeDamageWithUncertaintyAllCoordinates /
// ComputeLower·Middle·UpperStageDamage / ProduceZeroDamageFunctions / Validate() -- see the
// "TASK 7 ADDITIONS" comment further down for the details. ProduceImpactAreaStructureDetails and
// the *ToStrings CSV helpers remain out of scope (see class comment's SEVERANCES section) --
// CSV/text-report generation is severed repo-wide per CLAUDE.md.
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
//     DumpDataIntoDistributions, IsTheFunctionNotConverged): PORTED in Task 7 (see compute() and
//     its private helpers below) -- this bullet described the Task 6 state and is kept for
//     provenance; see the new "Task 7: Compute()" section further down for what changed.
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
// TASK 7 ADDITIONS (Compute() and everything it drives) -- read alongside the class comment above.
//
// validate()/has_errors()/error_level(): C#'s Validate() calls `ValidateProperty(x)` (x.Validate();
// if x.HasErrors, raise this.ErrorLevel to x.ErrorLevel) on all four non-null frequency-input
// pointers, because in C# every one of ContinuousDistribution/GraphicalUncertainPairedData/
// UncertainPairedData derives (transitively, via ValidationErrorLogger) from the MVVM `Validation`
// base. In THIS port only ContinuousDistribution derives hecfda::statistics::Validation (see
// continuous_distribution.hpp) -- GraphicalUncertainPairedData and UncertainPairedData do not (the
// MVVM rule-registration/AddRules() machinery was severed from both when they were ported, see
// their own class comments), so they expose no validate()/has_errors()/error_level() surface here.
// validate() below therefore only calls through to analytical_flow_frequency_ (when non-null); the
// graphical_frequency_/discharge_stage_/unregulated_regulated_ ValidateProperty(...) calls are a
// documented no-op severance. This does not weaken the one behaviorally load-bearing part of
// Validate() -- the Fatal "no frequency function at all" check (`graphical_frequency_ == nullptr
// && analytical_flow_frequency_ == nullptr`) and the Inventory::validate() aggregation are both
// fully ported -- and every fixture case here constructs valid graphical/discharge-stage/
// unregulated-regulated instances, so the severed branches are unreached in practice (same
// "should never happen" shape as several other severances already documented in this port).
//
// compute()'s convergence loop (ComputeDamageWithUncertaintyAllCoordinates): transcribed verbatim,
// INCLUDING the "TODO: hard-wire in an additional 10000 iterations" quirk -- when a compute chunk
// pass finishes and IsTheFunctionNotConverged still returns true, computeChunkQuantity is
// unconditionally reset to the LITERAL value 100 (not `computeChunkQuantity + 100`, not a
// remaining-iterations estimate), so a non-converged run's SECOND-and-later passes always run
// 100 chunks x IterationCount iterations before re-checking, regardless of how close it was to
// converging. `compute_is_deterministic=true` (the only path this task's fixture exercises) makes
// every realization within an iteration numerically IDENTICAL across iterations (deterministic
// sampling bypasses the RNG entirely, see OccupancyType::sample()'s computeIsDeterministic
// branch), so the resulting histogram has zero variance and its confidence half-width is exactly
// 0 -- ResultsAreConverged succeeds after the FIRST compute-chunk pass (min_iterations=1000 /
// IterationCount=100 = 10 chunks), so the hard-wire-100 branch is never actually taken by this
// task's fixture, but is transcribed anyway since a future non-deterministic caller will exercise
// it.
//
// ComputeLower/Middle/UpperStageDamage index arithmetic (transcribed EXACTLY, verified
// self-consistent against create_consequence_distribution_results()'s
// bottom+top+(numProfiles-1)*central sizing): lower writes indices [0, bottom] (bottom+1 entries,
// stage_index 0..bottom_extrapolation_points_ inclusive); middle's own running stage_index starts
// at bottom+1 and, for each of the (numProfiles-1) profile pairs, InterpolateBetweenProfiles
// writes central_interpolation_points_ entries at [stage_index, stage_index+central) then
// stage_index += central -- so middle's LAST written index is bottom + central*(numProfiles-1);
// upper computes its OWN stage_index = bottom + central*(numProfiles-1) (the exact index middle
// last wrote) and then writes at stage_index+i for i=1..top_extrapolation_points_-1 (i starting at
// 1, NOT 0) -- so upper's first write is exactly ONE PAST middle's last write (no gap, no
// overlap), and its last write (i = top-1) lands at bottom+central*(numProfiles-1)+top-1, the
// final valid index. The `i` starting at 1 in ComputeUpperStageDamage (vs. 0 in
// ComputeLowerStageDamage) is not a typo -- it is what makes the boundary arithmetic work out.
//
// reset_structure_water_index_tracking(): called on inventory_and_water_coupled.first (the
// damage-category-trimmed Inventory) once per chunk iteration, immediately after
// Lower/Middle/Upper -- see inventory.hpp's reset_structure_water_index_tracking() for why this is
// numerically load-bearing (Structure's sequential-search cursor), not a no-op formality.
//
// produce_zero_damage_functions()'s DynamicHistogram reconstruction: C# builds each "zero-valued"
// histogram via `new DynamicHistogram()`, the parameterless "ARBITRARY histogram" placeholder ctor
// (DEFAULT_BIN_WIDTH + a fresh default ConvergenceCriteria(), then ten AddObservationToHistogram(0)
// calls) -- NOT ported to this port's DynamicHistogram (see dynamic_histogram.hpp's
// DONE_WITH_CONCERNS: "a serialization/placeholder helper, not a data-collection surface"). Since
// this task's produced interface needs it, it is reconstructed here from the ctor's own documented
// body via the still-available `(bin_width, ConvergenceCriteria)` ctor +
// add_observations_to_histogram(vector<double>(10, 0.0)), which is behaviorally identical (same
// bin width, same default ConvergenceCriteria, same ten zero-observations) without adding the
// placeholder ctor itself. C# also ALIASES the same four-element `deterministics` IHistogram[]
// array across all four asset-category UncertainPairedData results AND across BOTH Item1/Item2 of
// the returned tuple (`zeroResults.Item1 = zeros; zeroResults.Item2 = zeros;` -- literally the same
// List<UncertainPairedData> object reference); this port's UncertainPairedData owns its histograms
// via unique_ptr (move-only, no aliasing), so each of the eight UncertainPairedData instances here
// (4 asset categories x {damage, quantity}) gets its own independently-constructed-but-
// numerically-identical set of zero histograms instead. No fixture exercises this path (no
// TractableStageDamageTests row has an empty inventory), so it's un-pinned by any oracle value;
// implemented straight from the C# source for interface completeness per the task brief.
class ImpactAreaStageDamage {
   public:
    using PairedData = hecfda::model::paired_data::PairedData;
    using Inventory = hecfda::model::structures::Inventory;
    using GraphicalUncertainPairedData = hecfda::model::paired_data::GraphicalUncertainPairedData;
    using UncertainPairedData = hecfda::model::paired_data::UncertainPairedData;
    using ContinuousDistribution = hecfda::statistics::distributions::ContinuousDistribution;
    using CurveMetaData = hecfda::model::paired_data::CurveMetaData;
    using DeterministicOccupancyType = hecfda::model::structures::DeterministicOccupancyType;
    using ErrorLevel = hecfda::statistics::ErrorLevel;
    using StudyAreaConsequencesBinned = hecfda::model::metrics::StudyAreaConsequencesBinned;
    using AggregatedConsequencesBinned = hecfda::model::metrics::AggregatedConsequencesBinned;
    using ConsequenceType = hecfda::model::metrics::ConsequenceType;

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

    bool has_errors() const { return has_errors_; }
    ErrorLevel error_level() const { return error_level_; }

    // ported from: ImpactAreaStageDamage.cs public void Validate(). See the "TASK 7 ADDITIONS"
    // class-comment note above for why only analytical_flow_frequency_'s ValidateProperty(...) is
    // ported (the only one of the four frequency-input pointer types that has a validate()/
    // has_errors()/error_level() surface in this port) -- the Fatal "no frequency function at all"
    // check and the Inventory::validate() aggregation are both fully ported.
    void validate() {
        has_errors_ = false;
        error_level_ = ErrorLevel::Unassigned;
        if (analytical_flow_frequency_ != nullptr) {
            analytical_flow_frequency_->validate();
            if (analytical_flow_frequency_->has_errors()) {
                has_errors_ = true;
                if (error_level_ < analytical_flow_frequency_->error_level()) {
                    error_level_ = analytical_flow_frequency_->error_level();
                }
            }
        }
        if (graphical_frequency_ == nullptr) {
            if (analytical_flow_frequency_ == nullptr) {
                has_errors_ = true;
                error_level_ = ErrorLevel::Fatal;
            }
        }
        inventory_.validate();
        if (inventory_.error_level() > error_level_) {
            has_errors_ = true;
            error_level_ = inventory_.error_level();
        }
    }

    // ported from: ImpactAreaStageDamage.cs public (List<UncertainPairedData>,
    // List<UncertainPairedData>) Compute(bool computeIsDeterministic, ProgressReporter, Stopwatch).
    // ProgressReporter/Stopwatch parameters SEVERED (MVVM/UI progress plumbing, matching every
    // other Monte-Carlo-loop task in this port). Returns (damage UPDs, quantity-damaged UPDs).
    std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>> compute(
        bool compute_is_deterministic = false) {
        validate();
        std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>> results;
        if (error_level_ >= ErrorLevel::Major) {
            // C#: builds an ErrorMessage/ReportMessage(this, ...) here (SEVERED, matching this
            // class's blanket MVVM severance) then returns the still-empty `results` tuple.
            return results;
        }
        inventory_.generate_random_numbers(convergence_criteria_);
        std::vector<std::string> damage_categories = inventory_.get_damage_categories();
        if (inventory_.structures().empty()) {
            return produce_zero_damage_functions();
        }

        std::vector<float> ground_elevations = inventory_.get_ground_elevations();
        std::vector<std::vector<float>> wses_at_each_structure_by_profile =
            hydraulics_.get_corrected_wses(ground_elevations);
        std::vector<double> profile_probabilities = hydraulics_.profile_probabilities();
        stages_at_index_location_ = compute_stages_at_index_location(profile_probabilities);

        // Run the compute by dam cat to simplify data collection (matches C# comment verbatim).
        for (const std::string& damage_category : damage_categories) {
            std::pair<Inventory, std::vector<std::vector<float>>> inventory_and_water_tupled =
                inventory_.get_inventory_and_water_trimmed_to_damage_category(
                    damage_category, wses_at_each_structure_by_profile);

            // There will be one StudyAreaConsequencesBinned for each stage in the stage-damage
            // function; each holds an AggregatedConsequencesBinned for each asset cat.
            std::vector<StudyAreaConsequencesBinned> consequence_distribution_results =
                compute_damage_with_uncertainty_all_coordinates(damage_category, inventory_and_water_tupled,
                                                                  profile_probabilities, compute_is_deterministic);

            // There should be four UncertainPairedData objects -- one for each asset cat of the
            // given dam cat level compute.
            std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>> temp_results_list =
                StudyAreaConsequencesBinned::to_uncertain_paired_data(stages_at_index_location_,
                                                                        consequence_distribution_results,
                                                                        impact_area_id_);
            for (auto& upd : temp_results_list.first) {
                results.first.push_back(std::move(upd));
            }
            for (auto& upd : temp_results_list.second) {
                results.second.push_back(std::move(upd));
            }
        }
        return results;
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

    // ---- TASK 7: Compute() and its private helpers ---------------------------------------------

    // ported from: ImpactAreaStageDamage.cs private (List<UncertainPairedData>,
    // List<UncertainPairedData>) ProduceZeroDamageFunctions(). See the class comment's "TASK 7
    // ADDITIONS" note for the DynamicHistogram-reconstruction and no-aliasing rationale.
    std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>>
    produce_zero_damage_functions() const {
        using DynamicHistogram = hecfda::statistics::histograms::DynamicHistogram;
        using IDistribution = hecfda::statistics::distributions::IDistribution;
        const std::vector<double>& stage_frequency_yvals = stage_frequency_.value().yvals();
        std::size_t n = stage_frequency_yvals.size();

        auto make_zero_histogram = []() {
            // C#: `new DynamicHistogram()` -- see class comment for why this is reconstructed from
            // the documented ctor body rather than porting the placeholder ctor itself.
            DynamicHistogram histogram(DynamicHistogram::DEFAULT_BIN_WIDTH,
                                        hecfda::statistics::ConvergenceCriteria());
            histogram.add_observations_to_histogram(std::vector<double>(10, 0.0));
            return histogram;
        };

        const std::string damcat = "NO STRUCTURES";
        auto build_one_set = [&]() {
            CurveMetaData structure_metadata("stage-damage function", "stages", "no structures", damcat,
                                              impact_area_id_, hecfda::model::metrics::kStructureAssetCategory);
            CurveMetaData content_metadata("stage-damage function", "stages", "no structures", damcat,
                                            impact_area_id_, hecfda::model::metrics::kContentAssetCategory);
            CurveMetaData other_metadata("stage-damage function", "stages", "no structures", damcat,
                                          impact_area_id_, hecfda::model::metrics::kOtherAssetCategory);
            CurveMetaData vehicle_metadata("stage-damage function", "stages", "no structures", damcat,
                                            impact_area_id_, hecfda::model::metrics::kVehicleAssetCategory);
            auto make_upd = [&](CurveMetaData metadata) {
                std::vector<std::unique_ptr<IDistribution>> histograms;
                histograms.reserve(n);
                for (std::size_t i = 0; i < n; ++i) {
                    histograms.push_back(std::make_unique<DynamicHistogram>(make_zero_histogram()));
                }
                return UncertainPairedData(stage_frequency_yvals, std::move(histograms), std::move(metadata));
            };
            std::vector<UncertainPairedData> zeros;
            zeros.push_back(make_upd(structure_metadata));
            zeros.push_back(make_upd(content_metadata));
            zeros.push_back(make_upd(other_metadata));
            zeros.push_back(make_upd(vehicle_metadata));
            return zeros;
        };

        std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>> zero_results;
        // C#: `zeroResults.Item1 = zeros; zeroResults.Item2 = zeros;` -- the SAME List reference.
        // This port cannot alias (UncertainPairedData is move-only); Item2 gets an independently
        // built but numerically identical set instead (see class comment).
        zero_results.first = build_one_set();
        zero_results.second = build_one_set();
        return zero_results;
    }

    // ported from: ImpactAreaStageDamage.cs private List<StudyAreaConsequencesBinned>
    // CreateConsequenceDistributionResults(string damageCategory). One StudyAreaConsequencesBinned
    // per stage coordinate, each holding the four asset-category AggregatedConsequencesBinned.
    std::vector<StudyAreaConsequencesBinned> create_consequence_distribution_results(
        const std::string& damage_category) const {
        std::vector<StudyAreaConsequencesBinned> consequence_distribution_results_list;
        consequence_distribution_results_list.reserve(stages_at_index_location_.size());
        for (std::size_t i = 0; i < stages_at_index_location_.size(); ++i) {
            std::vector<AggregatedConsequencesBinned> consequence_distribution_result_list;
            consequence_distribution_result_list.reserve(4);
            consequence_distribution_result_list.emplace_back(
                damage_category, hecfda::model::metrics::kStructureAssetCategory, convergence_criteria_,
                impact_area_id_, ConsequenceType::Damage);
            consequence_distribution_result_list.emplace_back(
                damage_category, hecfda::model::metrics::kContentAssetCategory, convergence_criteria_,
                impact_area_id_, ConsequenceType::Damage);
            consequence_distribution_result_list.emplace_back(
                damage_category, hecfda::model::metrics::kOtherAssetCategory, convergence_criteria_,
                impact_area_id_, ConsequenceType::Damage);
            consequence_distribution_result_list.emplace_back(
                damage_category, hecfda::model::metrics::kVehicleAssetCategory, convergence_criteria_,
                impact_area_id_, ConsequenceType::Damage);
            consequence_distribution_results_list.emplace_back(std::move(consequence_distribution_result_list));
        }
        return consequence_distribution_results_list;
    }

    // ported from: ImpactAreaStageDamage.cs private static void DumpDataIntoDistributions(ref
    // List<StudyAreaConsequencesBinned>).
    static void dump_data_into_distributions(
        std::vector<StudyAreaConsequencesBinned>& consequence_distribution_results_list) {
        for (StudyAreaConsequencesBinned& result : consequence_distribution_results_list) {
            result.put_data_into_histograms();
        }
    }

    // ported from: ImpactAreaStageDamage.cs private static bool IsTheFunctionNotConverged(...).
    static bool is_the_function_not_converged(
        std::vector<StudyAreaConsequencesBinned>& consequence_distribution_results) {
        constexpr double kLowerProb = 0.025;
        constexpr double kUpperProb = 0.975;
        for (StudyAreaConsequencesBinned& consequences : consequence_distribution_results) {
            bool is_converged = consequences.results_are_converged(kUpperProb, kLowerProb);
            if (!is_converged) {
                return true;
            }
        }
        return false;
    }

    // ported from: ImpactAreaStageDamage.cs private void ComputeLowerStageDamage(...). Writes
    // consequence realizations into stage indices [0, bottom_extrapolation_points_] (inclusive).
    void compute_lower_stage_damage(std::vector<StudyAreaConsequencesBinned>& consequence_distribution_results,
                                     const std::string& damage_category,
                                     const std::vector<DeterministicOccupancyType>& deterministic_occ_types,
                                     std::pair<Inventory, std::vector<std::vector<float>>>& inventory_and_water_coupled,
                                     const std::vector<double>& profile_probabilities, int this_chunk_iteration) {
        float interval = calculate_lower_increment_of_stages(profile_probabilities);
        std::vector<std::vector<float>> stages_at_all_structures_all_events;
        stages_at_all_structures_all_events.reserve(static_cast<std::size_t>(bottom_extrapolation_points_) + 1);
        for (int stage_index = 0; stage_index < bottom_extrapolation_points_ + 1; ++stage_index) {
            std::vector<float> wses_parallel_to_index_location = extrapolate_from_below_stages_at_index_location(
                inventory_and_water_coupled.second[0], interval, stage_index, bottom_extrapolation_points_);
            stages_at_all_structures_all_events.push_back(std::move(wses_parallel_to_index_location));
        }
        std::vector<hecfda::model::metrics::ConsequenceResult> consequence_results =
            inventory_and_water_coupled.first.compute_damages(stages_at_all_structures_all_events, analysis_year_,
                                                                 damage_category, deterministic_occ_types);
        int i = 0;
        for (const auto& consequence_result : consequence_results) {
            consequence_distribution_results[static_cast<std::size_t>(i)].add_consequence_realization(
                consequence_result, damage_category, impact_area_id_, this_chunk_iteration);
            ++i;
        }
    }

    // ported from: ImpactAreaStageDamage.cs private void InterpolateBetweenProfiles(...). Writes
    // central_interpolation_points_ realizations starting at stage_index.
    void interpolate_between_profiles(std::vector<StudyAreaConsequencesBinned>& consequence_distribution_results,
                                       const std::vector<DeterministicOccupancyType>& occ_types,
                                       const std::vector<float>& previous_hydraulic_profile,
                                       const std::vector<float>& current_hydraulic_profile,
                                       const std::string& damage_category, Inventory& inventory, int stage_index,
                                       int this_chunk_iteration) {
        std::vector<float> intervals_at_structures =
            calculate_intervals(previous_hydraulic_profile, current_hydraulic_profile);
        std::vector<std::vector<float>> stages_all_structures_all_stages;
        stages_all_structures_all_stages.reserve(static_cast<std::size_t>(central_interpolation_points_));
        for (int interpolator_index = 0; interpolator_index < central_interpolation_points_; ++interpolator_index) {
            std::vector<float> stages =
                calculate_increment_of_stages(previous_hydraulic_profile, intervals_at_structures, interpolator_index + 1);
            stages_all_structures_all_stages.push_back(std::move(stages));
        }
        std::vector<hecfda::model::metrics::ConsequenceResult> consequence_results = inventory.compute_damages(
            stages_all_structures_all_stages, analysis_year_, damage_category, occ_types);
        int i = 0;
        for (const auto& consequence_result : consequence_results) {
            consequence_distribution_results[static_cast<std::size_t>(stage_index + i)].add_consequence_realization(
                consequence_result, damage_category, impact_area_id_, this_chunk_iteration);
            ++i;
        }
    }

    // ported from: ImpactAreaStageDamage.cs private void ComputeMiddleStageDamage(...).
    void compute_middle_stage_damage(std::vector<StudyAreaConsequencesBinned>& consequence_distribution_results,
                                      const std::string& damage_category,
                                      const std::vector<DeterministicOccupancyType>& deterministic_occ_types,
                                      std::pair<Inventory, std::vector<std::vector<float>>>& inventory_and_water_coupled,
                                      const std::vector<double>& profile_probabilities, int this_chunk_iteration) {
        std::size_t num_profiles = profile_probabilities.size();
        int stage_index = bottom_extrapolation_points_ + 1;
        for (std::size_t profile_index = 1; profile_index < num_profiles; ++profile_index) {
            interpolate_between_profiles(consequence_distribution_results, deterministic_occ_types,
                                          inventory_and_water_coupled.second[profile_index - 1],
                                          inventory_and_water_coupled.second[profile_index], damage_category,
                                          inventory_and_water_coupled.first, stage_index, this_chunk_iteration);
            stage_index += central_interpolation_points_;
        }
    }

    // ported from: ImpactAreaStageDamage.cs private void ComputeUpperStageDamage(...). `i` starts
    // at 1 (not 0) -- see class comment's index-arithmetic note for why.
    void compute_upper_stage_damage(std::vector<StudyAreaConsequencesBinned>& consequence_distribution_results,
                                     const std::string& damage_category,
                                     const std::vector<DeterministicOccupancyType>& deterministic_occ_types,
                                     std::pair<Inventory, std::vector<std::vector<float>>>& inventory_and_water_coupled,
                                     const std::vector<double>& profile_probabilities, int this_chunk_iteration) {
        const PairedData& stage_frequency = stage_frequency_.value();
        int stage_index = bottom_extrapolation_points_ +
                           central_interpolation_points_ * (static_cast<int>(profile_probabilities.size()) - 1);
        double min_probability = *std::min_element(profile_probabilities.begin(), profile_probabilities.end());
        double stage_at_probability_of_highest_profile = stage_frequency.f(1.0 - min_probability);
        float index_station_upper_stage_delta =
            static_cast<float>(max_stage_for_area_ - stage_at_probability_of_highest_profile);
        float upper_interval = index_station_upper_stage_delta / static_cast<float>(top_extrapolation_points_);

        std::vector<std::vector<float>> stages_all_structures_all_events;
        stages_all_structures_all_events.reserve(static_cast<std::size_t>(top_extrapolation_points_) - 1);
        for (int extrapolator_index = 1; extrapolator_index < top_extrapolation_points_; ++extrapolator_index) {
            std::vector<float> wses_parallel_to_index_location = extrapolate_from_above_at_index_location(
                inventory_and_water_coupled.second.back(), upper_interval, extrapolator_index);
            stages_all_structures_all_events.push_back(std::move(wses_parallel_to_index_location));
        }
        std::vector<hecfda::model::metrics::ConsequenceResult> consequence_results =
            inventory_and_water_coupled.first.compute_damages(stages_all_structures_all_events, analysis_year_,
                                                                 damage_category, deterministic_occ_types);
        int i = 1;
        for (const auto& consequence_result : consequence_results) {
            consequence_distribution_results[static_cast<std::size_t>(stage_index + i)].add_consequence_realization(
                consequence_result, damage_category, impact_area_id_, this_chunk_iteration);
            ++i;
        }
    }

    // ported from: ImpactAreaStageDamage.cs private List<StudyAreaConsequencesBinned>
    // ComputeDamageWithUncertaintyAllCoordinates(...). See class comment's "TASK 7 ADDITIONS" note
    // for the hard-wire-100 quirk and why the deterministic path converges after one chunk pass.
    std::vector<StudyAreaConsequencesBinned> compute_damage_with_uncertainty_all_coordinates(
        const std::string& damage_category,
        std::pair<Inventory, std::vector<std::vector<float>>>& inventory_and_water_tupled,
        const std::vector<double>& profile_probabilities, bool compute_is_deterministic) {
        std::vector<StudyAreaConsequencesBinned> consequence_distribution_results =
            create_consequence_distribution_results(damage_category);
        int iterations_per_compute_chunk = convergence_criteria_.iteration_count();
        int compute_chunk_quantity = convergence_criteria_.min_iterations() / iterations_per_compute_chunk;
        bool stage_damage_functions_are_not_converged = true;

        while (stage_damage_functions_are_not_converged) {
            for (int compute_chunk = 0; compute_chunk < compute_chunk_quantity; ++compute_chunk) {
                for (int this_chunk_iteration = 0; this_chunk_iteration < iterations_per_compute_chunk;
                     ++this_chunk_iteration) {
                    // The only sampling in the aggregated stage-damage compute with uncertainty:
                    // sampling by the overall compute iteration number, so the same random numbers
                    // are retrieved for each iteration. NOTE: sampled off `inventory_` (this
                    // class's own, untrimmed-by-damcat Inventory), matching C#'s
                    // `Inventory.SampleOccupancyTypes(...)` -- NOT the damcat-trimmed
                    // `inventoryAndWaterTupled.Item1` (both share the same occ_types_ map via
                    // Inventory's shared_ptr aliasing, see inventory.hpp, so this makes no
                    // observable difference either way).
                    int this_compute_iteration = compute_chunk * iterations_per_compute_chunk + this_chunk_iteration;
                    std::vector<DeterministicOccupancyType> deterministic_occ_types =
                        inventory_.sample_occupancy_types(this_compute_iteration, compute_is_deterministic);

                    // Iteration counts in the following calls are only used for saving results in
                    // temp results arrays.
                    compute_lower_stage_damage(consequence_distribution_results, damage_category,
                                                deterministic_occ_types, inventory_and_water_tupled,
                                                profile_probabilities, this_chunk_iteration);
                    compute_middle_stage_damage(consequence_distribution_results, damage_category,
                                                 deterministic_occ_types, inventory_and_water_tupled,
                                                 profile_probabilities, this_chunk_iteration);
                    compute_upper_stage_damage(consequence_distribution_results, damage_category,
                                                deterministic_occ_types, inventory_and_water_tupled,
                                                profile_probabilities, this_chunk_iteration);
                    inventory_and_water_tupled.first.reset_structure_water_index_tracking();
                }
                dump_data_into_distributions(consequence_distribution_results);
            }
            stage_damage_functions_are_not_converged = is_the_function_not_converged(consequence_distribution_results);
            if (stage_damage_functions_are_not_converged) {
                // TODO (transcribed from upstream verbatim): hard-wire in an additional 10000
                // iterations for now -- see class comment's "TASK 7 ADDITIONS" note. Literal
                // reassignment to 100, not `+= 100` and not an iterations-remaining estimate.
                compute_chunk_quantity = 100;
            }
        }
        return consequence_distribution_results;
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
    // (minIterations: 1000, maxIterations: 5000). Task 7's compute() is the first consumer
    // (Inventory::generate_random_numbers / create_consequence_distribution_results /
    // compute_damage_with_uncertainty_all_coordinates).
    hecfda::statistics::ConvergenceCriteria convergence_criteria_{1000, 5000};

    std::optional<PairedData> stage_frequency_;
    double min_stage_for_area_ = 0.0;
    double max_stage_for_area_ = 0.0;
    int bottom_extrapolation_points_ = 0;
    int central_interpolation_points_ = 0;
    int top_extrapolation_points_ = 0;

    // ported from: ImpactAreaStageDamage.cs `private double[] _StagesAtIndexLocation` (Task 7:
    // written once per compute() call, read by create_consequence_distribution_results and the
    // final StudyAreaConsequencesBinned::to_uncertain_paired_data call).
    std::vector<double> stages_at_index_location_;

    // ported from: PropertyValidationHelper.HasErrors/.ErrorLevel (Task 7: see validate() above).
    bool has_errors_ = false;
    ErrorLevel error_level_ = ErrorLevel::Unassigned;
};

}  // namespace stage_damage
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_STAGE_DAMAGE_IMPACT_AREA_STAGE_DAMAGE_HPP
