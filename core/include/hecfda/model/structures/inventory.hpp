// ported from: upstream/HEC-FDA/HEC.FDA.Model/structures/Inventory.cs @
// f63682a86a30dc306a105689714a92bfd95956c5
#ifndef HECFDA_MODEL_STRUCTURES_INVENTORY_HPP
#define HECFDA_MODEL_STRUCTURES_INVENTORY_HPP
#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "hecfda/model/metrics/consequence_result.hpp"
#include "hecfda/model/structures/deterministic_occupancy_type.hpp"
#include "hecfda/model/structures/occupancy_type.hpp"
#include "hecfda/model/structures/structure.hpp"
#include "hecfda/statistics/convergence/convergence_criteria.hpp"
#include "hecfda/statistics/validation.hpp"
namespace hecfda {
namespace model {
namespace structures {

// ported from: Inventory.cs. C# is `class Inventory : PropertyValidationHelper,
// IDontImplementValidationButMyPropertiesDo` -- the same base-class shape as OccupancyType (see
// occupancy_type.hpp's class comment), so HasErrors/ErrorLevel are two plain fields set directly
// by validate() below rather than via the Validation-rule machinery Structure uses.
//
// SEVERED (spatial / metrics / persistence / messaging, per CLAUDE.md's structures scope):
//  - Both shapefile/terrain constructors (`Inventory(pointShapefilePath, impactAreaShapefilePath,
//    map, occTypes, priceIndex, projectionFilePath)` and its terrain-path overload) -- need
//    StructureFactory/RASHelper/Projection/GDALAssist/RasMapperLib. Only the in-memory ctor
//    (`Inventory(occTypes, structures, priceIndex)`) is ported.
//  - `GetPointMs()` (needs `PointMs`/`structure.Point`, spatial).
//  - `StructureDetails`/`ProduceDetails` (CSV, needs `Structure.ProduceDetailsHeader` -- already
//    severed in structure.hpp).
//  - `GetErrorsFromProperties()` (both overloads): string-message plumbing; omitted the same way
//    OccupancyType::get_errors_from_properties() is kept but Inventory's two overloads
//    (near-duplicates, differing only by an unused-here impact-area-id message) are not, since
//    nothing in this port's numeric surface consumes them.
//    (`ResetStructureWaterIndexTracking()` was ALSO listed here through Task 6, when it was still
//    a genuine no-op for this port's surface -- Task 7 un-severs it; see
//    reset_structure_water_index_tracking() below for why it turned out to matter.)
//  - `MessageReport`/`ReportMessage` (MVVM messaging, blanket severance).
//
// STORAGE / MOVE-SEMANTICS DECISIONS (Task 6):
//
// 1. `structures_` is `std::vector<Structure>` (Structure is copyable/movable -- it has no
//    move-only markers, unlike OccupancyType). This required a **bugfix** to Structure itself
//    (structure.hpp's `add_rules()`): its seven validation-rule predicates used to capture
//    `[this]`, which is only safe if a Structure is never relocated after construction. Storing
//    multiple Structures in a `std::vector<Structure>` and later calling `validate()` on them --
//    exactly what `Inventory::validate()` and the trim methods below do -- breaks that assumption
//    (`push_back(Structure(...))` move-constructs the vector's element from a short-lived
//    temporary, leaving the old `[this]`-capturing closures dangling; confirmed via ASan
//    stack-use-after-scope). Since every field those predicates check is set once in Structure's
//    ctor and never mutated again, capturing by VALUE instead of by `this` is behaviorally
//    identical and removes the dangling-pointer hazard entirely, making Structure safe to copy,
//    move, or store in a vector that reallocates. This is the correctness prerequisite this task
//    needed, not a Task 6 scope creep: Inventory is the first port code to store more than one
//    Structure together and validate() them.
//
// 2. `occ_types_` is `std::shared_ptr<std::map<std::string, OccupancyType>>`, not a bare
//    `std::map<std::string, OccupancyType>` member. Two reasons:
//      - OccupancyType is move-only (see occupancy_type.hpp), so a bare
//        `std::map<std::string, OccupancyType>` member could not be copied when building a
//        trimmed Inventory from a `const` method (`get_inventory_trimmed_to_impact_area` cannot
//        move out of `*this`).
//      - More importantly, this is the MOST faithful available option: the C# `OccTypes` is a
//        `Dictionary` (reference type), so `GetInventoryTrimmedToImpactArea`'s
//        `new Inventory(OccTypes, ...)` call shares the SAME dictionary object between the parent
//        and the trimmed inventory -- mutating one (e.g. `GenerateRandomNumbers`) is visible
//        through the other. A `shared_ptr` reproduces exactly that aliasing (copying the
//        `shared_ptr` is a cheap refcount bump, not a deep copy), which also makes the brief's
//        "ensure `generate_random_numbers` is called on the inventory that is actually sampled"
//        concern moot here: since the parent and every trimmed/filtered child share one physical
//        map, calling `generate_random_numbers` on ANY of them updates state visible to all.
//    `std::map` (not `unordered_map`) is used both for its **stable node addresses** (map
//    elements, unlike vector elements, never relocate on insertion -- no analog of the
//    Structure-in-a-vector hazard above can occur for OccupancyType) and because it gives a
//    well-defined enumeration order (sorted by key), matching Step 3's `SampleOccupancyTypes`
//    order requirement below.
//
// 3. `SampleOccupancyTypes` iterates `OccTypes.Values`. C# `Dictionary<TKey,TValue>` enumerates
//    the common case in insertion order, but this is NOT a documented/guaranteed contract, so
//    this port makes an explicit, deterministic choice instead: `std::map` (sorted by key).
//    Every fixture case below uses exactly ONE occupancy type, so this ordering choice is
//    unobservable there (the future R/Python distribution-parity work -- if `sample_occupancy_
//    types` gains R/Python bindings with a multi-occ-type inventory -- should keep this in mind:
//    the ordering will be key-sorted, not insertion order, and any such fixture/gate comparison
//    must construct its C# `Dictionary` in the matching sorted-key order for the two sides to
//    agree, or add a `SortedDictionary` on the emitter side).
//
// LOCAL BEHAVIORAL NOTE (a genuine, latent, pre-existing hazard this task does NOT fix, because
// fixing it correctly is out of Task 6's scope -- it lives in Phase 3 Tasks 1-4's files):
// `OccupancyType`'s four value/ratio/first-floor uncertainty-sampler members
// (`structure_value_error_` etc.) are themselves `Validation`-derived and use `[this]`-capturing
// rule predicates (see value_uncertainty.hpp / value_ratio_with_uncertainty.hpp /
// first_floor_elevation_uncertainty.hpp), and `OccupancyType::OccupancyTypeBuilder`'s `with_*`
// methods MOVE-ASSIGN a temporary parameter into `occupancy_type_`'s member slot -- the same
// relocation-after-`add_rules()` shape as the Structure bug fixed above, just one level down.
// Calling `OccupancyType::validate()` after building via the builder chain is therefore only
// confirmed SAFE in the narrow sense that it did not crash under ASan in a manual repro (unlike
// the Structure case, which reliably did); it has NOT been proven free of UB and should not be
// relied on. `Inventory::validate()` below still transcribes the C# aggregation over
// `OccTypes.Values` verbatim (faithfully, including calling `occupancy_type.validate()`), but this
// task's OWN fixture case for `validate_error_level` sidesteps the hazard by using an EMPTY
// `occ_types` map for its "bad structure" and "empty inventory" cases -- neither needs a real
// occupancy type, and this avoids exercising the still-latent OccupancyType-child hazard. A
// follow-up task should apply the same by-value-capture fix used here to
// ValueUncertainty/ValueRatioWithUncertainty/FirstFloorElevationUncertainty before any fixture
// calls `OccupancyType::validate()` on a builder-constructed instance.
class Inventory {
   public:
    using ErrorLevel = hecfda::statistics::ErrorLevel;
    using ConvergenceCriteria = hecfda::statistics::ConvergenceCriteria;

    // ported from: Inventory.cs public Inventory(Dictionary<string, OccupancyType> occTypes,
    // List<Structure> structures, double priceIndex = 1) -- "Builds an inventory from in-memory"
    // (the only ctor this port keeps; see class comment for the two severed shapefile/terrain
    // ctors). Wraps `occ_types` in a fresh shared_ptr -- see class comment point 2 for why OccTypes
    // is shared-by-pointer rather than held by value.
    Inventory(std::map<std::string, OccupancyType> occ_types, std::vector<Structure> structures,
               double price_index = 1)
        : occ_types_(std::make_shared<std::map<std::string, OccupancyType>>(std::move(occ_types))),
          structures_(std::move(structures)),
          price_index_(price_index) {}

    // ported from: Inventory.cs public List<Structure> Structures { get; }.
    const std::vector<Structure>& structures() const { return structures_; }
    // ported from: Inventory.cs public double PriceIndex { get; set; }.
    double price_index() const { return price_index_; }

    // ported from: Inventory.cs public float[] GetGroundElevations().
    std::vector<float> get_ground_elevations() const {
        std::vector<float> result;
        result.reserve(structures_.size());
        for (const auto& structure : structures_) {
            result.push_back(static_cast<float>(structure.ground_elevation()));
        }
        return result;
    }

    // ported from: Inventory.cs internal List<string> GetDamageCategories() -- unique damage
    // categories in first-seen order (transcribed via linear `Contains`-then-`Add`, matching the
    // C# List<string> scan verbatim rather than e.g. using a hash-set, since the C# uses a List).
    std::vector<std::string> get_damage_categories() const {
        std::vector<std::string> unique_damage_categories;
        for (const auto& structure : structures_) {
            const std::string& category = structure.damage_catagory();
            if (std::find(unique_damage_categories.begin(), unique_damage_categories.end(), category) ==
                unique_damage_categories.end()) {
                unique_damage_categories.push_back(category);
            }
        }
        return unique_damage_categories;
    }

    // ported from: Inventory.cs public Inventory GetInventoryTrimmedToImpactArea(int
    // impactAreaFID). See class comment point 2: the trimmed Inventory shares `occ_types_` (the
    // same shared_ptr, refcount-bumped) with `*this`, matching C#'s Dictionary-reference sharing.
    Inventory get_inventory_trimmed_to_impact_area(int impact_area_fid) const {
        std::vector<Structure> filtered_structure_list;
        for (const auto& structure : structures_) {
            if (structure.impact_area_id() == impact_area_fid) {
                filtered_structure_list.push_back(structure);
            }
        }
        return Inventory(occ_types_, std::move(filtered_structure_list), price_index_);
    }

    // ported from: Inventory.cs public (Inventory, List<float[]>)
    // GetInventoryAndWaterTrimmedToDamageCategory(string damageCategory, List<float[]>
    // wsesAtEachStructureByProfile). Filters structures AND the per-profile water-surface-elevation
    // arrays jointly by index (structure i <-> wsesAtEachStructureByProfile[profile][i]), exactly
    // preserving the C# double-loop structure (outer: structures; inner: profiles) rather than
    // transposing.
    std::pair<Inventory, std::vector<std::vector<float>>> get_inventory_and_water_trimmed_to_damage_category(
        const std::string& damage_category, const std::vector<std::vector<float>>& wses_at_each_structure_by_profile)
        const {
        std::vector<Structure> filtered_structure_list;
        std::vector<std::vector<float>> listed_wses_filtered(wses_at_each_structure_by_profile.size());
        for (std::size_t i = 0; i < structures_.size(); ++i) {
            if (structures_[i].damage_catagory() == damage_category) {
                filtered_structure_list.push_back(structures_[i]);
                for (std::size_t j = 0; j < wses_at_each_structure_by_profile.size(); ++j) {
                    listed_wses_filtered[j].push_back(wses_at_each_structure_by_profile[j][i]);
                }
            }
        }
        return {Inventory(occ_types_, std::move(filtered_structure_list), price_index_),
                std::move(listed_wses_filtered)};
    }

    // ported from: Inventory.cs public void GenerateRandomNumbers(ConvergenceCriteria
    // convergenceCriteria). `quantityOfRandomNumbers = Convert.ToInt32(convergenceCriteria.
    // MaxIterations * 2)`: MaxIterations is a C# `int`, so `MaxIterations * 2` is already
    // evaluated as an exact `int` (no fractional part for `Convert.ToInt32`'s banker's-rounding
    // rule to ever apply) -- `max_iterations() * 2` below is plain `int` arithmetic, an identical
    // transcription, not a rounding cast.
    void generate_random_numbers(const ConvergenceCriteria& convergence_criteria) {
        int quantity_of_random_numbers = convergence_criteria.max_iterations() * 2;
        for (auto& occ_type_entry : *occ_types_) {
            occ_type_entry.second.generate_random_numbers(quantity_of_random_numbers);
        }
    }

    // ported from: Inventory.cs public List<DeterministicOccupancyType> SampleOccupancyTypes(long
    // iteration, bool computeIsDeterministic). Iterates `OccTypes.Values` -- see class comment
    // point 3 for the `std::map` (sorted-by-key) enumeration-order decision.
    std::vector<DeterministicOccupancyType> sample_occupancy_types(long iteration, bool compute_is_deterministic) {
        std::vector<DeterministicOccupancyType> deterministic_occupancy_types;
        deterministic_occupancy_types.reserve(occ_types_->size());
        for (auto& occ_type_entry : *occ_types_) {
            deterministic_occupancy_types.push_back(occ_type_entry.second.sample(iteration, compute_is_deterministic));
        }
        return deterministic_occupancy_types;
    }

    // ported from: Inventory.cs public List<ConsequenceResult> ComputeDamages(List<float[]> wses,
    // int analysisYear, string damageCategory, List<DeterministicOccupancyType>
    // deterministicOccupancyType). One `ConsequenceResult` per water-surface-elevation profile
    // (`wses[profile][structure]`), the per-structure damage sum for that profile.
    //
    // FAITHFUL QUIRK (transcribed verbatim -- do NOT "fix"): C# stores into the "other"/"vehicle"
    // scratch collections with the values SWAPPED --
    //   _otherParallelCollection[j, i]   = vehicleDamage;   // note: vehicleDamage, not otherDamage
    //   _vehicleParallelCollection[j, i] = otherDamage;     // note: otherDamage, not vehicleDamage
    // -- and then calls `AggregateResults(..., _otherParallelCollection, _vehicleParallelCollection)`,
    // positionally binding them to AggregateResults' `(otherParallelCollection,
    // vehicleParallelCollection)` parameters, which are in turn read into `IncrementConsequence(
    // structureParallelCollection[j,i], contentParallelCollection[j,i], otherParallelCollection[j,i],
    // vehicleParallelCollection[j,i])` -- whose THIRD positional parameter is actually named
    // `vehicleDamage` and FOURTH `otherDamage` (see ConsequenceResult::increment_consequence's
    // signature). Traced symbol-by-symbol, the store-swap and the AggregateResults argument-position
    // wiring compose to the IDENTITY: the true vehicle value (stored under the "other" name) lands
    // back in increment_consequence's `vehicle_damage` parameter, and the true other value (stored
    // under the "vehicle" name) lands back in `other_damage`. Net effect: `ConsequenceResult::
    // vehicle_damage()`/`other_damage()` end up numerically CORRECT despite the confusingly-swapped
    // intermediate variable names -- two "bugs" that cancel. This is transcribed exactly (mirroring
    // the store step AND the aggregate_results parameter wiring below) rather than collapsed to the
    // equivalent direct call, both for structural-mirroring fidelity (see CLAUDE.md) and so a future
    // upstream fix to ONE side (e.g. renaming the arrays without touching the call site, or vice
    // versa) breaks this port's tests instead of silently diverging. Locked by
    // fixtures/stage_damage/inventory_compute_damages.json, which uses an occupancy type with
    // DISTINCT nonzero vehicle/other depth-percent-damage curves so the wiring is actually observed,
    // not just "both present".
    //
    // DEVIATIONS from C#:
    //  (a) `Utility.Parallel.SmartFor(nStruc, ...)` -> a serial `for` loop over structures. Each
    //      structure's damage/store is independent of every other (writes are index-addressed, no
    //      shared mutable state besides the scratch collections' fixed preallocated shape), so
    //      serial vs. parallel execution order does not affect the result.
    //  (b) The C# `_invertedWSEL`/`_strucParallelCollection`/`_contentParallelCollection`/
    //      `_otherParallelCollection`/`_vehicleParallelCollection`/`_occTypeIndices` are Inventory
    //      MEMBER fields, reused/resized across calls and explicitly marked "NOT SAFE TO CALL IN
    //      PARALLEL" in the C# comment (a mutable-scratch-buffer reuse optimization). This port uses
    //      plain per-call LOCALS instead: every element is written exactly once before being read in
    //      both versions, so there is no cross-call state to reuse, and the serial single-threaded
    //      C++ port has no concurrent-call hazard to guard against either -- locals are simpler and
    //      behavior-preserving.
    std::vector<hecfda::model::metrics::ConsequenceResult> compute_damages(
        const std::vector<std::vector<float>>& wses, int analysis_year, const std::string& damage_category,
        const std::vector<DeterministicOccupancyType>& deterministic_occupancy_types) {
        std::size_t n_pf = wses.size();
        std::size_t n_struc = wses[0].size();

        // invertedWSEL[struc][pf] = wses[pf][struc] (structure-major inversion, matching C#'s
        // `_invertedWSEL[j, i] = pf[j]` loop).
        std::vector<std::vector<float>> inverted_wsel(n_struc, std::vector<float>(n_pf));
        for (std::size_t i = 0; i < n_pf; ++i) {
            const auto& pf = wses[i];
            for (std::size_t j = 0; j < n_struc; ++j) {
                inverted_wsel[j][i] = pf[j];
            }
        }

        std::vector<std::vector<double>> struc_collection(n_pf, std::vector<double>(n_struc, 0.0));
        std::vector<std::vector<double>> content_collection(n_pf, std::vector<double>(n_struc, 0.0));
        std::vector<std::vector<double>> other_collection(n_pf, std::vector<double>(n_struc, 0.0));
        std::vector<std::vector<double>> vehicle_collection(n_pf, std::vector<double>(n_struc, 0.0));

        std::vector<int> occ_type_indices(n_struc);
        for (std::size_t i = 0; i < n_struc; ++i) {
            occ_type_indices[i] = structures_[i].find_occ_type_index(deterministic_occupancy_types);
        }

        // DEVIATION (a): serial over structures, replacing `Utility.Parallel.SmartFor`.
        for (std::size_t i = 0; i < n_struc; ++i) {
            const DeterministicOccupancyType& dt =
                deterministic_occupancy_types[static_cast<std::size_t>(occ_type_indices[i])];
            for (std::size_t j = 0; j < n_pf; ++j) {
                float wse = inverted_wsel[i][j];
                if (wse != -9999) {
                    auto [struct_damage, cont_damage, vehicle_damage, other_damage] =
                        structures_[i].compute_damage(wse, dt, price_index_, analysis_year);
                    struc_collection[j][i] = struct_damage;
                    content_collection[j][i] = cont_damage;
                    // FAITHFUL SWAP -- see method comment above: "other" stores the vehicle value,
                    // "vehicle" stores the other value.
                    other_collection[j][i] = vehicle_damage;
                    vehicle_collection[j][i] = other_damage;
                }
            }
        }

        return aggregate_results(wses, damage_category, struc_collection, content_collection, other_collection,
                                  vehicle_collection);
    }

    // ported from: Inventory.cs internal void ResetStructureWaterIndexTracking(). Task 6's class
    // comment called this a "no numeric effect" severance -- true THEN (nothing in that task's
    // geometry-only surface called compute_damage), but Task 7's compute loop calls
    // Structure::compute_damage repeatedly per Monte Carlo iteration via the sequential-search
    // cursor overload PairedData::f(x, ref index) (see structure.hpp's class comment): that cursor
    // only walks forward and is only correct when queried in ascending depth-above-first-floor
    // order within one pass (lower -> middle -> upper stages, which IS ascending WSE order).
    // Between chunk iterations the pass restarts from the lowest stage, so the cursor MUST be
    // reset back to 1 or it stays stuck high from the previous iteration and interpolates the
    // wrong segment. Un-severed here; ported now that Task 7 is the first real caller.
    void reset_structure_water_index_tracking() {
        for (auto& structure : structures_) {
            structure.reset_index_tracking();
        }
    }

    // ported from: Inventory.cs public void Validate(). Transcribed verbatim, INCLUDING the
    // faithful bug: the OccTypes loop raises `ErrorLevel` when a failing occupancy type's level is
    // higher, but never sets `HasErrors = true` for it (unlike the Structures loop just below,
    // which does) -- so an Inventory can end up with a non-Unassigned ErrorLevel purely from a
    // failing OccupancyType while `has_errors()` still reports false, exactly as C# does.
    void validate() {
        has_errors_ = false;
        error_level_ = ErrorLevel::Unassigned;
        for (auto& occ_type_entry : *occ_types_) {
            OccupancyType& occupancy_type = occ_type_entry.second;
            occupancy_type.validate();
            if (occupancy_type.has_errors()) {
                if (error_level_ < occupancy_type.error_level()) {
                    error_level_ = occupancy_type.error_level();
                }
            }
        }
        for (auto& structure : structures_) {
            structure.validate();
            if (structure.has_errors()) {
                if (structure.error_level() > error_level_) {
                    error_level_ = structure.error_level();
                }
                has_errors_ = true;
            }
        }
        if (structures_.empty()) {
            has_errors_ = true;
            error_level_ = ErrorLevel::Minor;
        }
    }

    bool has_errors() const { return has_errors_; }
    ErrorLevel error_level() const { return error_level_; }

   private:
    // ported from: Inventory.cs Inventory(Dictionary<string, OccupancyType> occTypes,
    // List<Structure> structures, double priceIndex) as reached via GetInventoryTrimmedToImpactArea
    // / GetInventoryAndWaterTrimmedToDamageCategory -- both share the parent's OccTypes reference
    // in C# (assigning the same Dictionary object, not copying it). This private overload takes an
    // already-shared occ_types pointer directly so the trim methods above can reuse it without a
    // deep copy (impossible anyway: OccupancyType is move-only).
    Inventory(std::shared_ptr<std::map<std::string, OccupancyType>> occ_types, std::vector<Structure> structures,
               double price_index)
        : occ_types_(std::move(occ_types)), structures_(std::move(structures)), price_index_(price_index) {}

    // ported from: Inventory.cs public Dictionary<string, OccupancyType> OccTypes { get; set; }.
    // shared_ptr, not a bare map -- see class comment point 2.
    std::shared_ptr<std::map<std::string, OccupancyType>> occ_types_;
    // ported from: Inventory.cs public List<Structure> Structures { get; } = new List<Structure>().
    std::vector<Structure> structures_;
    double price_index_;

    // ported from: PropertyValidationHelper.HasErrors / .ErrorLevel (see occupancy_type.hpp's
    // class comment for why this is folded directly in rather than a shared base class).
    bool has_errors_ = false;
    ErrorLevel error_level_ = ErrorLevel::Unassigned;

    // ported from: Inventory.cs private List<ConsequenceResult> AggregateResults(List<float[]> wses,
    // string damageCategory, List<ConsequenceResult> aggregateConsequenceResults, double[,]
    // structureParallelCollection, double[,] contentParallelCollection, double[,]
    // otherParallelCollection, double[,] vehicleParallelCollection). Parameter names mirror the C#
    // signature (structure/content/other/vehicle) EXACTLY -- see compute_damages' doc comment: the
    // 3rd/4th arguments compute_damages passes in are the swapped collections, and this method's own
    // increment_consequence call reproduces the exact same positional wiring as C#'s
    // IncrementConsequence call, so the two swaps compose the way the class comment documents. The
    // `aggregate_consequence_results` C# out-parameter (built empty by the caller and appended to)
    // is simplified to a plain return value here -- purely a calling-convention difference with no
    // behavioral effect, since C# never reads it before passing it in.
    std::vector<hecfda::model::metrics::ConsequenceResult> aggregate_results(
        const std::vector<std::vector<float>>& wses, const std::string& damage_category,
        const std::vector<std::vector<double>>& structure_parallel_collection,
        const std::vector<std::vector<double>>& content_parallel_collection,
        const std::vector<std::vector<double>>& other_parallel_collection,
        const std::vector<std::vector<double>>& vehicle_parallel_collection) {
        std::vector<hecfda::model::metrics::ConsequenceResult> aggregate_consequence_results;
        aggregate_consequence_results.reserve(wses.size());
        for (std::size_t j = 0; j < wses.size(); ++j) {
            hecfda::model::metrics::ConsequenceResult aggregate_consequence_result(damage_category);
            for (std::size_t i = 0; i < structures_.size(); ++i) {
                aggregate_consequence_result.increment_consequence(
                    structure_parallel_collection[j][i], content_parallel_collection[j][i],
                    other_parallel_collection[j][i], vehicle_parallel_collection[j][i]);
            }
            aggregate_consequence_results.push_back(std::move(aggregate_consequence_result));
        }
        return aggregate_consequence_results;
    }
};

}  // namespace structures
}  // namespace model
}  // namespace hecfda
#endif  // HECFDA_MODEL_STRUCTURES_INVENTORY_HPP
