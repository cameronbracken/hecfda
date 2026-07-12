# HEC-FDA Port — Phase 5: Compute + Metrics (EAD Monte Carlo) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Port `HEC.FDA.Model/compute/ImpactAreaScenarioSimulation` — the Expected-Annual-Damage (EAD) Monte-Carlo engine — end-to-end onto the Phase 1-4 core, together with the EAD-results / performance / threshold / assurance metrics surface it produces. This reintroduces the **seeded** Monte Carlo (per-curve seeds), integrating the frequency function × stage-damage curves into damage-frequency and integrating to EAD, plus system performance (AEP, long-term risk, assurance/conditional-non-exceedance). Validated against the graded `simulationShould` / `PerformanceTest` / `StudyData*ResultsTests` oracles — deterministic pins first, then the seeded-MC benchmarks.

**Architecture:** New headers under `core/include/hecfda/model/metrics/` (the 8-type EAD-results closure) and `core/include/hecfda/model/compute/` (`ImpactAreaScenarioSimulation` + its `SimulationBuilder`), each mirroring its C# file with a `// ported from: <path> @ <sha>` header. The compute consumes stage-damage `UncertainPairedData` curves as **inputs** (via the builder) — it never invokes the stage-damage compute. C# `Parallel.For` severs to a serial loop (each iteration samples by its own index → order-independent → identical results). The analytical-frequency realization goes through `ContinuousDistribution::sample(iteration)` + `bootstrap_to_paired_data` (NOT `ToCoordinates`). Oracle values live only in `fixtures/`; the four runners (C++ doctest, R testthat, Python pytest, dotnet emitter) validate them.

**Tech Stack:** C++17 (doctest), cpp11 (R), pybind11 (Python), dotnet gate (net9 build on net10 runtime via `DOTNET_ROLL_FORWARD=Major`).

**Branch:** `phase5` (off `main` @ the Phase-4 merge, commit `8d3140a`). Upstream pinned `upstream/HEC-FDA` @ `f63682a86a30dc306a105689714a92bfd95956c5`.

## Global Constraints

- **C++17 only.** No C++20. Self-contained core; no external C++ deps; NO threads-for-correctness (serial loop replaces `Parallel.For`).
- **Structural mirroring:** each ported file opens with `// ported from: <upstream-path> @ f63682a86a30dc306a105689714a92bfd95956c5`; mirror the C# method layout/order; transcribe verbatim; reproduce upstream bugs (do not "fix"), documenting each in a header comment.
- **Portability:** never `M_PI` (use `hecfda::kPi`); never a namespace alias `gamma`/`stat`; `-Wall/-Wextra` non-MSVC only.
- **FP-contraction parity (standing invariant):** `-ffp-contract=off` is already set project-wide. Do NOT remove/scope it. The compose/integrate/assurance-integration arithmetic is FP-sensitive.
- **RNG parity (LOAD-BEARING AGAIN this phase):** the seeded MC uses PER-CURVE hard-coded seed constants — `FREQUENCY_SEED=1234`, `FLOW_REGULATION_SEED=2345`, `STAGE_FLOW_SEED=3456`, `EXTERIOR_INTERIOR_SEED=4567`, `SYSTEM_RESPONSE_SEED=5678`, `STAGE_DAMAGE_SEED=6789`, `STAGE_LIFELOSS_SEED=7891` — each drives `hecfda::sampling::DotNetRandom(seed)` (the ported .NET `Random`). `quantity_of_random_numbers = (int)(MaxIterations * 1.25)`. Iteration `n` samples each curve via its pre-generated `random_numbers_[n]` (a SINGLE shared uniform per curve per iteration, applied across all ordinates via `InverseCDF`, then weak-monotonicity-forced). This is exactly `UncertainPairedData::sample_paired_data(iteration, deterministic)` / `GraphicalUncertainPairedData::sample_paired_data(...)` (Phase 2, already validated) + `ContinuousDistribution::sample(iteration)` (Task 5, NEW). NEVER hand-derive a seeded expected value — capture from the gate.
- **No hardcoded oracle values in test code.** Oracles live in `fixtures/*.json`, sourced from the upstream `HEC.FDA.ModelTest` compute tests, captured from real C# via the dotnet gate. Write `"expected": "PIN"` first, then replace in the pin step. Upstream test literals (e.g. `ComputeEAD` = 150000, `ComputeEAD_Iterations` = 121194.5159789352, LP3 EAD = 20.74) are cross-checks.
- **Namespaces:** `hecfda::model::metrics`, `hecfda::model::compute`.
- **Threading severed:** `Parallel.For` → serial `for`; `CancellationToken`/`AggregateException`/`TaskCanceledException` dropped; document.
- **Reuse Phase 1-4:** distributions (Normal/LogPearson3/Uniform/Triangular/…), `ContinuousDistribution`, histograms, `ConvergenceCriteria`, `PairedData`/`UncertainPairedData`/`GraphicalUncertainPairedData`/`CurveMetaData` (compose/multiply/integrate/f/f_inverse/sum_ys_for_given_x), the Phase-4 consequence-binning types (`ConsequenceResult`, `AggregatedConsequencesBinned`, `StudyAreaConsequencesBinned`, `ConsequenceExtensions`).
- **Commits:** SSH-signed; identity `Cam Bracken <cameron.bracken@pm.me>`; conventional-commit; no `Co-Authored-By`; push only when asked.
- **ABI (R):** after a core class-layout change, `R CMD INSTALL --preclean hecfdar`; after editing `hecfdar/src/*.cpp`, re-run `cpp11::cpp_register("hecfdar")`. **Python dev venv:** `~/venv/hecfdapy`.
- **Definition of done per task:** `ctest` green; `python3 tools/verify_oracles.py` green (gate count strictly increases); C++ fixture runner exercises the new fixture; commit. (R/Python runner wiring for the representative subset is the final task.)

## Scope: severed / deferred

| Area | Sever | Defer to |
|------|-------|----------|
| MVVM (`ValidationErrorLogger`/`IProgressReport`/`Rule`/`ReportMessage`/`ErrorMessage`) | messaging + validation base → the port's own validation/error model or documented throw/no-op | — |
| Messaging (`ComputeCompleteMessage`/`ProgressReportEventArgs`/`ReportProgress`/`FrequencyDamageMessage`) | all progress/messaging | — |
| XML (`WriteToXML`/`ReadFromXML`, `[StoredProperty]` reflection) on every metrics type | persistence | Phase 6 / never |
| Threading (`Parallel.For`, `CancellationToken`) | → serial loop | — |
| `ScenarioResults`, `Scenario`, `Alternative*`, the two `*ByQuantile` types | NOT ported | Phase 6 |
| `LifeLoss`/LifeSim | never (out of project scope) — but the `WithStageLifeLoss` builder path + `ConsequenceType::LifeLoss` accumulation IS ported (it's numeric, shares the damage path); the life-loss ORACLE (`ComputeEALL`) is a valid Phase-5 fixture | — |

## Metrics closure (the 8 Phase-5 types, dependency-ordered)

```
AssuranceResultStorage  ─┐
                         ├─ SystemPerformanceResults ─┐
ThresholdEnum ───────────┘                            ├─ Threshold ─ PerformanceByThresholds ─┐
CategoriedPairedData ─ CategoriedUncertainPairedData ─────────────────────────────────────────┼─ ImpactAreaScenarioResults
StudyAreaConsequencesBinned (Phase 4) ────────────────────────────────────────────────────────┘   (compute output)
```

## File Structure

```
core/include/hecfda/model/metrics/
  threshold_enum.hpp                     # Task 1
  assurance_result_storage.hpp           # Task 1
  system_performance_results.hpp         # Task 2
  threshold.hpp                          # Task 3
  performance_by_thresholds.hpp          # Task 3
  categoried_paired_data.hpp             # Task 4
  categoried_uncertain_paired_data.hpp   # Task 4
  impact_area_scenario_results.hpp       # Task 6
core/include/hecfda/statistics/distributions/continuous_distribution_extensions.hpp  # Task 5 (bootstrap_to_paired_data + required_exceedance_probabilities)
core/include/hecfda/model/compute/
  impact_area_scenario_simulation.hpp    # Tasks 7-11 (incl. SimulationBuilder)
core/tests/test_fixtures.cpp             # each task: loader + bespoke dispatch target
fixtures/metrics/*.json                  # Tasks 1-4,6
fixtures/compute/*.json                  # Tasks 5,10,11
tools/oracle_emitter/HecFdaOracleEmitter.csproj  # each task: <Compile Include> (patched where needed)
tools/oracle_emitter/patched/            # patched copies (MVVM/XML/messaging/threading severance)
tools/oracle_emitter/Program.cs          # each task: Eval* + target-switch case
hecfdar/src/*.cpp, hecfdapy/src/**       # Task 12: bind representative subset
```

**Emitter note (every task):** subset-compile the minimal closure; add each `.cs` via `<Compile Include>`. Where a file drags in MVVM messaging, XML/`System.Xml.Linq`/`[StoredProperty]` reflection, `System.Threading`, or messaging, add a **patched copy** under `tools/oracle_emitter/patched/` stripping exactly those (following the established `patched/*.cs` precedent), keeping every numeric line verbatim. `ImpactAreaScenarioSimulation` is the heaviest patch (MVVM base + messaging + `Parallel.For`→serial + `[StoredProperty]`); build it up across Tasks 7-11.

---

### Task 1: `ThresholdEnum` + `AssuranceResultStorage`

**Files:** Create `core/include/hecfda/model/metrics/threshold_enum.hpp`, `assurance_result_storage.hpp`; Create `fixtures/metrics/assurance_result_storage.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Phase-1 `DynamicHistogram`, `ConvergenceCriteria`.
- Produces: `enum class ThresholdEnum { NotSupported, DefaultExteriorStage, TopOfLevee, LeveeSystemResponse, AdditionalExteriorStage }` (confirm exact members/order from `ThresholdEnum.cs`); `AssuranceResultStorage` — ctor `(std::string assurance_type, double bin_width, ConvergenceCriteria, double standard_non_exceedance_probability = 0)`; `void add_observation(double result, long iteration)` (stages into `temp_results_[iteration]`); `void put_data_into_histogram()`; `DynamicHistogram assurance_histogram()`, `std::string assurance_type()`, `double standard_non_exceedance_probability()`; `bool equals(...)`.

- [ ] **Step 1: Read the source.** `ThresholdEnum.cs` (drop the `[DisplayName]`/`[StoredProperty]` attributes — port the enum only) + `AssuranceResultStorage.cs` (93). Transcribe `AddObservation` (stage) + `PutDataIntoHistogram` (flush temp → histogram, clear). SEVER XML.
- [ ] **Step 2: Write the failing fixture** `fixtures/metrics/assurance_result_storage.json`, target `assurance_result_storage`: construct with a ConvergenceCriteria + bin width, stage a known set of observations across iterations, `put_data_into_histogram()`, assert histogram stats (`sample_mean`/`inverse_cdf`). `"expected":"PIN"`.
- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalAssuranceResultStorage` + `case`. Patched copy if XML drags in.
- [ ] **Step 4: Implement** both headers; loader + `assurance_result_storage` dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green; count up (baseline 641).
- [ ] **Step 6: Commit** `feat(metrics): port ThresholdEnum + AssuranceResultStorage + fixtures`.

---

### Task 2: `SystemPerformanceResults`

**Files:** Create `core/include/hecfda/model/metrics/system_performance_results.hpp`; Create `fixtures/metrics/system_performance_results.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Task-1 `AssuranceResultStorage`, Phase-1 `DynamicHistogram`/`ConvergenceCriteria`, Phase-2 `UncertainPairedData`/`PairedData`.
- Produces: `SystemPerformanceResults` — ctors `()` (dummy AEP+stage assurances), `(ConvergenceCriteria)`, `(UncertainPairedData system_response, ConvergenceCriteria)` (sets levee flag from Xvals length); `std::vector<AssuranceResultStorage> assurances()`; `add_stage_assurance_histogram`, `get_assurance_of_threshold_histogram`, `add_aep_for_assurance(aep, iteration)`, `add_stage_for_assurance`, `mean_aep()`, `median_aep()`, `aep_with_given_assurance(q)`, `assurance_of_aep(aep)`, `assurance_of_event(standard_nonexceedance_prob, threshold_value)` + private `calculate_assurance_for_levee` (fragility integration over `system_response.sample_paired_data(0.5)`), `long_term_exceedance_probability(years)`, convergence (`assurance_is_converged`/`assurance_remaining_iterations`/`parallel_results_are_converged`), `put_data_into_histograms()`, `bool equals(...)`, `get_assurance(type)`. Read `SystemPerformanceResults.cs` for the const type/binwidth strings + the ER-101 assurance level handling.

- [ ] **Step 1: Read the source.** `SystemPerformanceResults.cs` (353). Transcribe the AEP-assurance + stage-assurance histogram machinery, `MeanAEP`/`MedianAEP`/`AEPWithGivenAssurance`/`AssuranceOfAEP`, `AssuranceOfEvent` + `CalculateAssuranceForLevee` (the fragility-curve integration — FP-sensitive), `LongTermExceedanceProbability` (`1 - (1-aep)^years`), the convergence methods. SEVER MVVM `ReportMessage` (in `GetAssurance` → documented throw/empty) + XML.
- [ ] **Step 2: Write the failing fixture** `fixtures/metrics/system_performance_results.json`, target `system_performance_results`. Reproduce the pure-numeric performance oracles that don't need the full compute: from `PerformanceTest` — the levee AEP path (`ComputeLeveeAEP` = 0.026) and the assurance path (`AssuranceResultStorageShould`: seed the standard-normal via `Random(1234)`, `assurance_of_event(0.998, 2.88) == Normal().cdf(2.88)` tol .009 — **this is the RNG-port conformance pin**, capture from the gate). Also a `mean_aep`/`long_term_exceedance_probability` case. `"expected":"PIN"`.
- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalSystemPerformanceResults` + `case`. Patched copy stripping MVVM/XML.
- [ ] **Step 4: Implement**; loader + `system_performance_results` dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green; the `Normal().cdf(2.88)` assurance conformance value reproduces (proves the seeded `Random(1234)` path).
- [ ] **Step 6: Commit** `feat(metrics): port SystemPerformanceResults (AEP + assurance + levee) + fixtures`.

---

### Task 3: `Threshold` + `PerformanceByThresholds`

**Files:** Create `core/include/hecfda/model/metrics/threshold.hpp`, `performance_by_thresholds.hpp`; Create `fixtures/metrics/performance_by_thresholds.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Task-2 `SystemPerformanceResults`, Task-1 `ThresholdEnum`, `ConvergenceCriteria`, `UncertainPairedData`.
- Produces: `Threshold` — ctors `()` (null/dummy id 9999), `(int id, ConvergenceCriteria, ThresholdEnum, double value)`, `(int id, UncertainPairedData system_response, ConvergenceCriteria, ThresholdEnum, double value)`; `ThresholdEnum threshold_type()`, `double threshold_value()`, `SystemPerformanceResults& system_performance_results()`, `int threshold_id()`; `bool equals(...)`. `PerformanceByThresholds` — ctors `()`, `(bool is_null)` (adds dummy Threshold); `void add_threshold(Threshold)`, `Threshold& get_threshold(int id)`, `std::vector<Threshold>& list_of_thresholds()`; `bool equals(...)`.

- [ ] **Step 1: Read the source.** `Threshold.cs` (147) + `PerformanceByThresholds.cs` (102). SEVER XML/`[StoredProperty]`-reflection + MVVM `ReportMessage` (in `GetThreshold` → throw/documented). `get_threshold` returns the matching threshold by id.
- [ ] **Step 2: Write the failing fixture** `fixtures/metrics/performance_by_thresholds.json`, target `performance_by_thresholds`: build thresholds, `add_threshold`, `get_threshold(id)`, assert threshold_value/type + a round-trip through the held SystemPerformanceResults. `"expected":"PIN"`.
- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalPerformanceByThresholds` + `case`. Patched copies (both).
- [ ] **Step 4: Implement** `threshold.hpp` then `performance_by_thresholds.hpp`; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green.
- [ ] **Step 6: Commit** `feat(metrics): port Threshold + PerformanceByThresholds + fixtures`.

---

### Task 4: `CategoriedPairedData` + `CategoriedUncertainPairedData`

**Files:** Create `core/include/hecfda/model/metrics/categoried_paired_data.hpp`, `categoried_uncertain_paired_data.hpp`; Create `fixtures/metrics/categoried_uncertain_paired_data.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Phase-2 `PairedData`/`UncertainPairedData`, Phase-1 `DynamicHistogram`/`ConvergenceCriteria`, Task-1 enums.
- Produces: `CategoriedPairedData` — immutable holder `(PairedData frequency_curve, ConsequenceType, RiskType, std::string damage_category, std::string asset_category)` + accessors. `CategoriedUncertainPairedData` — ctors `(std::vector<double> xvals, damage_category, asset_category, ConsequenceType, RiskType, ConvergenceCriteria)` and `(CategoriedPairedData initial, ConvergenceCriteria)`; `void add_curve_realization(const PairedData&, long iteration)`; `void put_data_into_histograms()`; `UncertainPairedData get_uncertain_paired_data()`; accessors (`xvals`, `y_histograms`, categories). Read `CategoriedUncertainPairedData.cs` for the per-ordinate histogram batching (`_TempYValues`/`_BatchSize`, `InitializeHistograms`).

- [ ] **Step 1: Read the source.** `CategoriedPairedData.cs` (21) + `CategoriedUncertainPairedData.cs` (260). Transcribe the batching (`AddCurveRealization` stages into `_TempYValues`; `PutDataIntoHistograms` flushes per-ordinate). SEVER XML.
- [ ] **Step 2: Write the failing fixture** `fixtures/metrics/categoried_uncertain_paired_data.json`, target `categoried_uncertain_paired_data`: construct from an initial `CategoriedPairedData`, `add_curve_realization` a sequence of `PairedData`s, `put_data_into_histograms()`, `get_uncertain_paired_data().sample_paired_data(1, true)`, assert yvals. `"expected":"PIN"` (vector).
- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalCategoriedUncertainPairedData` + `case`. Patched copy stripping XML.
- [ ] **Step 4: Implement** both; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green.
- [ ] **Step 6: Commit** `feat(metrics): port CategoriedPairedData + CategoriedUncertainPairedData + fixtures`.

---

### Task 5: analytical-frequency prerequisite (`ContinuousDistribution::sample` + `bootstrap_to_paired_data`)

**Files:** Create `core/include/hecfda/statistics/distributions/continuous_distribution_extensions.hpp`; Modify the `ContinuousDistribution` base (`core/include/hecfda/statistics/distributions/continuous_distribution.hpp` — add `sample(iteration)` + `generate_random_samples_of_numbers(seed, size)` if missing); Create `fixtures/compute/bootstrap_to_paired_data.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Phase-1 distributions (LogPearson3/Normal/Uniform), `DotNetRandom`.
- Produces: `ContinuousDistribution::sample(long iteration)` (parametric-uncertainty bootstrap resample — returns a `ContinuousDistribution` realization) + `generate_random_samples_of_numbers(int seed, long size)` (pre-generate the seeded uniform sequence, mirroring `UncertainPairedData::generate_random_numbers`); a free `bootstrap_to_paired_data(const ContinuousDistribution&, long iteration, const std::vector<double>& exceedance_probs, bool deterministic) -> PairedData` (deterministic → the distribution itself, else `sample(iteration)`, then `InverseCDF(prob)` over the exceedance probs); `required_exceedance_probabilities()` (the `DoubleGlobalStatics.RequiredExceedanceProbabilities` constant array).

- [ ] **Step 1: Read the source.** `upstream/HEC-FDA/HEC.FDA.Model/extensions/ContinuousDistributionExtensions.cs` (`BootstrapToPairedData` ~48-71), `HEC.FDA.Statistics/Distributions/ContinuousDistribution.cs` (`Sample(long)` + `GenerateRandomSamplesofNumbers`), and `DoubleGlobalStatics.RequiredExceedanceProbabilities`. Transcribe the bootstrap resample (how `Sample(iteration)` perturbs the fitted parameters using the pre-generated random numbers) + the `InverseCDF`-over-probs assembly. This is the seeded analytical path (`FREQUENCY_SEED=1234`).
- [ ] **Step 2: Write the failing fixture** `fixtures/compute/bootstrap_to_paired_data.json`, target `bootstrap_to_paired_data`: LP3 `LogPearson3(3.537, .438, .075, 125)` (from `StudyDataAnalyticalFrequencyResultsTests`), `bootstrap_to_paired_data(lp3, iteration=1, required_exceedance_probs, deterministic=true)` → assert the frequency-discharge PairedData yvals (deterministic; no RNG); plus a seeded case (`generate_random_samples_of_numbers(1234, size)` then `bootstrap_to_paired_data(..., iteration=1, ..., false)`) → gate-captured yvals (proves the seeded bootstrap). `"expected":"PIN"` (vector).
- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalBootstrapToPairedData` + `case`. Patched copy if the extensions file drags MVVM/etc.
- [ ] **Step 4: Implement** the `ContinuousDistribution` additions + `continuous_distribution_extensions.hpp`; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green; the deterministic LP3 curve + the seeded curve reproduce.
- [ ] **Step 6: Commit** `feat(statistics): ContinuousDistribution.Sample + bootstrap_to_paired_data (analytical frequency) + fixtures`.

---

### Task 6: `ImpactAreaScenarioResults`

**Files:** Create `core/include/hecfda/model/metrics/impact_area_scenario_results.hpp`; Create `fixtures/metrics/impact_area_scenario_results.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Task-3 `PerformanceByThresholds`, Task-4 `CategoriedPairedData`/`CategoriedUncertainPairedData`, Phase-4 `StudyAreaConsequencesBinned`, `ConvergenceCriteria`.
- Produces: `ImpactAreaScenarioResults` — ctors `(int impact_area_id, bool is_null)`, `(int impact_area_id)`; holds `PerformanceByThresholds`, `StudyAreaConsequencesBinned consequence_results_`, `std::vector<CategoriedPairedData> consequence_frequency_functions_`, `std::vector<CategoriedUncertainPairedData> uncertain_consequence_frequency_curves_`; the AEP/performance delegators (`mean_aep`/`median_aep`/`aep_with_given_assurance`/`assurance_of_aep`/`long_term_exceedance_probability`/`assurance_of_event` via `performance_by_thresholds_.get_threshold(id)`); consequence delegators (`mean_expected_annual_consequences(id, damcat, assetcat[, ConsequenceType, RiskType])`, `get_specific_histogram`); convergence (`results_are_converged`, `consequence_results_are_converged`, `performance_results_are_converged`, `remaining_iterations`, `parallel_results_are_converged`); `get_or_create_uncertain_consequence_frequency_curve(...)` (thread-safe in C#; serial in port), `put_uncertain_frequency_curves_into_histograms()`; `bool equals(...)`. SEVER XML.

- [ ] **Step 1: Read the source.** `ImpactAreaScenarioResults.cs` (287). Transcribe the delegators + convergence + the uncertain-curve get-or-create (drop the C# `lock`; serial port). SEVER `WriteToXml`/`ReadFromXML`.
- [ ] **Step 2: Write the failing fixture** `fixtures/metrics/impact_area_scenario_results.json`, target `impact_area_scenario_results`: construct a results object, feed consequence realizations + AEP/assurance observations through the held sub-objects, `put_*_into_histograms`, assert `mean_expected_annual_consequences` + `mean_aep`. `"expected":"PIN"`.
- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalImpactAreaScenarioResults` + `case`. Patched copy stripping XML.
- [ ] **Step 4: Implement**; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green.
- [ ] **Step 6: Commit** `feat(metrics): port ImpactAreaScenarioResults (compute output container) + fixtures`.

---

### Task 7: `ImpactAreaScenarioSimulation` — `SimulationBuilder` + skeleton + validation

**Files:** Create `core/include/hecfda/model/compute/impact_area_scenario_simulation.hpp` (skeleton); Create `fixtures/compute/simulation_validation.json`; Modify `test_fixtures.cpp`, csproj (+`patched/ImpactAreaScenarioSimulation.cs`), `Program.cs`.

**Interfaces:**
- Consumes: everything Tasks 1-6 produced + Phase 1-4 curve types.
- Produces: `ImpactAreaScenarioSimulation` with `static SimulationBuilder builder(int impact_area_id)`; the nested `SimulationBuilder` with the fluent `with_flow_frequency(ContinuousDistribution)` / `with_flow_frequency(GraphicalUncertainPairedData)` / `with_inflow_outflow(UncertainPairedData)` / `with_flow_stage(UncertainPairedData)` / `with_frequency_stage(GraphicalUncertainPairedData)` / `with_interior_exterior(UncertainPairedData)` / `with_levee(UncertainPairedData, double top)` / `with_stage_damages(std::vector<UncertainPairedData>)` / `with_non_failure_stage_damage(...)` / `with_stage_life_loss(...)` / `with_non_failure_stage_life_loss(...)` / `with_additional_threshold(Threshold)` / `build()`; the stored fields (all the `_Frequency*`/`_DischargeStage`/`_SystemResponseFunction`/`_FailureStageDamageFunctions`/etc.); `bool can_compute(ConvergenceCriteria)` + `validate()` (the CanCompute gate — returns a null `ImpactAreaScenarioResults` when min>max or inputs insufficient); `initialize_consequence_histograms(...)`. The move-only / storage decisions (curve inputs are move-only `UncertainPairedData`/`GraphicalUncertainPairedData`; the sim is move-only — document).

- [ ] **Step 1: Read the source.** `ImpactAreaScenarioSimulation.cs` lines 1-100, 733-904 (fields, ctor, `Builder`, `SimulationBuilder`, `CanCompute`, `Validate`, `InitializeConsequenceHistograms`). SEVER MVVM base/`IProgressReport`/`[StoredProperty]`/messaging.
- [ ] **Step 2: Write the failing fixture** `fixtures/compute/simulation_validation.json`, target `simulation` (method `can_compute`): the `ResultsShouldNotComputeWhenMaxIterationsAreGreaterThanMinIterations` guard — `ConvergenceCriteria(maxIterations:1)` (default min 50000 > max 1) → `compute(...).is_null() == true`. Also a valid-inputs `can_compute == true`. `"expected"` bool/exact.
- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalSimulation` (build via the `SimulationBuilder` chain from the fixture construct; dispatch `can_compute`/`is_null`) + `case "simulation"`. Start `patched/ImpactAreaScenarioSimulation.cs` (MVVM base → plain class, messaging/progress/threading stripped, `[StoredProperty]` dropped). Only the skeleton + CanCompute compiled/dispatched this task; stub the compute body (throw) so it compiles.
- [ ] **Step 4: Implement** the skeleton + builder + can_compute; loader + `simulation` dispatch (validation only).
- [ ] **Step 5: Pin + verify.** `ctest` + gate green.
- [ ] **Step 6: Commit** `feat(compute): ImpactAreaScenarioSimulation skeleton + SimulationBuilder + CanCompute + fixtures`.

---

### Task 8: frequency-stage assembly + seeded random-number population

**Files:** Modify `impact_area_scenario_simulation.hpp` (add the sampling methods); Create `fixtures/compute/frequency_stage_sample.json`; Modify `test_fixtures.cpp`, `patched/ImpactAreaScenarioSimulation.cs`, `Program.cs`.

**Interfaces:**
- Produces: `populate_random_numbers(ConvergenceCriteria)` (each curve `generate_random_numbers(SEED, quantity)` with the per-curve seed constants, `quantity = (int)(MaxIterations * 1.25)`); `get_frequency_stage_sample(iteration, deterministic)` → a `FrequencyStageCurves { PairedData channel_stage; PairedData floodplain_stage; }` (the analytical `bootstrap_to_paired_data` / graphical `sample_paired_data` → reg/unreg `compose` → rating `compose` → interior/exterior `compose`); `get_stage_freq(...)`.

- [ ] **Step 1: Read the source.** `ImpactAreaScenarioSimulation.cs` `PopulateRandomNumbers` (202-252), `GetFrequencyStageSample` (405-433), `GetStageFreq` (435-451), the `FrequencyStageCurves` record (25-28). Transcribe the seed constants (34-40) + the compose chain (analytical via `bootstrap_to_paired_data`, graphical via `sample_paired_data`, reg/unreg + rating + interior/exterior via `compose`; the `ReferenceEquals` channel==floodplain when no interior/exterior).
- [ ] **Step 2: Write the failing fixture** `fixtures/compute/frequency_stage_sample.json`, target `simulation` (methods `frequency_stage_channel_yvals`/`frequency_stage_floodplain_yvals`): build the `ComputeEAD` tractable inputs (flow_freq `Uniform(0,100000,1000)`, flow_stage X=`{0,100000}` Y=`Uniform(0,0,10)`/`Uniform(0,30,10)`), `populate_random_numbers(cc)`, `get_frequency_stage_sample(iteration=1, deterministic=true)` → assert the channel-stage yvals; plus a seeded case. `"expected":"PIN"` (vector).
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** Extend `EvalSimulation` + `patched/ImpactAreaScenarioSimulation.cs` with these methods.
- [ ] **Step 4: Implement**; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green; the seeded frequency-stage sample reproduces (proves `populate_random_numbers` + the per-curve seeds).
- [ ] **Step 6: Commit** `feat(compute): frequency-stage assembly + seeded PopulateRandomNumbers + fixtures`.

---

### Task 9: risk / consequence integration + performance / threshold / assurance

**Files:** Modify `impact_area_scenario_simulation.hpp` (add the risk + performance methods); Create `fixtures/compute/default_threshold.json`; Modify `test_fixtures.cpp`, `patched/ImpactAreaScenarioSimulation.cs`, `Program.cs`.

**Interfaces:**
- Produces: `compute_risk_from_stage_frequency(...)` (no-levee: `stage_consequences.compose(frequency_stage).integrate()` → consequence realization + `add_curve_realization`); `compute_annualized_consequence(...)` (levee: `multiply(system_response)` / interior-exterior `compose`+`multiply`; then `integrate`); `calculate_failure_prob_complement`, `compute_total_stage_damage` (sum via `sum_ys_for_given_x`), `ensure_bottom_and_top_have_correct_probabilities`; `compute_performance_from_stage_frequency(...)` → `compute_performance` (per threshold `aep = 1 - frequency_stage.f_inverse(value)`, `add_aep_for_assurance`, `get_stage_for_non_exceedance_probability` with ER-101 levels `{.9,.96,.98,.99,.996,.998}`) / `compute_levee_performance` (fragility integration); `setup_performance_thresholds(...)`, `determine_system_response_threshold`, `compute_default_threshold` (`DefaultExteriorStage` at 5% of the 0.99-NEP damage, back-solved via `total_stage_damage.f_inverse(...)`), `create_histograms_for_assurance_of_thresholds`.

- [ ] **Step 1: Read the source.** `ImpactAreaScenarioSimulation.cs` 136-199, 498-722. Transcribe the compose/integrate risk chain, the levee `multiply` path, the performance/assurance methods, `ComputeDefaultThreshold` (`THRESHOLD_DAMAGE_PERCENT`, `DEFAULT_THRESHOLD_ID=0`), the ER-101 assurance levels.
- [ ] **Step 2: Write the failing fixture** `fixtures/compute/default_threshold.json`, target `simulation` (method `default_threshold_value` / `threshold_value`): the `DefaultThresholdShould` oracles (deterministic) — the default `DefaultExteriorStage` threshold value in `[0,30]`, the user-override case (`AdditionalExteriorStage, 20.0` → value 20.0 exact), the same-value-regardless-of-damage-category-count case. `"expected":"PIN"` + the exact user-override literal.
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** Extend `EvalSimulation` + patched copy with these methods (a deterministic single-pass to compute the default threshold).
- [ ] **Step 4: Implement**; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green.
- [ ] **Step 6: Commit** `feat(compute): risk/consequence integration + performance/threshold/assurance + fixtures`.

---

### Task 10: `ComputeIterations` MC loop + `Compute`/`PreviewCompute` → DETERMINISTIC oracles

**Files:** Modify `impact_area_scenario_simulation.hpp` (the compute loop + entry points); Create `fixtures/compute/impact_area_scenario_simulation_deterministic.json`; Modify `test_fixtures.cpp`, `patched/ImpactAreaScenarioSimulation.cs`, `Program.cs`.

**Interfaces:**
- Produces: `ImpactAreaScenarioResults compute(ConvergenceCriteria, bool compute_is_deterministic = false)` (+ the internal `compute_iterations`), `ImpactAreaScenarioResults preview_compute()`. `compute_iterations`: the chunked convergence loop (`iterations_per_compute_chunk = IterationCount`, `additional_chunks = ceil(MinIterations/chunk)`, the `while(not converged)` over chunks × iterations — **serial `for`, NOT `Parallel.For`**; after each chunk `put_data_into_histograms` on consequences/frequency-curves/per-threshold-performance; `results_are_converged(.95, .05, check_consequence_results)`; on non-convergence recompute `remaining_iterations`). `compute` orchestrates `can_compute` → `initialize_consequence_histograms` → `setup_performance_thresholds` → `populate_random_numbers` → `compute_iterations` → final `parallel_results_are_converged(.95, .05)`.

- [ ] **Step 1: Read the source.** `ImpactAreaScenarioSimulation.cs` 97-134, 330-403, 724-731 (`Compute` overloads, `ComputeIterations`, `PreviewCompute`). Transcribe the loop structure verbatim, replacing `Parallel.For(0, iterationsPerComputeChunk, ...)` with a serial `for` (document: order-independent since iteration `n` samples by index `n`).
- [ ] **Step 2: Write the failing fixture** `fixtures/compute/impact_area_scenario_simulation_deterministic.json`, target `impact_area_scenario_simulation` (method `mean_eac` = `mean_expected_annual_consequences(id, damcat, assetcat[, ConsequenceType, RiskType])`). The DETERMINISTIC oracles (`compute_is_deterministic=true`, cc min=max=1): `ComputeEAD` (flow_freq `Uniform(0,100000,1000)`, flow_stage X=`{0,100000}` Y=`Uniform(0,0,10)`/`Uniform(0,30,10)`, stage_damage X=`{0,15,30}` Y=`{Uniform(0,0,10),Uniform(0,600000,10),Uniform(0,600000,10)}`, Threshold(1,DefaultExteriorStage,150000)) → **150000** (rel tol 0.01); `ComputeEAD_withLevee` (`{83333.33 @ top 10}`, `{0 @ top 400000}`); `TotalRiskShould` (all-Deterministic failure/non-failure/system-response) → **100150.179**; `ComputeEALL` (life-loss) → 150000; `preview_compute` on the LP3 analytical inputs (`StudyDataAnalyticalFrequencyResultsTests.ComputeMeanEAD_Test`) → **20.74** (rel diff < 0.016). `"expected":"PIN"` (the literals are cross-checks).
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** Extend `EvalImpactAreaScenarioSimulation` (build via builder, `Compute(cc, true)` / `PreviewCompute()`, dispatch `mean_expected_annual_consequences`). Finish the compute body in `patched/ImpactAreaScenarioSimulation.cs`.
- [ ] **Step 4: Implement** the loop + entry points; loader + dispatch. DEBUG until the deterministic oracles reproduce (do NOT loosen tolerances).
- [ ] **Step 5: Pin + verify.** `ctest` + gate green; 150000 / 83333.33 / 0 / 100150.179 / 20.74 all reproduce and match the upstream literals.
- [ ] **Step 6: Commit** `feat(compute): ImpactAreaScenarioSimulation.Compute + PreviewCompute (deterministic EAD oracle)`.

---

### Task 11: seeded-MC validation (the seeded EAD + performance benchmarks)

**Files:** Create `fixtures/compute/impact_area_scenario_simulation_seeded.json`; Modify `test_fixtures.cpp`, `Program.cs` (extend `EvalImpactAreaScenarioSimulation` for the seeded path — same builder, `Compute(cc)` non-deterministic). No new core code expected (Tasks 8/10 already implemented the seeded path); this task PINS it and closes any divergence.

**Interfaces:**
- Consumes: the full Task-7-10 `ImpactAreaScenarioSimulation`.
- Produces: no new interface — a validation task pinning the seeded-MC oracles.

- [ ] **Step 1:** Confirm the seeded path (`compute_is_deterministic=false`) runs: per-curve seeds + `populate_random_numbers` + `sample_paired_data(iteration, false)` per iteration.
- [ ] **Step 2: Write the failing fixture** `fixtures/compute/impact_area_scenario_simulation_seeded.json`, target `impact_area_scenario_simulation`. The seeded oracles with EXACT iteration counts (cc min==max so the count is fixed): `ComputeEAD_Iterations` (cc min=max=100, the `ComputeEAD` inputs, no explicit threshold, non-deterministic) → **121194.5159789352** (rel tol 0.05); `PerformanceTest.ComputeConditionalNonExceedanceProbability` (cc min 101 / max 10001) → `assurance_of_event` values (0.7777/0.7575/0.7028/0.9018, rel tol 0.07); the graphical seeded (`StudyDataGraphicalStageFrequencyResultsTests`, cc min 1000 / max 10000) → **5.88** (rel tol 0.09); the LP3 seeded (`ComputeMeanEADWithIterations`, cc min 100 / max 10000) → **21.09** (rel tol 0.032). `"expected":"PIN"` — captured from the gate; the literals are cross-checks. (Prefer the exact-count cases; higher-tolerance ones confirm the pipeline.)
- [ ] **Step 3: Run, confirm the seeded values reproduce vs the gate.** If a value diverges, DEBUG the seeded sampling (per-curve seed order, `quantity = MaxIterations*1.25`, the shared-uniform-per-curve-per-iteration, the weak-monotonicity forcing) until it matches — the seeded MC must reproduce real C# bit-for-bit at fixed iteration counts. Never loosen beyond the upstream tolerances.
- [ ] **Step 4: Pin + verify.** `ctest` + gate green; `121194.5159789352` reproduces at exactly 100 iterations.
- [ ] **Step 5: Commit** `feat(compute): seeded Monte-Carlo EAD + performance oracle validation`.

---

### Task 12: R/Python representative subset + closeout

**Files:** Modify `hecfdar/src/*.cpp` (+`cpp11::cpp_register`), `hecfdar/tests/testthat/test-fixtures.R`, `hecfdapy/src/**`, `hecfdapy/tests/test_fixtures.py`; Modify `.claude/CLAUDE.md`, `.claude/PLAN.md`, `.superpowers/sdd/progress.md`, memory file.

**Interfaces:**
- Produces: R/Python bindings + fixture-runner dispatch for a representative subset: `system_performance_results` (a metrics/performance leaf exercising the assurance histograms + the seeded `Random(1234)` conformance) and `impact_area_scenario_simulation` (the end-to-end EAD compute — the phase's headline). Document that the other targets traverse the identical binding + compiled core.

- [ ] **Step 1:** Choose + document the subset (`system_performance_results` + `impact_area_scenario_simulation`) in `.claude/CLAUDE.md`, mirroring the R/Python coverage-scope convention.
- [ ] **Step 2:** Add R bindings mirroring the C++ dispatch; `cpp11::cpp_register("hecfdar")`; `R CMD INSTALL --preclean hecfdar`. (Note: the seeded compute runs many iterations — the impact_area_scenario_simulation fixture may take a minute in R; use the exact-100-iteration deterministic/seeded cases to keep it fast, or a reduced case.)
- [ ] **Step 3:** Extend `test-fixtures.R` to load the two fixtures; `Rscript -e 'testthat::test_local("hecfdar")'`.
- [ ] **Step 4:** Mirror in Python (pybind11; `pip install --force-reinstall --no-deps ./hecfdapy`; extend `test_fixtures.py`); `pytest hecfdapy/tests -q`.
- [ ] **Step 5:** If any Phase-5 fixture needs a compare mode the R/Python runners lack, add it symmetrically; otherwise skip.
- [ ] **Step 6: Full four-leg exit gate.** `make test-core`, `make test-r`, `make test-py PYTHON=~/venv/hecfdapy/bin/python`, `make oracles` — all green; record the new gate count. Confirm the cross-language invariants still hold (`sample_and_integrate(1234)==24.425549382855987`, `rng_digest`) and the Phase-3/4 fixtures (structures, stage-damage tractable curves) still pass.
- [ ] **Step 7: Docs + memory.** Update `.claude/CLAUDE.md` (Status → Phase 5 complete; add a paragraph on `hecfda::model::metrics` EAD-results surface + `hecfda::model::compute::ImpactAreaScenarioSimulation`; the per-curve seed constants + the `Parallel.For`→serial severance; the analytical `bootstrap_to_paired_data` path; new faithful bugs; the R/Python subset; note the seeded-MC is now validated). Update `.claude/PLAN.md` (Phase 5 done, Phase 6 scenarios+alternatives next: `Scenario`/`Alternative`/`AlternativeComparisonReport` + `ScenarioResults` + the ByQuantile types). Update the memory file with a concise "Phase 5 COMPLETE" paragraph.
- [ ] **Step 8:** Commit `docs(compute): Phase 5 closeout -- bindings, exit gate, status`. Stop. Do NOT open a PR or merge — the controller runs `superpowers:finishing-a-development-branch` and the user chooses.

---

## Self-Review

- **Spec coverage:** design-spec Phase 5 ("Compute + metrics — `ImpactAreaScenarioSimulation` (EAD Monte Carlo) and the full results/consequence/performance/threshold/assurance surface"). Tasks 1-6 port the 8-type metrics closure + the analytical prerequisite; Tasks 7-11 port the simulation engine (skeleton/builder → seeding/freq-stage → risk/performance → deterministic compute → seeded validation). `ScenarioResults`/`Scenario`/`Alternative`/ByQuantile deferred to Phase 6 (documented).
- **Type consistency:** `AssuranceResultStorage`(T1) → `SystemPerformanceResults`(T2) → `Threshold`(T3) → `PerformanceByThresholds`(T3) → `ImpactAreaScenarioResults`(T6) ← `CategoriedPairedData`/`CategoriedUncertainPairedData`(T4); `bootstrap_to_paired_data`(T5) feeds the simulation's analytical path (T8); `ImpactAreaScenarioSimulation`(T7-11) returns `ImpactAreaScenarioResults`(T6). Every consumer's signature is declared in its producer task.
- **Oracle discipline:** deterministic pins first (150000 / 83333.33 / 100150.179 / 20.74 / default-threshold), then seeded-MC (121194.5159789352 exact-100, `Normal().cdf(2.88)` RNG conformance, 5.88/21.09). All `"PIN"`-then-gate-captured; upstream literals are cross-checks. The seeded MC must reproduce real C# bit-for-bit at fixed iteration counts (RNG parity).
- **Placeholder scan:** each task has exact paths, upstream line ranges, fixture inputs with literals + seeds, and the emitter/runner wiring. The heaviest patched copy (`ImpactAreaScenarioSimulation.cs`) is built across T7-T11 with the severance list (MVVM/messaging/threading/XML) enumerated. The only deferral is gate-captured expecteds.
- **Severance coherence:** MVVM/messaging/XML/threading are cut at the same boundary across every metrics type and the simulation, with patched emitter copies keeping numeric lines verbatim. `Parallel.For`→serial is safe (index-addressed sampling).
