# HEC-FDA Port — Phase 4: Stage-Damage (with its consequence-binning metrics substrate) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Port the `HEC.FDA.Model/stageDamage` compute end-to-end onto the Phase 1-3 core — the aggregated stage-damage function that turns a structure inventory + hydraulic stage profiles + a frequency relationship into per-asset-category stage-damage `UncertainPairedData`. Because that compute is built on the consequence-binning metrics types and the Phase-3-severed `Inventory.ComputeDamages`, this phase pulls in that dependency-closed metrics substrate too. Validated against the fully-deterministic `TractableStageDamageTests` oracle plus the two pure static stage-extrapolation helpers.

**Architecture:** New headers under `core/include/hecfda/model/metrics/` (the consequence-binning subset) and `core/include/hecfda/model/stage_damage/`, each mirroring its C# file with a `// ported from: <path> @ <sha>` header. The disk-reading `HydraulicDataset` is severed and replaced by a caller-supplied hydraulics input `(vector<double> probabilities, vector<vector<float>> wsesByProfile)` plus the ported pure-numeric `CorrectDryStructureWSEs`. `Inventory::compute_damages` (severed in Phase 3) is added. Oracle values live only in `fixtures/`; the four runners (C++ doctest, R testthat, Python pytest, dotnet emitter) validate them.

**Tech Stack:** C++17 (doctest), cpp11 (R), pybind11 (Python), dotnet gate (net9 build on net10 runtime via `DOTNET_ROLL_FORWARD=Major`).

**Branch:** `phase4` (off `main` @ the Phase-3 merge, commit `74c50a6`). Upstream pinned `upstream/HEC-FDA` @ `f63682a86a30dc306a105689714a92bfd95956c5`.

## Global Constraints

- **C++17 only.** No C++20. Self-contained core; no external C++ deps (no Eigen/Boost/threads-for-correctness).
- **Structural mirroring:** each ported file opens with `// ported from: <upstream-path> @ f63682a86a30dc306a105689714a92bfd95956c5`; mirror the C# method layout/order; transcribe numeric logic verbatim; reproduce upstream bugs (do not "fix"), documenting each in a header comment.
- **Portability:** never `M_PI` (use `hecfda::kPi`); never a namespace alias `gamma`/`stat`; `-Wall/-Wextra` non-MSVC only (already in `core/CMakeLists.txt`).
- **FP-contraction parity (standing invariant):** `-ffp-contract=off` is already set project-wide (core CMake, R Makevars/Makevars.win, Python CMake). Do NOT remove or scope it down. The stage-interpolation and histogram bin-width arithmetic is FP-sensitive.
- **No hardcoded oracle values in test code.** Oracles live in `fixtures/*.json`, sourced from the upstream `HEC.FDA.ModelTest` stage-damage tests, and **captured from real C# via the dotnet gate — never hand-derived**. Write `"expected": "PIN"` first, then replace with the gate-emitted value in the pin step. Upstream test literals (the tractable curves `{0,0,30,60,90,120,150,180}` / `{0,0,0,0,84,168,252,336}`, the extrapolation helpers) may be used as cross-checks.
- **Namespaces:** `hecfda::model::metrics`, `hecfda::model::stage_damage`.
- **Determinism scope:** the oracle path is fully deterministic (`compute_is_deterministic = true`, all inputs `Deterministic`). RNG is still generated (`Inventory::generate_random_numbers`) but not consumed on the deterministic path, so the seeded-RNG-parity concern is dormant here — but keep `-ffp-contract=off` for the arithmetic. Do NOT add a seeded-MC fixture this phase (deferred to Phase 5's EAD compute).
- **Threading severed:** C# `Utility.Parallel.SmartFor` in `Inventory.ComputeDamages` is replaced by a serial loop (identical results; the port has no threading). Document.
- **Reuse Phase 1-3:** histograms (`statistics::histograms::DynamicHistogram`), `ConvergenceCriteria`, all distributions, `UncertainPairedData`/`PairedData`/`CurveMetaData`/`GraphicalUncertainPairedData`, `Structure`/`Inventory`/`OccupancyType`/`DeterministicOccupancyType`.
- **Commits:** SSH-signed; identity `Cam Bracken <cameron.bracken@pm.me>`; conventional-commit; no `Co-Authored-By`; push only when asked.
- **ABI (R):** after a core class-layout change, `R CMD INSTALL --preclean hecfdar`; after editing `hecfdar/src/*.cpp`, re-run `cpp11::cpp_register("hecfdar")`. **Python dev venv:** `~/venv/hecfdapy`.
- **Definition of done per task:** `ctest` green; `python3 tools/verify_oracles.py` green (gate count strictly increases); C++ fixture runner exercises the new fixture; commit. (R/Python runner wiring for the representative subset is the final task.)

## Scope: severed at the port boundary (numeric only)

Port the numeric compute; DO NOT port these (document each severance):

| Area | Sever | Replace with / defer to |
|------|-------|--------------------------|
| `HydraulicDataset` / `HydraulicProfile` / RAS ingest | all disk I/O, TIFF/HDF sampling, `PointMs` sampling, XML | caller-supplied `(vector<double> probabilities, vector<vector<float>> wsesByProfile)` + ported `CorrectDryStructureWSEs` |
| `AggregatedConsequencesBinned` | `WriteToXML`/`ReadFromXML`, `ConvertToSingleEmpiricalDistributionOfConsequences` (quantile + `Empirical`) | Phase 5 reporting |
| `StudyAreaConsequencesBinned` | `WriteToXML`/`ReadFromXML`, `ConvertToStudyAreaConsequencesByQuantile`, `GetAggregateEmpiricalDistribution`, the `SampleMeanDamage`/`GetConsequenceResult` aggregation region, MVVM `ValidationErrorLogger`/`MessageHub`/`ReportMessage` (error-path logging in `GetSpecificHistogram`) | Phase 5 reporting; error logging → a plain throw or documented no-op |
| `ImpactAreaStageDamage` | `ProduceImpactAreaStructureDetails`/`*ToStrings` (CSV), MVVM `MessageReport`/`ReportMessage`, `ProgressReporter`/`Stopwatch` | Phase 5 CSV; progress → no-op |
| `ScenarioStageDamage` | `ProduceStructureDetails` (CSV), `GetErrorMessages` (optional), `ProgressReporter`/`Stopwatch` | Phase 5 CSV |
| EAD-level metrics (`ImpactAreaScenarioResults`, `ScenarioResults`, `AlternativeResults`, `Threshold`, `PerformanceByThresholds`, quantile types) | NOT ported this phase | Phase 5 |

## File Structure

```
core/include/hecfda/model/metrics/
  consequence_type.hpp              # Task 1: enums ConsequenceType + RiskType
  consequence_result.hpp            # Task 1: ConsequenceResult POCO + IncrementConsequence
  consequence_extensions.hpp        # Task 4: FilterByCategories (Binned overload only)
  aggregated_consequences_binned.hpp# Task 3
  study_area_consequences_binned.hpp# Task 4
core/include/hecfda/model/structures/inventory.hpp   # Task 2: add compute_damages + aggregate_results
core/include/hecfda/model/stage_damage/
  hydraulic_profiles.hpp            # Task 5: caller-supplied WSE arrays + CorrectDryStructureWSEs
  impact_area_stage_damage.hpp      # Tasks 6,7
  scenario_stage_damage.hpp         # Task 8
core/tests/test_fixtures.cpp        # each task: loader + bespoke dispatch target
fixtures/metrics/*.json             # Tasks 1,3,4
fixtures/stage_damage/*.json        # Tasks 5,6,7,8
tools/oracle_emitter/HecFdaOracleEmitter.csproj  # each task: <Compile Include> (patched where needed)
tools/oracle_emitter/patched/       # patched copies (MVVM/XML/hydraulics/spatial severance)
tools/oracle_emitter/Program.cs     # each task: Eval* + target-switch case
hecfdar/src/*.cpp, hecfdapy/src/**  # Task 9: bind representative subset
```

**Emitter note (every task):** the emitter subset-compiles the minimal source closure. Add each new `.cs` via `<Compile Include>`. Where a file drags in MVVM messaging, XML/`System.Xml.Linq`, `Statistics.Distributions.Empirical`-only-in-reporting, quantile types, hydraulics disk I/O, spatial, or `Utility.Parallel`/`Utility.Progress`, add a **patched copy** under `tools/oracle_emitter/patched/` that strips/stubs exactly those (following the established `patched/CurveMetaData.cs`/`patched/OccupancyType.cs`/`patched/Structure.cs`/`patched/Inventory.cs` precedent) and `<Compile Include>` the patched copy instead. Keep every numeric line verbatim. **Task 2 must extend the EXISTING `patched/Inventory.cs` to re-add `ComputeDamages`/`AggregateResults`** (Phase 3 stripped them) — with `SmartFor` replaced by a serial `for`.

---

### Task 1: metrics enums + `ConsequenceResult`

**Files:** Create `core/include/hecfda/model/metrics/consequence_type.hpp` (enums), `core/include/hecfda/model/metrics/consequence_result.hpp`; Create `fixtures/metrics/consequence_result.json`; Modify `test_fixtures.cpp` (loader + `consequence_result` target), csproj (+`patched/` if needed), `Program.cs` (`EvalConsequenceResult` + `case`).

**Interfaces:**
- Produces: `enum class ConsequenceType { UNASSIGNED, Damage, LifeLoss, All }`; `enum class RiskType { Fail, Non_Fail, Total, Unassigned }`; `ConsequenceResult` — ctors `ConsequenceResult()` (category "unassigned", `is_null()==true`) and `ConsequenceResult(std::string damage_category)` (`is_null()==false`); fields `structure_damage/content_damage/vehicle_damage/other_damage` (double, default 0), `damaged_structures_quantity/damaged_contents_quantity/damaged_others_quantity/damaged_vehicles_quantity` (int, default 0), `damage_category` (string), `is_null` (bool); `void increment_consequence(double structure_damage, double content_damage=0, double vehicle_damage=0, double other_damage=0)` (adds each to its total; for each value `> 0`, +1 to the matching quantity counter); `bool equals(const ConsequenceResult&) const`.

- [ ] **Step 1: Read the source.** `upstream/HEC-FDA/HEC.FDA.Model/metrics/ConsequenceResult.cs` (81), `ConsequenceTypes.cs`, `RiskType.cs`. `ConsequenceResult` is a plain POCO with zero external deps. Transcribe verbatim, incl. the `> 0`-guarded quantity increments.

- [ ] **Step 2: Write the failing fixture.** `fixtures/metrics/consequence_result.json`, `"target": "consequence_result"`. Cases exercising `increment_consequence` accumulation: construct `ConsequenceResult("dc")`, apply a sequence of `increment_consequence(s,c,v,o)` calls, then assert the four damage totals + four quantity counters. Assertions dispatch methods like `structure_damage`, `damaged_structures_quantity` after N increments. Use `"expected":"PIN"` (gate-captured); the arithmetic is exact so also assert `mode:"exact"` where integer/exact-sum.

- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalConsequenceResult` builds `new ConsequenceResult(dc)`, applies the fixture's increment list, dispatches the accessor. Add `ConsequenceResult.cs` (likely unpatched — no external deps) + `case "consequence_result"`.

- [ ] **Step 4: Implement** `consequence_type.hpp` + `consequence_result.hpp`; add loader + `consequence_result` bespoke dispatch in `test_fixtures.cpp`.

- [ ] **Step 5: Pin + verify.** `ctest` + gate green; gate count up.

- [ ] **Step 6: Commit** `feat(metrics): port ConsequenceResult + consequence enums + fixtures`.

---

### Task 2: `Inventory::compute_damages` (+ `aggregate_results`)

**Files:** Modify `core/include/hecfda/model/structures/inventory.hpp` (add methods); Create `fixtures/stage_damage/inventory_compute_damages.json`; Modify `test_fixtures.cpp`, `tools/oracle_emitter/patched/Inventory.cs` (re-add the two methods), `Program.cs`.

**Interfaces:**
- Consumes: Task 1 `ConsequenceResult`, Phase-3 `Structure::compute_damage`, `DeterministicOccupancyType`.
- Produces: `std::vector<ConsequenceResult> Inventory::compute_damages(const std::vector<std::vector<float>>& wses, int analysis_year, const std::string& damage_category, const std::vector<DeterministicOccupancyType>& deterministic_occupancy_types)` — one `ConsequenceResult` per water-surface profile; `std::vector<ConsequenceResult> aggregate_results(...)` (private helper).

- [ ] **Step 1: Read the source.** `Inventory.cs` `ComputeDamages` + `AggregateResults` (the Phase-3 port severed these). Transcribe verbatim EXCEPT: (a) replace `Utility.Parallel.SmartFor(nStruc, ...)` with a serial `for` over structures (identical results); (b) keep the mutable scratch buffers (`_invertedWSEL`, the four `_*ParallelCollection`, `_occTypeIndices`) or a clean equivalent — mirror the structure-major inversion. **REPRODUCE THE FAITHFUL QUIRK:** `ComputeDamages` stores `_otherParallelCollection[j,i] = vehicleDamage` and `_vehicleParallelCollection[j,i] = otherDamage` (other/vehicle swapped on store), and calls `AggregateResults(..., _otherParallelCollection, _vehicleParallelCollection)` bound to params `(otherParallelCollection, vehicleParallelCollection)` — so the two swaps compose. Transcribe the exact index/argument wiring; document it as a faithful upstream quirk. The `wse != -9999` skip guard stays.

- [ ] **Step 2: Write the failing fixture.** `fixtures/stage_damage/inventory_compute_damages.json`, target `inventory_compute_damages`. Build a small inventory (the Task-4/Phase-3 occ-type + 2-3 structures) + a `wses` list-of-profiles (`[profile][structure]`), sample occ-types deterministically, call `compute_damages(wses, 9999, damcat, det)`, assert the per-profile `ConsequenceResult` `structure_damage`/`content_damage` (+ the swapped other/vehicle to lock the quirk). `"expected":"PIN"`, gate-captured.

- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** Extend `patched/Inventory.cs` to re-add `ComputeDamages`+`AggregateResults` (serial loop; `ConsequenceResult` now available in the closure via Task 1). `EvalInventoryComputeDamages` builds the inventory, samples occ-types, dispatches. Add `case "inventory_compute_damages"`.

- [ ] **Step 4: Implement** the two methods in `inventory.hpp`; loader + dispatch in `test_fixtures.cpp`.

- [ ] **Step 5: Pin + verify.** `ctest` + gate green.

- [ ] **Step 6: Commit** `feat(structures): port Inventory.ComputeDamages/AggregateResults (faithful other/vehicle swap) + fixtures`.

---

### Task 3: `AggregatedConsequencesBinned`

**Files:** Create `core/include/hecfda/model/metrics/aggregated_consequences_binned.hpp`; Create `fixtures/metrics/aggregated_consequences_binned.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Phase-1 `statistics::histograms::DynamicHistogram`/`IHistogram`, `ConvergenceCriteria`, Task-1 enums.
- Produces: `AggregatedConsequencesBinned` — the compute ctor `(std::string damage_category, std::string asset_category, ConvergenceCriteria, int impact_area_id, ConsequenceType, RiskType = RiskType::Fail)` (defers histogram construction, `histogram_not_constructed_ = true`); `void add_consequence_realization(double damage_realization, long iteration = 1, int damaged_elements_count = 0)` (stages into `temp_results_[iteration]`/`temp_counts_[iteration]`); `void put_data_into_histogram()` (build both histograms from staged range then add observations; clear temp); `double sample_mean_expected_annual_consequences() const`; `double consequence_exceeded_with_probability_q(double) const`; `double quantity_exceeded_with_probability_q(double) const`; convergence accessors used by Task 4 (`consequence_histogram()` / `remaining-iterations` support). Read `DynamicHistogram`'s API for `DEFAULT_BIN_WIDTH`, `add_observations`, `sample_mean`, `inverse_cdf`, `is_histogram_converged`, `estimate_iterations_remaining`, `histogram_is_zero_valued`.

- [ ] **Step 1: Read the source.** `AggregatedConsequencesBinned.cs` (199). Port the compute ctor + `PutDataIntoHistogram` (bin width = `DEFAULT_BIN_WIDTH` if range < 500 else range/500; quantity histogram width 1), `AddConsequenceRealization`, the sample-mean/exceedance accessors, `Equals`. **SEVER** `WriteToXML`/`ReadFromXML` and `ConvertToSingleEmpiricalDistributionOfConsequences` (quantile + `Empirical`). Inline `utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE` (−999).

- [ ] **Step 2: Write the failing fixture.** `fixtures/metrics/aggregated_consequences_binned.json`, target `aggregated_consequences_binned`. Construct with a `ConvergenceCriteria`, stage a known set of realizations across iterations, `put_data_into_histogram()`, assert `sample_mean_expected_annual_consequences`, `consequence_exceeded_with_probability_q(0.5)`, `quantity_exceeded_with_probability_q(0.5)`. `"expected":"PIN"`.

- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalAggregatedConsequencesBinned` + `case`. Patched copy if XML/quantile drags in (strip those methods).

- [ ] **Step 4: Implement**; loader + `aggregated_consequences_binned` dispatch.

- [ ] **Step 5: Pin + verify.** `ctest` + gate green.

- [ ] **Step 6: Commit** `feat(metrics): port AggregatedConsequencesBinned (histogram staging + convergence) + fixtures`.

---

### Task 4: `StudyAreaConsequencesBinned` + `ConsequenceExtensions`

**Files:** Create `core/include/hecfda/model/metrics/study_area_consequences_binned.hpp`, `consequence_extensions.hpp`; Create `fixtures/metrics/study_area_consequences_binned.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Task 3 `AggregatedConsequencesBinned`, Task 1 `ConsequenceResult`/enums, Phase-2 `UncertainPairedData`/`CurveMetaData`, `utilities` asset-category strings.
- Produces: `StudyAreaConsequencesBinned` — ctor `(std::vector<AggregatedConsequencesBinned>)`; `void add_consequence_realization(const ConsequenceResult&, const std::string& damage_category, int impact_area_id, int iteration)` (splits into 4 asset-category `add_consequence_realization` calls — STRUCTURE/CONTENT/VEHICLE/OTHER); `void put_data_into_histograms()`; `bool results_are_converged(double upper, double lower) const`; `long remaining_iterations(double upper, double lower) const`; `static std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>> to_uncertain_paired_data(const std::vector<double>& x_values, const std::vector<StudyAreaConsequencesBinned>& y_values, int impact_area_id)`. `consequence_extensions.hpp`: `filter_by_categories` (Binned overload only). Define the four asset-category string constants (`STRUCTURE_ASSET_CATEGORY` etc.) from `utilities.StringGlobalConstants`.

- [ ] **Step 1: Read the source.** `StudyAreaConsequencesBinned.cs` (359), `Extensions/ConsequenceExtensions.cs` (Binned overload). Port: the `(List<AggregatedConsequencesBinned>)` ctor + `AddNewConsequenceResultObject`/`AddExistingConsequenceResultObject` guards, the `AddConsequenceRealization(ConsequenceResult, ...)` 4-way split (asset-category strings), `PutDataIntoHistograms`, `ResultsAreConverged` (AND across results), `RemainingIterations`, `ToUncertainPairedData` (per damage×asset category, collect consequence + quantity histograms across x-steps into two `UncertainPairedData` with `CurveMetaData`). **SEVER** `WriteToXML`/`ReadFromXML`, the quantile converters, `GetAggregateEmpiricalDistribution`, the `SampleMeanDamage`/`ConsequenceExceededWithProbabilityQ`/`GetConsequenceResult` aggregation region, and MVVM `ValidationErrorLogger`/`MessageHub`/`ReportMessage` (in `GetSpecificHistogram` the C# emits a Fatal `ErrorMessage` on miss and returns an empty histogram — replace with a documented empty-histogram return or a throw; keep behavior equivalent for the compute path where lookups always hit).

- [ ] **Step 2: Write the failing fixture.** `fixtures/metrics/study_area_consequences_binned.json`, target `study_area_consequences_binned`. Construct the 4-asset-category result list for one damage category + ConvergenceCriteria, feed a sequence of `ConsequenceResult` realizations via `add_consequence_realization`, `put_data_into_histograms()`, then `to_uncertain_paired_data(xValues, [this], impactAreaID)` and assert the resulting damage `UncertainPairedData`'s sampled yvals (deterministic sample). `"expected":"PIN"` (vector mode).

- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalStudyAreaConsequencesBinned` + `case`. Patched copy stripping XML/quantile/MVVM (inherits `ValidationErrorLogger` — reachable transitively, but the XML/quantile methods must be stripped to avoid dragging `AggregatedConsequencesByQuantile`/`Empirical` reporting).

- [ ] **Step 4: Implement** `consequence_extensions.hpp` then `study_area_consequences_binned.hpp`; loader + dispatch.

- [ ] **Step 5: Pin + verify.** `ctest` + gate green.

- [ ] **Step 6: Commit** `feat(metrics): port StudyAreaConsequencesBinned + ConsequenceExtensions + fixtures`.

---

### Task 5: hydraulics-as-arrays input + `CorrectDryStructureWSEs`

**Files:** Create `core/include/hecfda/model/stage_damage/hydraulic_profiles.hpp`; Create `fixtures/stage_damage/correct_dry_structure_wses.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Produces: `HydraulicProfiles` — a caller-supplied input type holding `std::vector<double> probabilities` (descending exceedance) + `std::vector<std::vector<float>> wses_by_profile` (`[profile][structure]`), with a ctor that enforces/asserts descending-probability ordering; `std::vector<double> profile_probabilities() const`; `std::vector<std::vector<float>> get_corrected_wses(const std::vector<float>& ground_elevations) const` applying `correct_dry_structure_wses`; the static/free `correct_dry_structure_wses(std::vector<std::vector<float>>& wses, const std::vector<float>& ground_elevations)`.

- [ ] **Step 1: Read the source.** `upstream/HEC-FDA/HEC.FDA.Model/hydraulics/HydraulicDataset.cs` `GetHydraulicDatasetInFloatsWithProbabilities` (the shape) + `CorrectDryStructureWSEs` (152-190) and the ctor's `Sort()`+`Reverse()` (descending exceedance). Port ONLY the numeric correction: `offsetForDryStructures = 9`, `offsetForBarelyDryStructures = 2`; walk profiles 0..Count-2 correcting against the next profile + ground elevations (dry = `wse < groundElev`; dry-in-current & dry-in-next → `groundElev - 9`; dry-in-current & wet-in-next → `groundElev - 2`); last profile: dry → `groundElev - 9`. **SEVER** all disk I/O / `GetWSE` / `PointMs` / RAS. Document that `HydraulicProfiles` replaces `HydraulicDataset` as the passed-in-arrays boundary.

- [ ] **Step 2: Write the failing fixture.** `fixtures/stage_damage/correct_dry_structure_wses.json`, target `correct_dry_structure_wses`. Input raw `wsesByProfile` (some structures dry in some profiles) + `groundElevations`; assert the corrected matrix (matrix mode). Include a case with a dry-then-wet transition and a last-profile-dry case. `"expected":"PIN"` (matrix), gate-captured against the real (patched) `CorrectDryStructureWSEs`.

- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalCorrectDryStructureWSEs` + `case`. Add a `patched/HydraulicDataset.cs` OR a minimal patched helper exposing just `CorrectDryStructureWSEs` (strip disk/`GetWSE`/`PointMs`/profile classes) — follow the smallest-surface patched precedent.

- [ ] **Step 4: Implement** `hydraulic_profiles.hpp`; loader + `correct_dry_structure_wses` dispatch (matrix).

- [ ] **Step 5: Pin + verify.** `ctest` + gate green.

- [ ] **Step 6: Commit** `feat(stage_damage): hydraulics-as-arrays input + CorrectDryStructureWSEs + fixtures`.

---

### Task 6: `ImpactAreaStageDamage` geometry (aggregation stages)

**Files:** Create `core/include/hecfda/model/stage_damage/impact_area_stage_damage.hpp` (geometry portion + ctor); Create `fixtures/stage_damage/stage_damage_geometry.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Task 5 `HydraulicProfiles`, Phase-3 `Inventory`, Phase-2 `GraphicalUncertainPairedData`/`UncertainPairedData`/`PairedData`, Phase-1 `ContinuousDistribution` (analytical frequency, if used).
- Produces: `ImpactAreaStageDamage` ctor `(int impact_area_id, Inventory inventory, HydraulicProfiles hydraulics, int analysis_year = 9999, /*optional frequency inputs*/ analytical_flow_frequency, graphical_frequency, discharge_stage, unregulated_regulated, bool using_mock_data = false)` running `establish_aggregation_stages()`; the static helpers `static std::vector<float> extrapolate_from_above_at_index_location(const std::vector<float>&, float upper_interval, int step_count)` and `static std::vector<float> extrapolate_from_below_stages_at_index_location(const std::vector<float>&, float interval, int i, int num_interpolated)`; accessors for `compute_stages_at_index_location` output + the coordinate-quantity fields (for the Task-7 compute + fixtures). Move-only if it holds move-only members (Inventory/UncertainPairedData) — mirror and document.

- [ ] **Step 1: Read the source.** `ImpactAreaStageDamage.cs` constructor + `EstablishAggregationStages`, `IdentifyCentralStageFrequencyAtIndexLocation`, `IdentifyMinAndMaxStageWithUncertainty`, `SetCoordinateQuantity`, `ComputeStagesAtIndexLocation`, and the static extrapolation + `CalculateIntervals`/`CalculateIncrementOfStages`/`CalculateLowerIncrementOfStages` helpers. Constants: `MIN_PROBABILITY=0.0001`, `MAX_PROBABILITY=0.9999`, `FEET_PER_COORDINATE=0.25`, `MINIMUM_EXTRAPOLATION_COORDINATES=4`, `MINIMUM_INTERPOLATION_COORDINATES=2`, `ConvergenceCriteria(1000, 5000)`. Replace `_HydraulicDataset.HydraulicProfiles.First()/Last().Probability` and `.Count()` with the `HydraulicProfiles` accessors. `using_mock_data` skips `GetInventoryTrimmedToImpactArea`. **SEVER** MVVM `ReportMessage` (error branches → documented throw/no-op).

- [ ] **Step 2: Write the failing fixture.** `fixtures/stage_damage/stage_damage_geometry.json`, target `stage_damage_geometry`. Two groups: (a) the two pure static helpers — `extrapolate_from_above` (`{5,4,3}`, upper=1, step=5 → `{10,9,8}`) and `extrapolate_from_below` (`{500,400,300}`, interval=1, i=5, num=50 → `{455,355,255}`) — exact/vector, cross-checked against `StageDamageShould`; (b) construct the tractable `ImpactAreaStageDamage` (graphical stage-frequency `{.5,.2,.1,.04,.02,.01,.004,.002}`/`{12..19}`, ERL 50, UsingStagesNotFlows, the tractable inventory, mock hydraulics) and assert `compute_stages_at_index_location(...)` output + `_BottomExtrapolationPoints`/`_CentralInterpolationPoints`/`_TopExtrapolationPoints`. `"expected":"PIN"` where captured; the two static helpers use their exact literals.

- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalStageDamageGeometry` + `case`. Add `patched/ImpactAreaStageDamage.cs` — the heaviest patch: strip hydraulics disk (`_HydraulicDataset`→arrays), MVVM messaging, `ProgressReporter`/`Stopwatch`, CSV `Produce*`/`*ToStrings`. Keep the geometry + (Task-7) compute verbatim. For THIS task the compute body can remain but only the geometry is dispatched.

- [ ] **Step 4: Implement** the geometry portion of `impact_area_stage_damage.hpp`; loader + `stage_damage_geometry` dispatch.

- [ ] **Step 5: Pin + verify.** `ctest` + gate green; the two static helpers match their literals.

- [ ] **Step 6: Commit** `feat(stage_damage): ImpactAreaStageDamage aggregation-stage geometry + fixtures`.

---

### Task 7: `ImpactAreaStageDamage.Compute` (the deterministic stage-damage function)

**Files:** Modify `core/include/hecfda/model/stage_damage/impact_area_stage_damage.hpp` (add the compute loop); Create `fixtures/stage_damage/impact_area_stage_damage.json`; Modify `test_fixtures.cpp`, `patched/ImpactAreaStageDamage.cs`, `Program.cs`.

**Interfaces:**
- Consumes: Task 6 geometry, Task 2 `Inventory::compute_damages`, Task 4 `StudyAreaConsequencesBinned`, Task 3 `AggregatedConsequencesBinned`.
- Produces: `std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>> ImpactAreaStageDamage::compute(bool compute_is_deterministic = false)` — (damage UPDs, quantity-damaged UPDs); `void validate()` + `has_errors()`/`error_level()` (the PropertyValidationHelper aggregation); `produce_zero_damage_functions()` (private).

- [ ] **Step 1: Read the source.** `ImpactAreaStageDamage.cs` `Compute`, `ComputeDamageWithUncertaintyAllCoordinates` (the compute-chunk/iteration/convergence loop), `CreateConsequenceDistributionResults`, `ComputeLowerStageDamage`/`ComputeMiddleStageDamage`/`ComputeUpperStageDamage` + `InterpolateBetweenProfiles`, `DumpDataIntoDistributions`, `IsTheFunctionNotConverged`, `ProduceZeroDamageFunctions`, `Validate`. Replace `_HydraulicDataset.GetHydraulicDatasetInFloatsWithProbabilities(...)` with `hydraulics_.get_corrected_wses(inventory_.get_ground_elevations())` + `hydraulics_.profile_probabilities()`. **SEVER** `ProgressReporter`/`Stopwatch`/MVVM. Keep the convergence loop (`_ConvergenceCriteria` 1000/5000, compute-chunk quantity, the "hard-wire 100 more chunks" on non-convergence) verbatim — on the deterministic path all realizations are identical so it converges immediately, but the loop structure must match.

- [ ] **Step 2: Write the failing fixture.** `fixtures/stage_damage/impact_area_stage_damage.json`, target `impact_area_stage_damage`. Reproduce `TractableStageDamageTests.TrackStageDamageTest` — all 4 InlineData rows (Residential/Commercial × useRegUnreg false/true). For each: build the occ-types (Residential/Commercial deterministic depth-percent curves, CSVR 50/120), the structures (fid 1-4, FFE 14/15/17/18, val 100/200/300/400, ground 12), the mock hydraulics from `(stage1, stage2)`, the graphical frequency (stage-frequency for useRegUnreg=false; flow-frequency+dischargeStage+unregReg for true), `compute(compute_is_deterministic=true)`, then for the target damage-category+asset-category `UncertainPairedData`, `sample_paired_data(1, true)` and assert `f(stage)` at stages `{12..19}`. Expected: Residential-Structure `{0,0,30,60,90,120,150,180}`, Commercial-Content `{0,0,0,0,84,168,252,336}` (abs tol 3 / rel 0.05). `"expected":"PIN"` (gate-captured; the literals are the cross-check).

- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalImpactAreaStageDamage` builds the full tractable scenario, calls `Compute(true)`, dispatches `f(stage)` on the selected result curve. Finish `patched/ImpactAreaStageDamage.cs` (compute body, hydraulics-as-arrays). Add `case`.

- [ ] **Step 4: Implement** the compute loop in `impact_area_stage_damage.hpp`; loader + `impact_area_stage_damage` dispatch.

- [ ] **Step 5: Pin + verify.** `ctest` + gate green; the tractable curves reproduce within tolerance and match the upstream literals.

- [ ] **Step 6: Commit** `feat(stage_damage): ImpactAreaStageDamage.Compute (deterministic stage-damage function) + tractable oracle`.

---

### Task 8: `ScenarioStageDamage`

**Files:** Create `core/include/hecfda/model/stage_damage/scenario_stage_damage.hpp`; Create `fixtures/stage_damage/scenario_stage_damage.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Task 7 `ImpactAreaStageDamage`.
- Produces: `ScenarioStageDamage` ctor `(std::vector<ImpactAreaStageDamage>)`; `std::pair<std::vector<UncertainPairedData>, std::vector<UncertainPairedData>> compute(bool compute_is_deterministic = false)` (concatenate per-impact-area results); `const std::vector<ImpactAreaStageDamage>& impact_area_stage_damages() const`.

- [ ] **Step 1: Read the source.** `ScenarioStageDamage.cs` (95). Port the ctor + `Compute` (loop impact areas, `AddRange` both result lists). **SEVER** `ProduceStructureDetails` (CSV), `GetErrorMessages` (optional — keep a thin version or sever), `ProgressReporter`/`Stopwatch`.

- [ ] **Step 2: Write the failing fixture.** `fixtures/stage_damage/scenario_stage_damage.json`, target `scenario_stage_damage`. Wrap the Task-7 tractable `ImpactAreaStageDamage` (one impact area) in a `ScenarioStageDamage`, `compute(true)`, assert the same Residential-Structure curve at stages `{12..19}` → confirms the outer loop concatenates correctly. `"expected":"PIN"`.

- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalScenarioStageDamage` + `case`. Patched copy stripping CSV/progress.

- [ ] **Step 4: Implement** `scenario_stage_damage.hpp`; loader + dispatch.

- [ ] **Step 5: Pin + verify.** `ctest` + gate green.

- [ ] **Step 6: Commit** `feat(stage_damage): ScenarioStageDamage outer compute loop + fixtures`.

---

### Task 9: R/Python representative subset + closeout

**Files:** Modify `hecfdar/src/*.cpp` (+`cpp11::cpp_register`), `hecfdar/tests/testthat/test-fixtures.R`, `hecfdapy/src/**`, `hecfdapy/tests/test_fixtures.py`; Modify `.claude/CLAUDE.md`, `.claude/PLAN.md`, `.superpowers/sdd/progress.md`, memory file.

**Interfaces:**
- Consumes: everything Tasks 1-8 produced.
- Produces: R/Python bindings + fixture-runner dispatch for a representative subset: `consequence_result` (a metrics leaf) and `impact_area_stage_damage` (the end-to-end deterministic stage-damage compute — the phase's headline result). Document that the other targets traverse the identical binding + compiled core and stay validated in C++ + the gate.

- [ ] **Step 1:** Choose + document the subset (`consequence_result` + `impact_area_stage_damage`) in `.claude/CLAUDE.md`, mirroring the existing R/Python coverage-scope convention.

- [ ] **Step 2:** Add R bindings (`[[cpp11::register]]` `hecfda_consequence_result_*`, `hecfda_impact_area_stage_damage_*`) mirroring the C++ fixture dispatch; `cpp11::cpp_register("hecfdar")`; `R CMD INSTALL --preclean hecfdar`.

- [ ] **Step 3:** Extend `hecfdar/tests/testthat/test-fixtures.R` to load `fixtures/metrics/consequence_result.json` + `fixtures/stage_damage/impact_area_stage_damage.json`; `Rscript -e 'testthat::test_local("hecfdar")'`.

- [ ] **Step 4:** Mirror in Python (pybind11 bindings; `pip install --force-reinstall --no-deps ./hecfdapy`; extend `test_fixtures.py`); `pytest hecfdapy/tests -q`.

- [ ] **Step 5:** If any stage-damage fixture needs a compare mode the R/Python runners lack (matrix mode from Task 5 may be new — check), add it symmetrically to all three runners; otherwise skip.

- [ ] **Step 6: Full four-leg exit gate.** `make test-core`, `make test-r`, `make test-py PYTHON=~/venv/hecfdapy/bin/python`, `make oracles` — all green; record the new gate count. Confirm the Phase-0/2 invariants still hold (`sample_and_integrate(1234)==24.425549382855987`, `rng_digest`) and the Phase-3 structures fixtures still pass.

- [ ] **Step 7: Docs + memory.** Update `.claude/CLAUDE.md` (Status → Phase 4 complete; add a paragraph on `hecfda::model::metrics` consequence-binning + `hecfda::model::stage_damage`; new faithful bugs — the `Inventory.ComputeDamages` other/vehicle swap-on-store; any others found; note the hydraulics-as-arrays boundary + `CorrectDryStructureWSEs`; record the R/Python subset). Update `.claude/PLAN.md` (Phase 4 done, Phase 5 compute+metrics/EAD next). Update the memory file with a concise "Phase 4 COMPLETE" paragraph.

- [ ] **Step 8:** Commit `docs(stage_damage): Phase 4 closeout -- bindings, exit gate, status`. Stop. Do NOT open a PR or merge — the controller runs `superpowers:finishing-a-development-branch` and the user chooses.

---

## Self-Review

- **Spec coverage:** design-spec Phase 4 ("Stage-damage — `ImpactAreaStageDamage`, `ScenarioStageDamage` over paired-data + structures, hydraulic profiles as input arrays") — Task 6+7 cover `ImpactAreaStageDamage`, Task 8 covers `ScenarioStageDamage`, Task 5 the hydraulics-as-arrays boundary. The consequence-binning metrics substrate (Tasks 1,3,4) + `Inventory.ComputeDamages` (Task 2) are the forced dependencies pulled in per the approved scope decision (one-phase E2E). EAD-level metrics/results explicitly deferred to Phase 5.
- **Type consistency:** `ConsequenceResult` (T1) → `Inventory::compute_damages` (T2) → `StudyAreaConsequencesBinned::add_consequence_realization` (T4) → `ImpactAreaStageDamage::compute` (T7) → `ScenarioStageDamage::compute` (T8); `AggregatedConsequencesBinned` (T3) is held by `StudyAreaConsequencesBinned` (T4); `HydraulicProfiles` (T5) feeds T6/T7. Every consumer's signature is declared in its producer task.
- **Oracle discipline:** all expecteds `"PIN"`-then-gate-captured; the tractable curves + the two extrapolation helpers are upstream literals used as cross-checks. Deterministic path only this phase (no seeded MC) — documented.
- **Placeholder scan:** each task has exact paths, the upstream source + line ranges, the fixture inputs with literals, and the emitter/runner wiring. The heaviest patched copy (`ImpactAreaStageDamage.cs`) is split across T6 (geometry) and T7 (compute) with the severance list enumerated. The only deferral is gate-captured expecteds, per project convention.
- **Severance coherence:** the hydraulics disk-ingest, MVVM messaging, XML, quantile/reporting metrics, CSV `Produce*`, and progress/threading are cut at the same boundary across every touched file and its patched emitter copy.
