# HEC-FDA Port — Phase 6 (FINAL): Scenarios & Alternatives Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Complete the port — `HEC.FDA.Model/scenarios`, `alternatives`, and `alternativeComparisonReport` — on top of the Phase-5 EAD engine. `Scenario` fans out `ImpactAreaScenarioSimulation.Compute` across impact areas → `ScenarioResults`; `Alternative.AnnualizationCompute` turns base-year + future-year `ScenarioResults` into equivalent-annual damage (EqAD) via deterministic quantile-walking (interpolate → present-value → PVIFA); `AlternativeComparisonReport` computes with/without-project damage-reduction (benefits) by empirical-distribution subtraction. This requires un-severing the `Empirical`/histogram/quantile chain deferred in Phases 4-5 and porting the 5 remaining `metrics` types.

**Architecture:** New headers under `core/include/hecfda/model/metrics/` (the two `*ByQuantile` types + the 3 result aggregates), `core/include/hecfda/model/scenarios/`, `core/include/hecfda/model/alternatives/`, `core/include/hecfda/model/alternative_comparison_report/`. Un-sever `Empirical::stack_empirical_distributions` (+ `sum`/`subtract`), `DynamicHistogram::convert_to_empirical_distribution`, `AggregatedConsequencesBinned::convert_to_single_empirical_distribution_of_consequences`, `StudyAreaConsequencesBinned::convert_to_study_area_consequences_by_quantile`, and the ByQuantile `filter_by_categories`. **No new seeded Monte Carlo** — the annualization/comparison operate deterministically on already-computed `ScenarioResults` distributions (the seeded MC lives entirely in the ported `ImpactAreaScenarioSimulation`). Oracle values live only in `fixtures/`; the four runners validate them.

**Tech Stack:** C++17 (doctest), cpp11 (R), pybind11 (Python), dotnet gate (net9 build on net10 runtime via `DOTNET_ROLL_FORWARD=Major`).

**Branch:** `phase6` (off `main` @ the Phase-5 merge, commit `71c7a9b`). Upstream pinned `upstream/HEC-FDA` @ `f63682a86a30dc306a105689714a92bfd95956c5`.

## Global Constraints

- **C++17 only.** No C++20. Self-contained core; no external C++ deps; NO threads-for-correctness (`Parallel.For`/`ConcurrentBag` → serial; deterministic).
- **Structural mirroring:** each ported file opens with `// ported from: <upstream-path> @ f63682a86a30dc306a105689714a92bfd95956c5`; mirror the C# method layout/order; transcribe verbatim; reproduce upstream bugs (do not "fix"), documenting each.
- **Portability:** never `M_PI` (use `hecfda::kPi`); never a namespace alias `gamma`/`stat`; `-Wall/-Wextra` non-MSVC only.
- **FP-contraction parity (standing invariant):** `-ffp-contract=off` is already set project-wide. Do NOT remove/scope it. The present-value/PVIFA/quantile-walk arithmetic is FP-sensitive.
- **Determinism scope:** this phase adds NO seeded MC. The `ComputeEqad`/`AnnualizationCompute`/comparison math is deterministic (inverse-CDF quantile-walking, `probabilitySteps=25000`, NOT a seed). The `Parallel.For`→serial severance is safe (per-quantile-step independent). The one upstream `seed=1234` in `DefaultDataComputeOutcomes` is declared-but-unwired — ignore it; that oracle relies on 50000-iteration convergence.
- **No hardcoded oracle values in test code.** Oracles live in `fixtures/*.json`, sourced from the upstream `HEC.FDA.ModelTest` scenario/alternative tests, captured from real C# via the dotnet gate. Write `"expected": "PIN"` first, then replace in the pin step. Upstream literals (`ComputeEqad` = {38835.3, 255.68, 35000, 41893.12, 40680.87, 378.72, 35000, 44279.92}; comparison subtraction = {700, 1500, 7, 15, 0}; `AlternativeResults_Test` = {208213.8061, 239260.1814}; Muncie EAD = {310937.1, 295506.53, 132323.23, 98588.63}) are cross-checks.
- **Namespaces:** `hecfda::model::metrics`, `hecfda::model::scenarios`, `hecfda::model::alternatives`, `hecfda::model::alternative_comparison_report`.
- **Reuse Phase 1-5:** distributions incl. `Empirical`, histograms (`DynamicHistogram`), `ConvergenceCriteria`, paired-data, the Phase-4 consequence-binning types, the Phase-5 EAD-results/`ImpactAreaScenarioSimulation`.
- **Commits:** SSH-signed; identity `Cam Bracken <cameron.bracken@pm.me>`; conventional-commit; no `Co-Authored-By`; push only when asked.
- **ABI (R):** after a core class-layout change, `R CMD INSTALL --preclean hecfdar`; after editing `hecfdar/src/*.cpp`, re-run `cpp11::cpp_register("hecfdar")`. **Python dev venv:** `~/venv/hecfdapy`.
- **Definition of done per task:** `ctest` green; `python3 tools/verify_oracles.py` green (gate count strictly increases); C++ fixture runner exercises the new fixture; commit. (R/Python runner wiring for the representative subset is the final task.)

## Scope: severed / this-phase un-severed

| Area | Sever (this phase) | Un-sever (was deferred in Phase 4/5) |
|------|--------------------|--------------------------------------|
| MVVM (`ValidationErrorLogger`/`Validation`/`Rule`/`ReportMessage`/`MessageReport` event) | messaging + validation base → documented throw/no-op | — |
| Reflection (`Assembly` version stamp in Scenario), `ProgressReporter`/`Utility.Logging`/`OperationResult` | all | — |
| XML (`WriteToXML`/`ReadFromXML` on ScenarioResults) | persistence | — |
| Threading (`Parallel.For`, `ConcurrentBag`, `CancellationToken`) | → serial | — |
| `Empirical` stacking | — | `stack_empirical_distributions` (+ `sum`, `subtract` ops), `fit_to_sample` (verify ported) |
| `DynamicHistogram` | — | `convert_to_empirical_distribution` |
| binned→quantile converters | — | `AggregatedConsequencesBinned::convert_to_single_empirical_distribution_of_consequences`, `StudyAreaConsequencesBinned::convert_to_study_area_consequences_by_quantile` |
| `consequence_extensions` | — | the ByQuantile `filter_by_categories` overload |

**Faithful behavior to preserve:** `AnnualizationCompute` excludes `ConsequenceType::LifeLoss` from EqAD (life loss has no EqAD concept; it survives in the component scenario results). The identical-scenario short-circuit (`base.equals(future)` → `ScenariosAreIdentical`, EqAD taken directly with no interpolation/discount). Single-scenario path → EqAD == the input `SampleMean` exactly (no discounting). The comparison-report reduction is `without_project - with_project` (benefit = damage reduced).

## Dependency-ordered file/task map

```
core/include/hecfda/statistics/distributions/empirical.hpp        # Task 1: un-sever stack + subtract/sum (+ fit_to_sample)
core/include/hecfda/statistics/histograms/dynamic_histogram.hpp   # Task 1: un-sever convert_to_empirical_distribution
core/include/hecfda/model/metrics/
  aggregated_consequences_by_quantile.hpp   # Task 2 (leaf)
  consequence_extensions.hpp                # Task 2: add ByQuantile filter_by_categories overload
  study_area_consequences_by_quantile.hpp   # Task 3
  aggregated_consequences_binned.hpp        # Task 4: un-sever convert_to_single_empirical...
  study_area_consequences_binned.hpp        # Task 4: un-sever convert_to_study_area_consequences_by_quantile
  scenario_results.hpp                      # Task 5
  alternative_results.hpp                   # Task 6
  alternative_comparison_report_results.hpp # Task 7
core/include/hecfda/model/scenarios/scenario.hpp                              # Task 8
core/include/hecfda/model/alternatives/alternative.hpp                        # Task 9 (ComputeEqad + AnnualizationCompute)
core/include/hecfda/model/alternative_comparison_report/alternative_comparison_report.hpp  # Task 10
core/tests/test_fixtures.cpp                # each task: loader + bespoke dispatch target
fixtures/metrics/*.json, fixtures/alternatives/*.json, fixtures/scenarios/*.json
tools/oracle_emitter/... (patched copies for MVVM/XML/threading severance)
hecfdar/src/*.cpp, hecfdapy/src/**          # Task 12: bind representative subset
```

**Emitter note (every task):** subset-compile the minimal closure; patched copies strip MVVM/messaging/XML/reflection/threading, keeping numeric lines verbatim (established `patched/*.cs` precedent). The un-sever tasks (1, 4) restore methods to EXISTING patched copies where the port severed them.

---

### Task 1: un-sever `Empirical` stacking + `DynamicHistogram::convert_to_empirical_distribution`

**Files:** Modify `core/include/hecfda/statistics/distributions/empirical.hpp` (add `stack_empirical_distributions` + `sum`/`subtract` ops; verify/add `fit_to_sample`), `core/include/hecfda/statistics/histograms/dynamic_histogram.hpp` (add `convert_to_empirical_distribution`); Create `fixtures/distributions/empirical_stacking.json`; Modify `test_fixtures.cpp`, csproj, `Program.cs`.

**Interfaces:**
- Produces: `static Empirical Empirical::stack_empirical_distributions(const std::vector<Empirical>&, StackOp)` where `StackOp` = `sum` | `subtract` (parallel InverseCDF sampling across the inputs at each quantile, combine, refit via `fit_to_sample`); `static Empirical Empirical::fit_to_sample(const std::vector<double>&)` (if not already ported); `Empirical DynamicHistogram::convert_to_empirical_distribution() const` (the 2500-point InverseCDF sweep → new `Empirical` carrying `SampleMean`).

- [ ] **Step 1: Read the source.** `HEC.FDA.Statistics/Distributions/Empirical.cs` — `StackEmpiricalDistributions` + the `Sum`/`Subtract` stack operations + `FitToSample` (confirm whether `fit_to_sample` is already ported; if yes, reuse). `DynamicHistogram.cs` `ConvertToEmpiricalDistribution` (the 2500-pt sweep + SampleMean copy). Read the port's existing severance comments (`empirical.hpp:25-29`, `dynamic_histogram.hpp:51-53`) to see exactly what was stubbed. Transcribe the parallel-InverseCDF-then-refit verbatim (Parallel→serial).
- [ ] **Step 2: Write the failing fixture** `fixtures/distributions/empirical_stacking.json`, target `empirical_stacking`: build 2-3 `Empirical`s from known samples, `stack_empirical_distributions(list, sum)` and `(list, subtract)`, assert the stacked distribution's `sample_mean` + `inverse_cdf(0.5)`; plus a `DynamicHistogram` → `convert_to_empirical_distribution` case asserting the resulting empirical's `sample_mean`. `"expected":"PIN"`.
- [ ] **Step 3: Wire the emitter, run, confirm FAIL.** `EvalEmpiricalStacking` + `case`. Restore the severed methods in the emitter (they were in the clean `HEC.FDA.Statistics.csproj` — likely available; no patch needed).
- [ ] **Step 4: Implement** the un-severed methods; loader + `empirical_stacking` dispatch. Remove/replace the severance stubs + comments.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green; confirm NO prior Empirical/histogram fixture regressed.
- [ ] **Step 6: Commit** `feat(statistics): un-sever Empirical stacking (sum/subtract) + DynamicHistogram.ConvertToEmpiricalDistribution + fixtures`.

---

### Task 2: `AggregatedConsequencesByQuantile` + ByQuantile `filter_by_categories`

**Files:** Create `core/include/hecfda/model/metrics/aggregated_consequences_by_quantile.hpp`; Modify `core/include/hecfda/model/metrics/consequence_extensions.hpp` (add the ByQuantile overload); Create `fixtures/metrics/aggregated_consequences_by_quantile.json`; Modify `test_fixtures.cpp`, csproj, `Program.cs`.

**Interfaces:**
- Produces: `AggregatedConsequencesByQuantile` — ctors `()` (null/dummy) and `(std::string damage_category, std::string asset_category, Empirical, int impact_area_id, ConsequenceType, RiskType)`; get-only accessors (`consequence_distribution()`, categories, `region_id()`, `is_null()`); `double consequence_sample_mean() const` (`→ empirical.sample_mean()`), `double consequence_exceeded_with_probability_q(double q) const` (`→ empirical.inverse_cdf(1-q)`). `consequence_extensions.hpp`: `filter_by_categories` over `std::vector<AggregatedConsequencesByQuantile>` (impactAreaID -999 wildcard, ConsequenceType exact, RiskType Total wildcard — mirror the Binned overload; ByQuantile holds `Empirical` by value → copyable, so no unique_ptr constraint).

- [ ] **Step 1: Read the source.** `AggregatedConsequencesByQuantile.cs` (68) + `ConsequenceExtensions.cs` (the ByQuantile `FilterByCategories`). The leaf holds an `Empirical` (vs the Binned type's `DynamicHistogram`).
- [ ] **Step 2: Write the failing fixture** `fixtures/metrics/aggregated_consequences_by_quantile.json`, target `aggregated_consequences_by_quantile`: construct from a known `Empirical`, assert `consequence_sample_mean` + `consequence_exceeded_with_probability_q(0.5)`. `"expected":"PIN"`.
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalAggregatedConsequencesByQuantile` + `case`.
- [ ] **Step 4: Implement** the header + the extension overload; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green.
- [ ] **Step 6: Commit** `feat(metrics): port AggregatedConsequencesByQuantile + ByQuantile FilterByCategories + fixtures`.

---

### Task 3: `StudyAreaConsequencesByQuantile`

**Files:** Create `core/include/hecfda/model/metrics/study_area_consequences_by_quantile.hpp`; Create `fixtures/metrics/study_area_consequences_by_quantile.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Task-2 `AggregatedConsequencesByQuantile` + the ByQuantile `filter_by_categories`, Task-1 `Empirical::stack_empirical_distributions`.
- Produces: `StudyAreaConsequencesByQuantile` — ctors `()` (dummy), `(int alternative_id)`, `(std::vector<AggregatedConsequencesByQuantile>)`; `add_existing_consequence_result_object`, `double sample_mean_damage(...)`, `double consequence_exceeded_with_probability_q(...)`, `AggregatedConsequencesByQuantile get_consequence_result(...)`, `Empirical get_aggregate_empirical_distribution(...)` (filter → `stack_empirical_distributions(list, sum)`). SEVER MVVM (`Validation` base + `MessageReport` event + `ReportMessage` on miss → documented throw/dummy).

- [ ] **Step 1: Read the source.** `StudyAreaConsequencesByQuantile.cs` (144). Transcribe the filter+sum aggregation. SEVER MVVM.
- [ ] **Step 2: Write the failing fixture** `fixtures/metrics/study_area_consequences_by_quantile.json`, target `study_area_consequences_by_quantile`: build a list of `AggregatedConsequencesByQuantile` (multiple categories), assert `sample_mean_damage(...)` (filtered sum) + `get_aggregate_empirical_distribution(...).sample_mean()`. `"expected":"PIN"`.
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalStudyAreaConsequencesByQuantile` + `case`. Patched copy stripping MVVM.
- [ ] **Step 4: Implement**; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green.
- [ ] **Step 6: Commit** `feat(metrics): port StudyAreaConsequencesByQuantile + fixtures`.

---

### Task 4: un-sever the binned→quantile converters

**Files:** Modify `core/include/hecfda/model/metrics/aggregated_consequences_binned.hpp` (add `convert_to_single_empirical_distribution_of_consequences`), `study_area_consequences_binned.hpp` (add `convert_to_study_area_consequences_by_quantile`); Create `fixtures/metrics/binned_to_quantile.json`; Modify `test_fixtures.cpp`, the existing `patched/AggregatedConsequencesBinned.cs` + `patched/StudyAreaConsequencesBinned.cs` (restore the methods), `Program.cs`.

**Interfaces:**
- Consumes: Task-1 `DynamicHistogram::convert_to_empirical_distribution`, Task-2/3 the ByQuantile types.
- Produces: `AggregatedConsequencesByQuantile AggregatedConsequencesBinned::convert_to_single_empirical_distribution_of_consequences() const` (uses `consequence_histogram().convert_to_empirical_distribution()`); `static StudyAreaConsequencesByQuantile StudyAreaConsequencesBinned::convert_to_study_area_consequences_by_quantile(const StudyAreaConsequencesBinned&, ConsequenceType)`.

- [ ] **Step 1: Read the source.** The two `ConvertTo*` methods in `AggregatedConsequencesBinned.cs` + `StudyAreaConsequencesBinned.cs` (the ones the port severed — see the severance comments in the ported headers). Transcribe. Restore them in the two existing `patched/*.cs` emitter copies.
- [ ] **Step 2: Write the failing fixture** `fixtures/metrics/binned_to_quantile.json`, target `binned_to_quantile`: build a binned consequence (Task-4-style histogram staging from Phase 4), `convert_to_study_area_consequences_by_quantile(...)`, assert the resulting ByQuantile's `sample_mean_damage`. `"expected":"PIN"`.
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalBinnedToQuantile` + `case`.
- [ ] **Step 4: Implement** the two un-severed methods; loader + dispatch. Remove the severance stubs/comments.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green; confirm no Phase-4/5 binned fixture regressed.
- [ ] **Step 6: Commit** `feat(metrics): un-sever binned->quantile converters + fixtures`.

---

### Task 5: `ScenarioResults`

**Files:** Create `core/include/hecfda/model/metrics/scenario_results.hpp`; Create `fixtures/metrics/scenario_results.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Phase-5 `ImpactAreaScenarioResults`, Task-1 `Empirical` stacking + `DynamicHistogram::convert_to_empirical_distribution`, Task-3/4 the quantile types + converter, `CategoriedUncertainPairedData` (Phase 5).
- Produces: `ScenarioResults` — ctor `()`; `add_results(ImpactAreaScenarioResults)`, `get_results(int impact_area_id)`; the category enumerators; the AEP/assurance pass-throughs to `get_results(id)`; `double sample_mean_expected_annual_consequences(...)` (sum across impact areas), `double consequences_exceeded_with_probability_q(...)`, `Empirical get_consequences_distribution(...)` (filter → `convert_to_empirical_distribution` → `stack_empirical_distributions(sum)`), `UncertainPairedData get_accumulated_life_loss_fn_curve_data()`; `static StudyAreaConsequencesByQuantile convert_to_study_area_consequences_by_quantile(const ScenarioResults&, ConsequenceType)`; `bool equals(...)`. SEVER MVVM + XML (`WriteToXML`/`ReadFromXML`, the `ComputeDate`/`SoftwareVersion` string stamps → keep the fields as plain strings, don't stamp).

- [ ] **Step 1: Read the source.** `ScenarioResults.cs` (329). Transcribe the aggregators + the static converter. SEVER MVVM/XML/reflection.
- [ ] **Step 2: Write the failing fixture** `fixtures/metrics/scenario_results.json`, target `scenario_results`: build 1-2 `ImpactAreaScenarioResults` (with consequence realizations binned), `add_results`, assert `sample_mean_expected_annual_consequences(...)` + `get_consequences_distribution(...).sample_mean()` + a `mean_aep` pass-through. `"expected":"PIN"`.
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalScenarioResults` + `case`. Patched copy (MVVM/XML).
- [ ] **Step 4: Implement**; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green.
- [ ] **Step 6: Commit** `feat(metrics): port ScenarioResults + fixtures`.

---

### Task 6: `AlternativeResults`

**Files:** Create `core/include/hecfda/model/metrics/alternative_results.hpp`; Create `fixtures/metrics/alternative_results.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Task-3 `StudyAreaConsequencesByQuantile`, Task-2 `AggregatedConsequencesByQuantile`, Task-5 `ScenarioResults`.
- Produces: `AlternativeResults` — ctors `()` (null), `(int id, std::vector<int> analysis_years, int period_of_analysis)`; fields `scenarios_are_identical_`, `eqad_results_` (StudyAreaConsequencesByQuantile), `base_year_scenario_results_`/`future_year_scenario_results_` (ScenarioResults); `sample_mean_eqad(...)`/`sample_mean_base_year_ead(...)`/`sample_mean_future_year_ead(...)` (the `scenarios_are_identical_ ? base_scenario : eqad_results` branch), `eqad_exceeded_with_probability_q(...)`, `Empirical get_eqad_distribution(...)`/`get_base_year_ead_distribution(...)`/`get_future_year_ead_distribution(...)`, `add_consequence_results(AggregatedConsequencesByQuantile)`. SEVER MVVM `Rule`/`AddRules` validation (the future≥base+1 / PoA rules → documented no-op or keep as plain checks).

- [ ] **Step 1: Read the source.** `AlternativeResults.cs` (241). Transcribe the identical-vs-eqad delegation pattern. SEVER MVVM.
- [ ] **Step 2: Write the failing fixture** `fixtures/metrics/alternative_results.json`, target `alternative_results`: construct directly (set base/future ScenarioResults + eqad_results), assert the delegators. `"expected":"PIN"`.
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalAlternativeResults` + `case`. Patched copy (MVVM rules).
- [ ] **Step 4: Implement**; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green.
- [ ] **Step 6: Commit** `feat(metrics): port AlternativeResults + fixtures`.

---

### Task 7: `AlternativeComparisonReportResults`

**Files:** Create `core/include/hecfda/model/metrics/alternative_comparison_report_results.hpp`; Create `fixtures/metrics/alternative_comparison_report_results.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Task-6 `AlternativeResults`, Task-3 `StudyAreaConsequencesByQuantile`.
- Produces: `AlternativeComparisonReportResults` — ctor `(std::vector<AlternativeResults> with_project, AlternativeResults without_project, list×3 reduced-results)`; the enumerators; `sample_mean_eqad_reduced(...)`/`sample_mean_base_year_ead_reduced(...)`/`sample_mean_future_year_ead_reduced(...)` (→ `get_consequences_reduced_results_for_given_alternative(...).sample_mean_damage(...)`); the without/with-project delegators to `AlternativeResults`; `get_consequences_reduced_results_for_given_alternative(alt_id, get_ead, get_base_year)` (selects one of the three lists; throws on illogical combo). SEVER MVVM.

- [ ] **Step 1: Read the source.** `AlternativeComparisonReportResults.cs` (294). Transcribe. SEVER MVVM.
- [ ] **Step 2: Write the failing fixture** `fixtures/metrics/alternative_comparison_report_results.json`, target `alternative_comparison_report_results`: construct directly with reduced-results lists, assert `sample_mean_base_year_ead_reduced(...)` + a with/without delegator. `"expected":"PIN"`.
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalAlternativeComparisonReportResults` + `case`. Patched copy (MVVM).
- [ ] **Step 4: Implement**; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green.
- [ ] **Step 6: Commit** `feat(metrics): port AlternativeComparisonReportResults + fixtures`.

---

### Task 8: `Scenario` (domain)

**Files:** Create `core/include/hecfda/model/scenarios/scenario.hpp`; Create `fixtures/scenarios/scenario.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Phase-5 `ImpactAreaScenarioSimulation`, Task-5 `ScenarioResults`.
- Produces: `Scenario` — ctor `(std::vector<ImpactAreaScenarioSimulation>)` (move-only); `ScenarioResults compute(ConvergenceCriteria, bool compute_is_deterministic = false)` (loop impact areas → `impact_area.compute(...)` → `scenario_results.add_results(...)`). SEVER MVVM messaging + `Assembly` version stamp (leave `compute_date`/`software_version` empty or documented) + `CancellationToken`.

- [ ] **Step 1: Read the source.** `Scenario.cs` (51). Transcribe the fan-out loop. SEVER MVVM/reflection/threading.
- [ ] **Step 2: Write the failing fixture** `fixtures/scenarios/scenario.json`, target `scenario` (method `mean_eac`): the `ScenarioShould`/`AlternativeResults_Test` deterministic 2-impact-area compute (`compute(cc, deterministic=true)`), assert `scenario_results.sample_mean_expected_annual_consequences(...)`. `"expected":"PIN"`.
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalScenario` (build the impact-area sims via the Phase-5 builder from the fixture construct, `Compute(cc, true)`, dispatch) + `case "scenario"`. Patched copy (MVVM/reflection).
- [ ] **Step 4: Implement**; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green.
- [ ] **Step 6: Commit** `feat(scenarios): port Scenario (impact-area fan-out) + fixtures`.

---

### Task 9: `Alternative` — `ComputeEqad` + `AnnualizationCompute` (the EqAD headline)

**Files:** Create `core/include/hecfda/model/alternatives/alternative.hpp`; Create `fixtures/alternatives/alternative.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Task-5 `ScenarioResults`, Task-6 `AlternativeResults`, Task-2/3 the quantile types, `Empirical::fit_to_sample`, `AggregatedConsequencesBinned::consequence_histogram().inverse_cdf/sample_mean`.
- Produces (free functions / static-style in `hecfda::model::alternatives`): `double compute_eqad(double base_year_ead, int base_year, double most_likely_future_ead, int most_likely_future_year, int period_of_analysis, double discount_rate)` (interpolate → present_value → PVIFA); `AlternativeResults annualization_compute(double discount_rate, int period_of_analysis, int alternative_results_id, const ScenarioResults* base, const ScenarioResults* future, int base_year, int future_year)`; `bool can_compute(int base_year, int future_year, int period_of_analysis)`. Internal `interpolate`/`present_value_compute`/`into_equivalent_annual_terms`/`iterate_on_eqad` (`probabilitySteps=25000`, `Parallel.For`→serial), `run_annualization_compute`, `process_base_and_future_year_scenario_results`.

- [ ] **Step 1: Read the source.** `Alternative.cs` (316) — `ComputeEqad` (274-314: the interpolate/PV/PVIFA math), `AnnualizationCompute`, `CanCompute`, `IterateOnEqad` (25000 quantile steps via `InverseCDF`, `Parallel.For`→serial + `ConcurrentBag`→`std::vector`), `RunAnnualizationCompute` (identical-scenario short-circuit; single-scenario null-coalesce), `ProcessBaseAndFutureYearScenarioResults` (match by ImpactAreaID + the 4-key; life-loss exclusion; unmatched-future throw). Transcribe verbatim; document the `Parallel.For`→serial (per-quantile-step independent → identical) + the `Empirical.SampleMean` force-set.
- [ ] **Step 2: Write the failing fixture** `fixtures/alternatives/alternative.json`, target `alternative`. Multiple case kinds:
  - `compute_eqad` (method) — the 8 `AlternativeTest.ComputeEEAD_Test` `[InlineData]` rows (baseEAD/baseYr/futEAD/futYr/poa/rate → expected). Cross-check: {38835.3, 255.68, 35000, 41893.12, 40680.87, 378.72, 35000, 44279.92} (abs tol 0.005). THIS IS THE TIGHTEST PIN — do it first.
  - `annualization_single_base` / `annualization_single_future` — build a ScenarioResults directly from a `Range(100,100)`/`Range(200,100)` histogram, `annualization_compute(0.0275, 50, 1, base, null, 2023, 2072)` (or future-only), assert `sample_mean_eqad == input SampleMean` EXACT (149.5 / 249.5). Exercises the single-scenario no-discount path + life-loss exclusion.
  - `annualization_lifeloss_excluded` — assert the EqAD results contain Damage but NOT LifeLoss.
  - `"expected":"PIN"` where captured; the ComputeEqad literals are the cross-check.
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalAlternative` (dispatch `compute_eqad` scalar + build ScenarioResults from the fixture histograms + `AnnualizationCompute` + dispatch the AlternativeResults getters) + `case "alternative"`. Patched copy (progress/threading).
- [ ] **Step 4: Implement**; loader + dispatch. DEBUG until the 8 ComputeEqad rows reproduce to 0.005.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green; the ComputeEqad table + the single-scenario exact-SampleMean cases reproduce.
- [ ] **Step 6: Commit** `feat(alternatives): port Alternative.ComputeEqad + AnnualizationCompute (EqAD) + fixtures`.

---

### Task 10: `AlternativeComparisonReport` (with/without benefits)

**Files:** Create `core/include/hecfda/model/alternative_comparison_report/alternative_comparison_report.hpp`; Create `fixtures/alternatives/alternative_comparison_report.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Task-6 `AlternativeResults`, Task-7 `AlternativeComparisonReportResults`, Task-3 `StudyAreaConsequencesByQuantile`, Task-1 `Empirical::stack_empirical_distributions(subtract)`, Task-4 `AggregatedConsequencesBinned::convert_to_single_empirical_distribution_of_consequences`.
- Produces: `enum class AlternativeComparisonReportType { BaseYearEADReduced, FutureYearEADReduced }`; `AlternativeComparisonReportResults compute_alternative_comparison_report(const AlternativeResults& without_project, const std::vector<AlternativeResults>& with_project)`; internal `compute_distribution_of_eqad_reduced`, `compute_distribution_ead_reduced(..., type)`, `iterate_on_consequence_distribution_result(with, without, iterate_on_with_project)` (the `stack_empirical_distributions([without, with], subtract)` = without−with). SEVER progress/OperationResult.

- [ ] **Step 1: Read the source.** `AlternativeComparisonReport.cs` (247). Transcribe the three reduction passes + the subtraction seam (`without − with`). SEVER progress/logging.
- [ ] **Step 2: Write the failing fixture** `fixtures/alternatives/alternative_comparison_report.json`, target `alternative_comparison_report`. The `AlternativeComparisonReportConsolidationTests` deterministic subtraction cases: build with/without `AlternativeResults` directly from constant histograms, `compute_alternative_comparison_report(without, [with])`, assert `sample_mean_base_year_ead_reduced` = 1000−300 = **700**, future = 2000−500 = **1500**, life-loss base = 10−3 = **7**, future = 20−5 = **15**, and the equal-with/without → **0** case. `"expected":"PIN"` + the exact subtraction literals (abs tol 0.1).
- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalAlternativeComparisonReport` + `case`. Patched copy.
- [ ] **Step 4: Implement**; loader + dispatch.
- [ ] **Step 5: Pin + verify.** `ctest` + gate green; the 700/1500/7/15/0 subtraction reproduces.
- [ ] **Step 6: Commit** `feat(alternatives): port AlternativeComparisonReport (with/without benefits) + fixtures`.

---

### Task 11: end-to-end capstone (`DefaultDataComputeOutcomes` Muncie + simulated Alternative/comparison)

**Files:** Create `fixtures/alternatives/end_to_end.json`; Modify `test_fixtures.cpp`, `Program.cs` (extend the `scenario`/`alternative`/`alternative_comparison_report` dispatch for the full simulated pipeline). No new core code expected — this validates the whole chain.

**Interfaces:**
- Consumes: the full Task-8/9/10 domain + the Phase-5 simulation.
- Produces: no new interface — the capstone integration validation.

- [ ] **Step 1:** Confirm the full pipeline runs: `Scenario.compute` (seeded or deterministic) → `Alternative.annualization_compute` → `AlternativeComparisonReport`.
- [ ] **Step 2: Write the failing fixture** `fixtures/alternatives/end_to_end.json`. Cases:
  - `AlternativeResults_Test` (deterministic simulated, cc(iter,iter), the 3-node stage-damage): `annualization_compute(.0275, 50, 1, sr, sr2, 2023, 2072/2050)` → `sample_mean_eqad` = {208213.8061, 239260.1814} and the base==future → EqAD==EAD (150000) cases (rel tol 0.01).
  - `AlternativeComparisonReportTests.ComputeEqad` (simulated levee with/without): `eqad_reduced` = {51442, 59410}, base-year-reduced = 36500, future = 75000 (rel tol 0.1); plus the two EXACT cross-checks (report `SampleMeanWithoutProjectBaseYearEAD` == alt `SampleMeanBaseYearEAD`).
  - `DefaultDataComputeOutcomes.WithoutAnalytical` (Muncie, seeded 50000-iter — SLOW; use it as a loose smoke test, rel tol 0.11): commercial EAD ≈ 310937.1, residential ≈ 295506.53; and `AnalyticalWithRegUnreg` residential ≈ 132323.23, commercial ≈ 98588.63 (rel tol 0.2). (If the 50000-iteration Muncie cases make ctest impractically slow, keep ONE representative Muncie case and note the reduction; the `AlternativeResults_Test` deterministic cases are the primary end-to-end pins.)
  - `"expected":"PIN"` (literals are cross-checks).
- [ ] **Step 3: Wire emitter, run, confirm FAIL/capture.** Extend the emitter dispatch for the simulated pipeline.
- [ ] **Step 4: Pin + verify.** `ctest` + gate green; the end-to-end EqAD + benefits values reproduce within their tolerances.
- [ ] **Step 5: Commit** `feat(alternatives): end-to-end scenario->alternative->comparison capstone validation`.

---

### Task 12: R/Python representative subset + FINAL closeout

**Files:** Modify `hecfdar/src/*.cpp` (+`cpp11::cpp_register`), `hecfdar/tests/testthat/test-fixtures.R`, `hecfdapy/src/**`, `hecfdapy/tests/test_fixtures.py`; Modify `.claude/CLAUDE.md`, `.claude/PLAN.md`, `.superpowers/sdd/progress.md`, memory file.

**Interfaces:**
- Produces: R/Python bindings + fixture-runner dispatch for a representative subset: `alternative` (the EqAD annualization — `compute_eqad` + `annualization_compute`, the phase's headline math) and `scenario` (the impact-area fan-out). Document that the other targets traverse the identical binding + compiled core.

- [ ] **Step 1:** Choose + document the subset (`alternative` + `scenario`) in `.claude/CLAUDE.md`.
- [ ] **Step 2:** Add R bindings mirroring the C++ dispatch; `cpp11::cpp_register("hecfdar")`; `R CMD INSTALL --preclean hecfdar`. (Use the deterministic `compute_eqad` + direct-construction cases to keep R/Python fast — NOT the 50000-iteration Muncie.)
- [ ] **Step 3:** Extend `test-fixtures.R` to load `fixtures/alternatives/alternative.json` + `fixtures/scenarios/scenario.json`; `Rscript -e 'testthat::test_local("hecfdar")'`.
- [ ] **Step 4:** Mirror in Python (pybind11; `pip install --force-reinstall --no-deps ./hecfdapy`; extend `test_fixtures.py`); `pytest hecfdapy/tests -q`.
- [ ] **Step 5:** Add any missing compare mode symmetrically; otherwise skip.
- [ ] **Step 6: Full four-leg exit gate.** `make test-core`, `make test-r`, `make test-py PYTHON=~/venv/hecfdapy/bin/python`, `make oracles` — all green; record the new gate count. Confirm the cross-language invariants (`sample_and_integrate(1234)==24.425549382855987`, `rng_digest`) and the Phase-3/4/5 fixtures (structures, stage-damage curves, EAD oracles) still pass.
- [ ] **Step 7: Docs + memory — THE PORT IS COMPLETE.** Update `.claude/CLAUDE.md` (Status → **all 6 phases complete, port done**; add a paragraph on `hecfda::model::{scenarios,alternatives,alternative_comparison_report}` + the un-severed Empirical/quantile chain + the EqAD annualization math + the with/without benefits; new faithful bugs; the R/Python subset). Update `.claude/PLAN.md` (mark Phase 6 done; the port is complete — note any remaining follow-ups from the whole-branch reviews across all phases). Update the memory file with a "PORT COMPLETE" summary (all 6 phases, final gate count, the C++/R/Python-identical-to-C# achievement, the deferred/severed surfaces that were never in scope: GIS/RAS/SQLite/LifeSim/MVVM/XML).
- [ ] **Step 8:** Commit `docs(alternatives): Phase 6 closeout -- port complete, bindings, exit gate, status`. Stop. Do NOT open a PR or merge — the controller runs `superpowers:finishing-a-development-branch` and the user chooses.

---

## Self-Review

- **Spec coverage:** design-spec Phase 6 ("Scenarios & alternatives — `Scenario`, `Alternative` (period-of-analysis EAD, EqAD), `AlternativeComparisonReport` (with/without)"). Tasks 8/9/10 cover the three domain classes; Tasks 2-7 port the 5 deferred metrics types; Task 1 + Task 4 un-sever the Empirical/histogram/quantile chain they need; Task 11 is the end-to-end capstone. This completes the numeric core.
- **Type consistency:** `Empirical` stacking + `DynamicHistogram::convert_to_empirical_distribution` (T1) → `AggregatedConsequencesByQuantile` (T2) → `StudyAreaConsequencesByQuantile` (T3) → the binned converters (T4) → `ScenarioResults` (T5) → `AlternativeResults` (T6) → `AlternativeComparisonReportResults` (T7); `Scenario` (T8) produces `ScenarioResults`; `Alternative` (T9) consumes `ScenarioResults` → `AlternativeResults`; `AlternativeComparisonReport` (T10) consumes `AlternativeResults` → `AlternativeComparisonReportResults`. Every consumer's signature is declared in its producer task.
- **Oracle discipline:** the tightest pin (`ComputeEqad`, 8 scalar rows) first; then direct-construction annualization (exact-SampleMean single-scenario) + comparison subtraction (700/1500/7/15/0); then deterministic-simulated (208213.8061) + the loose Muncie smoke test. All `"PIN"`-then-gate-captured; upstream literals are cross-checks. No new seeded MC — the deterministic quantile-walk reproduces exactly.
- **Placeholder scan:** each task has exact paths, upstream line ranges, fixture inputs with literals, and the emitter/runner wiring. The un-sever tasks (1, 4) restore severed methods to existing headers + patched emitter copies. The only deferral is gate-captured expecteds.
- **Severance coherence:** MVVM/messaging/XML/reflection/threading are cut at the same boundary across every new type; the `Parallel.For`→serial is safe (per-quantile-step / per-impact-area independent). The Empirical/quantile chain un-severance is the deliberate inverse of the Phase-4/5 deferrals, each documented.
