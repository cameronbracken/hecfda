# HEC-FDA Port — Phase 1: Statistics Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the full `HEC.FDA.Statistics` foundation — the remaining 12 distributions, `SampleStatistics`, the complete `SpecialFunctions`, the validation subsystem, `IDistributionFactory`/`IDistributionEnum`, `ConvergenceCriteria`, and `DynamicHistogram` — each fixture-validated in C++/R/Python and reproduced against real C# by the dotnet oracle gate.

**Architecture:** Extend the Phase-0 shared C++17 core. A polymorphic factory-based dispatch (built once in Task A4) means adding a distribution is a fixture file plus a factory `case`, not new binding glue. Every ported file mirrors the C# and carries a `// ported from: <path> @ <sha>` header.

**Tech Stack:** C++17 (doctest), cpp11 (R), pybind11 (Python), dotnet gate (net9 build on net10 runtime via `DOTNET_ROLL_FORWARD=Major`).

**Branch:** `phase1` (off `main` @ the Phase-0 merge). Upstream pinned submodule `upstream/HEC-FDA` @ `f63682a86a30dc306a105689714a92bfd95956c5`.

## Global Constraints

- **C++17 only.** No C++20. Self-contained core (no external deps; the vendored `doctest.h`/`json.hpp` are test-only).
- **Structural mirroring:** each ported C++ file opens with `// ported from: <upstream-path> @ f63682a86a30dc306a105689714a92bfd95956c5`; class/method names and order mirror the C# source. Transcribe math method-for-method; do NOT invent it.
- **Portability:** never `M_PI` (use `hecfda::kPi`); `-Wall/-Wextra` for non-MSVC only; no namespace alias `gamma`/`stat`.
- **No hardcoded oracle values in test code.** Oracle values live ONLY in `fixtures/*.json`. Each distribution's oracles come from its `HEC.FDA.StatisticsTests/Distributions/*Tests.cs` file (curated into a fixture) and are reproduced by the dotnet gate.
- **Namespaces:** `hecfda::statistics`, `hecfda::statistics::distributions`.
- **RNG parity:** any Monte Carlo / sampling fixture carries a seed and reads it from the case-level `seed` field (Phase-0 convention).
- **Fixture runner DRY:** distributions are constructed through the factory and dispatched on `IDistribution` (the base) via the generic `dist_*` glue built in Task A4. Adding a distribution = a new fixture file + a factory `case` + a couple of dispatch entries. No per-distribution binding functions.
- **Validation-as-values:** the C# distributions carry `AddSinglePropertyRule`/`Validate()`/`HasErrors`/`ErrorLevel`. Port a minimal validation subsystem (Task A1) so fixtures can assert `has_errors` (bool) and `error_level` (string/int).
- **Commits:** SSH-signed automatically; identity `Cam Bracken <cameron.bracken@pm.me>`; conventional-commit; push only when asked.
- **ABI (R):** after a core class-layout change, `R CMD INSTALL --preclean hecfdar`. After editing `hecfdar/src/*.cpp`, re-run `cpp11::cpp_register`. CI does NOT run `cpp_register` (generated files are committed).
- **Python dev venv:** `~/venv/hecfdapy`. Local `make test-py PYTHON=~/venv/hecfdapy/bin/python`.
- **Definition of done per task:** `cmake --build core/build && ctest` green; R fixtures pass; Python fixtures pass; `python3 tools/verify_oracles.py` green (real C# reproduces every fixture); then commit.

## Distribution scope (12 remaining; Normal done in Phase 0)

| Distribution | C# source | Test oracle | Factory / enum |
|---|---|---|---|
| Uniform (EXEMPLAR) | `Distributions/Uniform.cs` | `UniformTests.cs` | `FactoryUniform`, enum `Uniform=2` |
| Triangular | `Distributions/Triangular.cs` | `TriangularTests.cs` | `FactoryTriangular`, `Triangular=3` |
| Deterministic | `Distributions/Deterministic.cs` | `DeterministicTests.cs` | enum `Deterministic=6` |
| LogNormal | `Distributions/LogNormal.cs` | `LogNormalTests.cs` | enum `LogNormal=5` |
| TruncatedNormal | `Distributions/TruncatedNormal.cs` | (via NormalTests / gate) | `FactoryTruncatedNormal`, `TruncatedNormal=101` |
| TruncatedLogNormal | `Distributions/TruncatedLogNormal.cs` | (gate) | — |
| PearsonIII | `Distributions/PearsonIII.cs` | `PearsonIIITests.cs` | — |
| LogPearson3 | `Distributions/LogPearson3.cs` | `LogPearsonIIITests.cs` | `FactoryLogPearsonIII`, `LogPearsonIII=4` |
| TruncatedLogPearson3 | `Distributions/TruncatedLogPearson3.cs` | (gate) | `FactoryTruncatedLogPearsonIII` |
| Gamma | `Distributions/Gamma.cs` | (gate) | — |
| ShiftedGamma | `Distributions/ShiftedGamma.cs` | `ShiftedGammaTests.cs` | — |
| Empirical | `Distributions/Empirical.cs` | `EmpiricalTests.cs` | enum `Empirical=8` |

## File Structure

```
core/include/hecfda/statistics/
  validation.hpp                         # A1: rule + error-level subsystem (mirrors ValidationErrorLogger)
  special_functions.hpp                  # A2: extend to the full SpecialFunctions.cs surface
  sample_statistics.hpp                  # A3: SampleStatistics + ISampleStatistics
  distributions/
    i_distribution.hpp                   # A4: extend (Type enum, PDF/CDF/InverseCDF/Fit/Sample/Equals)
    continuous_distribution.hpp          # A4: extend base (validation, GenerateRandomSamplesofNumbers)
    i_distribution_enum.hpp              # A4: DistributionType enum (mirror IDistributionEnum values)
    i_distribution_factory.hpp           # A4: factory (switch on type + params)
    uniform.hpp triangular.hpp deterministic.hpp lognormal.hpp normal.hpp(exists)
    truncated_normal.hpp truncated_lognormal.hpp
    pearson3.hpp logpearson3.hpp truncated_logpearson3.hpp
    gamma.hpp shifted_gamma.hpp empirical.hpp
  histograms/
    i_histogram.hpp dynamic_histogram.hpp   # C2
  convergence/convergence_criteria.hpp      # C1
  distributions/uncertain_to_deterministic_converter.hpp  # D1
core/tests/  (new test_*.cpp per task; the generic test_fixtures.cpp gains a factory dispatch in A4)
fixtures/distributions/*.json  fixtures/histograms/*.json  fixtures/convergence/*.json
hecfdar/src/glue.cpp  hecfdapy/src/bindings/glue.cpp   # A4: generic dist_construct/dist_eval
tools/oracle_emitter/Program.cs                        # A4: factory dispatch for the `distribution` target
```

---

## Group A — Foundation

### Task A1: Validation subsystem

**Files:** Create `core/include/hecfda/statistics/validation.hpp`; Test `core/tests/test_validation.cpp`.

**Interfaces:**
- Produces: `hecfda::statistics::ErrorLevel` (`enum class { None=0, Minor, Major, Severe, Fatal }` — confirm exact values/names against `HEC.MVVMFramework.Base.Enumerations.ErrorLevel` in the submodule), and `Validation` base with `add_single_property_rule(std::string prop, std::function<bool()> predicate, std::string message, ErrorLevel level)`, `void validate()`, `bool has_errors() const`, `ErrorLevel error_level() const` (the max level of failing rules), `std::vector<std::string> errors() const`.

- [ ] **Step 1: Read the upstream `ErrorLevel` enum and `ValidationErrorLogger`/`Validation`** in `upstream/HEC-FDA/HEC.MVVMFramework.Base/` to get exact enum names/values and the `Validate()`/`HasErrors` semantics (a rule holds a predicate + message + level; `Validate()` evaluates all rules; `HasErrors` is true if any failed; `ErrorLevel` is the highest failed level).

- [ ] **Step 2: Write the failing test `test_validation.cpp`** — a tiny `Validation` subclass with two rules (one Fatal `x>0`, one Minor `x>1`); assert: value 2 → no errors; value 1 → has_errors, error_level Minor; value 0 → has_errors, error_level Fatal. (doctest; DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN.)

- [ ] **Step 3: Run to confirm it fails** (missing header).
- [ ] **Step 4: Implement `validation.hpp`** — a rule is `{std::string property, std::function<bool()> is_valid, std::string message, ErrorLevel level}`; `validate()` runs all predicates and records failures; `has_errors()`/`error_level()`/`errors()` report them. Provenance header cites the MVVMFramework path + SHA.
- [ ] **Step 5: Run to confirm pass. Step 6: Commit** `feat(stats): validation rule subsystem (ErrorLevel + Validate/HasErrors)`.

### Task A2: Full SpecialFunctions

**Files:** Extend `core/include/hecfda/statistics/special_functions.hpp`; Test `core/tests/test_special_functions.cpp`; Fixture `fixtures/special_functions/special_functions.json` (oracles from `HEC.FDA.StatisticsTests/MathematicsTests.cs` where present, else pinned by the gate).

**Interfaces:** Produces every static in `SpecialFunctions.cs` the distributions call. Phase 0 ported `log_gamma` + `reg_incomplete_gamma`(+ P/Q/CF). Add the rest verbatim: whatever the Pearson/Gamma/LogNormal InverseCDFs need (e.g. the inverse incomplete gamma / `GammaInverseCDF`, `erf`/`erfc` if used, Beta/incomplete-beta if present).

- [ ] **Step 1:** Read `SpecialFunctions.cs` in full; list the public statics not yet ported. Read the Pearson/Gamma/LogNormal distribution files to see which they call — port exactly those (do not port dead code; note any skipped as `skipped:unused` in the report).
- [ ] **Step 2:** Write a failing `fixtures/special_functions/special_functions.json` with a few oracle points per added function (from `MathematicsTests.cs` if it covers them, else compute-then-pin-via-gate like the Phase-0 Normal cdf). Extend `test_fixtures.cpp` with a `special_functions` target dispatch (scalar args, abs/rel modes) OR add a dedicated `test_special_functions.cpp` that asserts a handful directly.
- [ ] **Step 3–6:** RED (fixture fails) → transcribe the functions from C# verbatim → GREEN (ctest + gate) → commit `feat(stats): complete SpecialFunctions surface`.

### Task A3: SampleStatistics

**Files:** Create `core/include/hecfda/statistics/sample_statistics.hpp` (+ `i_sample_statistics` if kept separate); Test via `fixtures/distributions/sample_statistics.json` (oracles from `SummaryStatisticsShould.cs`).

**Interfaces:** Produces `hecfda::statistics::SampleStatistics` ctor `SampleStatistics(std::vector<double> data)` with `mean()`, `variance()`, `standard_deviation()`, `median()`, `skewness()`, `min()`, `max()`, `sample_size()`. Transcribe the moment/median/skew formulas from `SampleStatistics.cs` exactly (they drive every distribution's `Fit`).

- [ ] **Step 1:** Read `SampleStatistics.cs` + `SummaryStatisticsShould.cs` (+ `Resources/SampleData.cs` for the input arrays).
- [ ] **Step 2:** Failing fixture with the test's sample array + expected mean/variance/stddev/median/skew/min/max. Add a `sample_statistics` dispatch to `test_fixtures.cpp`.
- [ ] **Step 3–6:** RED → port → GREEN (ctest + R + Python + gate) → commit. NOTE: this replaces the minimal inline `fit` mean/sd used by Normal in Phase 0 — update `normal.hpp::fit` to use `SampleStatistics` and confirm the Phase-0 normal fixture still passes.

### Task A4: Distribution base, enum, factory, and the generic four-runner dispatch (the enabling task)

**Files:** Extend `i_distribution.hpp`, `continuous_distribution.hpp`; create `i_distribution_enum.hpp`, `i_distribution_factory.hpp`; extend `core/tests/test_fixtures.cpp`, `hecfdar/src/glue.cpp` + `hecfdar/R/dist.R` (or extend `analysis`-style), `hecfdapy/src/bindings/glue.cpp` + `hecfdapy/src/hecfdapy/__init__.py`, and `tools/oracle_emitter/Program.cs`.

**Interfaces (the contract every later distribution task relies on):**
- `DistributionType` enum mirroring `IDistributionEnum` (Normal=1, Uniform=2, Triangular=3, LogPearsonIII=4, LogNormal=5, Deterministic=6, IHistogram=7, Empirical=8, TruncatedNormal=101; plus internal types PearsonIII/Gamma/ShiftedGamma/TruncatedLogNormal/TruncatedLogPearson3 as the distributions define them).
- `IDistributionFactory::create(DistributionType type, const std::vector<double>& params) -> std::unique_ptr<IDistribution>` — a `switch` mapping type+params to the concrete ctor (params order documented per type, matching the C# `Factory*` signatures).
- Generic glue: R `hecfda_dist_eval(type_str, params, method, x)` and `hecfda_dist_fit(type_str, params?, data)`; Python `dist_eval(type, params, method, x)`; each constructs via the factory and calls `pdf`/`cdf`/`inverse_cdf` (or `has_errors`/`error_level` after `validate`). C++ `test_fixtures.cpp` gains a `distribution` target that reads `{type, params}` and dispatches `pdf`/`cdf`/`inverse_cdf`/`has_errors`/`error_level`. The dotnet emitter gains the same `distribution` dispatch via `IDistributionFactory` + `dynamic`/switch.
- Fixture schema for distributions: `{target:"distribution", cases:[{name, construct:{type, params:[...]}, assertions:[{method, args, expected, tol, mode}]}]}` where method ∈ `pdf|cdf|inverse_cdf|has_errors|error_level|fit_*`.

- [ ] **Step 1:** Extend `i_distribution.hpp` to the full `IDistribution` surface (`type()`, `sample_size()`, `pdf`/`cdf`/`inverse_cdf`, `fit(data)->unique_ptr`, `sample(packet)`, `equals`), and `continuous_distribution.hpp` to derive from `Validation` (Task A1) and carry `sample_size_` + `GenerateRandomSamplesofNumbers`/`Sample(iteration)` (port from `ContinuousDistribution.cs`). Migrate the Phase-0 `Normal` onto the extended base (keep its fixture green).
- [ ] **Step 2:** Create `i_distribution_enum.hpp` (the enum) and `i_distribution_factory.hpp` (`create(type, params)` with a `switch`; initially only `Normal` + `Uniform` cases — later tasks add cases).
- [ ] **Step 3:** Wire the generic `distribution` dispatch into all four runners (C++ test_fixtures, R glue + test-fixtures.R, Python glue + test_fixtures.py, dotnet emitter). Re-migrate the Phase-0 `fixtures/distributions/normal.json` to the `{type:"Normal", params:[mean,sd,sampleSize]}` construct shape and confirm it still passes in all four.
- [ ] **Step 4:** Port `Uniform` (the EXEMPLAR, see below) as the second factory case, with its fixture, to prove the generic path end to end.
- [ ] **Step 5:** Full four-way validation + `cpp_register` + `--preclean` R rebuild. Commit `feat(stats): distribution base/enum/factory + generic four-runner dispatch (Normal, Uniform)`.

**This task ends with Uniform working through the generic path; all later distribution tasks are "add a factory case + a fixture".**

---

## Distribution EXEMPLAR — Uniform (worked in Task A4 Step 4; every later card references this)

**Files:** Create `core/include/hecfda/statistics/distributions/uniform.hpp`; Fixture `fixtures/distributions/uniform.json`; factory `case DistributionType::Uniform:` in `i_distribution_factory.hpp`.

Port `Uniform.cs` verbatim: fields `min_`,`max_`; `Type() => Uniform`; `pdf`/`cdf` with the `Max==Min` guard and the `[Min,Max]` support; `inverse_cdf(p) => min_ + (max_-min_)*p`; `fit(sample) => Uniform(stats.min, stats.max, stats.sample_size)`; the three validation rules (`Min<=Max` Fatal, `Min<Max` Minor, `SampleSize>0` Fatal) via `add_single_property_rule`. Factory case: `create(Uniform, params)` → `Uniform(params[0], params[1], (int)params[2])` (min, max, sampleSize).

Fixture `uniform.json` (oracles verbatim from `UniformTests.cs`):
- `inverse_cdf` cases: construct `{type:"Uniform", params:[0,1,1]}` → inverse_cdf(0.5)=0.5, (0.25)=0.25, (0.75)=0.75, (0.95)=0.95; `[1,2,1]` → 1.5/1.25/1.75/1.95; `[1,3,1]` → 2/1.5/2.5/2.90 (mode abs, tol 1e-5 per the test's 5-digit precision).
- validation cases (bool/error_level modes): `[0,-1,1]` → has_errors true; `[0,0,1]` → has_errors true AND error_level "Minor"; `[0,1,1]` → has_errors false; `[-1,2,1]` → has_errors false.

Verify: ctest + R + Python + gate all reproduce. This card is the template: every distribution below gives only its distribution-specific facts and says "follow the Uniform exemplar".

---

## Group B — Distributions (each: follow the Uniform exemplar)

Each task: read the C# source + its test file; port the `.hpp` (PDF/CDF/InverseCDF/Fit + validation rules) verbatim; add the factory `case`; add a `fixtures/distributions/<name>.json` from the test's `[InlineData]` oracles (+ validation cases); wire nothing else (the generic dispatch already handles it); verify four ways; commit. Below are only the per-distribution specifics.

### Task B1: Triangular
`Triangular.cs` / `TriangularTests.cs`. Ctor `(min, mostLikely, max, sampleSize=1)`, `FactoryTriangular`, enum `Triangular=3`. Piecewise PDF/CDF and the two-branch InverseCDF (below/above the mode split at `F(mostLikely)`). Validation rules per the C# `addRules`. Fixture from `TriangularTests` InlineData.

### Task B2: Deterministic
`Deterministic.cs` / `DeterministicTests.cs`. Single value; PDF is a spike, CDF a step, InverseCDF returns the value. enum `Deterministic=6`. Needed by the UncertainToDeterministic converter (D1). Fixture from `DeterministicTests`.

### Task B3: LogNormal
`LogNormal.cs` / `LogNormalTests.cs`. Ctor `(mean, sd, sampleSize)` on the LOGGED scale (read the file for base-10 vs natural log — LogNormal here typically works in log10 space). PDF/CDF/InverseCDF via the Normal in log space; `fit` logs the sample. enum `LogNormal=5`. Fixture from `LogNormalTests`.

### Task B4: TruncatedNormal
`TruncatedNormal.cs`. Ctor `(mean, sd, min, max, sampleSize)`, `FactoryTruncatedNormal`, enum `TruncatedNormal=101`. Renormalizes the Normal CDF over `[min,max]` (density outside reassigned to the truncation values per the enum doc). Depends on Normal. Oracle: no dedicated test file — pin the fixture via the dotnet gate (compute-then-gate).

### Task B5: TruncatedLogNormal
`TruncatedLogNormal.cs`. Truncated LogNormal. Depends on LogNormal. Oracle via the gate.

### Task B6: PearsonIII
`PearsonIII.cs` / `PearsonIIITests.cs`. Ctor `(mean, sd, skew, sampleSize)`. Uses the incomplete-gamma inverse from SpecialFunctions (A2). Fixture from `PearsonIIITests`.

### Task B7: LogPearson3
`LogPearson3.cs` / `LogPearsonIIITests.cs`. Ctor `(mean, sd, skew, sampleSize)` on the logged scale, `FactoryLogPearsonIII`, enum `LogPearsonIII=4`. The flood-frequency workhorse; PearsonIII in log10 space. Depends on PearsonIII/SpecialFunctions. Fixture from `LogPearsonIIITests`.

### Task B8: TruncatedLogPearson3
`TruncatedLogPearson3.cs`. Ctor `(mean, sd, skew, min, max, sampleSize)`, `FactoryTruncatedLogPearsonIII`. Truncated LogPearson3. Depends on LogPearson3. Oracle via the gate.

### Task B9: Gamma
`Gamma.cs`. Ctor per the file. Depends on SpecialFunctions incomplete gamma. Oracle via the gate (no dedicated test).

### Task B10: ShiftedGamma
`ShiftedGamma.cs` / `ShiftedGammaTests.cs`. Gamma with a location shift. Depends on Gamma/SpecialFunctions. Fixture from `ShiftedGammaTests`.

### Task B11: Empirical
`Empirical.cs` (551 LOC) / `EmpiricalTests.cs`. Ctor from `(cumulativeProbabilities[], values[])`; PDF/CDF/InverseCDF by interpolation over the ordinate pairs; `fit` builds from a sample. enum `Empirical=8`. This is the largest distribution — its own task. It underpins graphical/empirical frequency curves in the Model. Fixture from `EmpiricalTests` (multiple InlineData blocks). May need a bespoke `construct` shape (two arrays rather than scalar params) — extend the factory/dispatch to accept `{type:"Empirical", probabilities:[...], values:[...]}` and document it.

---

## Group C — Histogram & Convergence

### Task C1: ConvergenceCriteria
**Files:** `core/include/hecfda/statistics/convergence/convergence_criteria.hpp`; Fixture `fixtures/convergence/convergence_criteria.json`.
Port `ConvergeCriteria.cs`: ctor `(minIterations=50000, maxIterations=500000, zAlpha=1.96039491692543, tolerance=.01)`, accessors `min_iterations()`/`max_iterations()`/`z_alpha()`/`tolerance()`/`iteration_count()` (default 100), `equals()`. Small. Fixture asserts the defaults + a custom construct round-trip (from `ConvergenceCriteriaTests.cs`, dropping the XML serialization case). Verify four ways.

### Task C2: DynamicHistogram
**Files:** `core/include/hecfda/statistics/histograms/{i_histogram,dynamic_histogram}.hpp`; Fixture `fixtures/histograms/dynamic_histogram.json` (oracles from `HistogramTests.cs`); its own `test_dynamic_histogram.cpp`.
The MC result accumulator (863 LOC) — the biggest single task. Port: the four numeric ctors (skip the XML one), `AddObservationToHistogram`/`AddObservationsToHistogram` (the dynamic bin-growth logic), `HistogramMean`/`HistogramVariance`/`HistogramStandardDeviation`, `FindBinCount`, `PDF`/`CDF`/`InverseCDF`, `IsHistogramConverged`/`EstimateIterationsRemaining` (needs ConvergenceCriteria from C1 + the Normal z-quantile), `Min`/`Max`/`SampleMin`/`SampleMax`/`SampleMean`/`SampleVariance`. SKIP (documented severances): `ToXML`/`ReadFromXML`, `HistogramDebugVisualizer`, `ConvertToEmpiricalDistribution` (defer until Empirical + a caller needs it — or include if cheap). Fixture from `HistogramTests` (Minimum/Maximum/Mean/StandardDeviation/HistogramStandardDeviation + InvCDF/CDF/PDF InlineData over a known binWidth+data). Because the histogram is an `IHistogram` used by the compute engine, keep its numeric API faithful. Verify four ways. Bind a minimal user-facing histogram constructor in R/Python if the fixture dispatch needs it (add a `histogram` target to the runners, analogous to `distribution`).

---

## Group D — Integration & closeout

### Task D1: UncertainToDeterministicDistributionConverter + deterministic UPD path
**Files:** `core/include/hecfda/statistics/distributions/uncertain_to_deterministic_converter.hpp`; extend `uncertain_paired_data.hpp`.
Port `UncertainToDeterministicDistributionConverter.cs` (maps each IDistribution to a `Deterministic` at its central value). Then un-defer the Phase-0-severed `SamplePairedData(iterationNumber, retrieveDeterministicRepresentation=true)` deterministic branch in `UncertainPairedData` (it converts each Yval to Deterministic). Add a fixture exercising the deterministic sample path. Depends on Deterministic (B2). Verify four ways.

### Task D2: Phase-1 closeout — full validation + docs
**Files:** update `.claude/CLAUDE.md` + `.claude/PLAN.md` status; ensure every new fixture is in the R/Python vendored trees (they are symlinked, so automatic); run the complete gate.
- [ ] Run `make test-core && make test-r && make test-py PYTHON=~/venv/hecfdapy/bin/python && make oracles` — all green, the gate reproduces every Phase-0 + Phase-1 fixture (report the counts).
- [ ] Update `.claude/CLAUDE.md`/`PLAN.md`: Phase 1 COMPLETE; list the 12 distributions + histogram + convergence + sample statistics + full special functions; carry forward the `lower_bound`/`Array.BinarySearch` duplicate-value risk into Phase 2 (paired-data). Note any documented severances (Histogram XML, ConvertToEmpiricalDistribution if deferred, distributions with gate-only oracles: TruncatedNormal/TruncatedLogNormal/Gamma/TruncatedLogPearson3).
- [ ] Commit; the branch is ready for a PR (human-gated).

---

## Self-Review

**Spec coverage:** the design spec's Phase 1 ("Numerics math + RNG foundation" + "all univariate distributions" equivalent for HEC-FDA) maps to: full SpecialFunctions (A2), SampleStatistics (A3), validation (A1), base/enum/factory (A4), 12 distributions (A4 exemplar + B1-B11), ConvergenceCriteria (C1), DynamicHistogram (C2), the converter + deterministic path (D1), closeout (D2). Every distribution class in `HEC.FDA.Statistics/Distributions/` is covered. Skipped-as-boilerplate (documented): `Serialization.cs`, `StoredAttribute.cs`, `StoredPropertyAttribute.cs`, `HistogramDebugVisualizer.cs`, `AssemblyInfo.cs`, and all XML `ToXML`/`ReadFromXML` methods.

**Placeholder scan:** distribution cards intentionally say "follow the Uniform exemplar" + give distribution-specific facts (source path, test path, ctor, factory, enum, oracle source) rather than re-transcribing ~1500 lines of C# — this is the mirror-port convention (the C# file is the literal spec), matching the Phase-0 approach the reviews accepted. The exemplar (Uniform) is fully worked. Fixtures with no dedicated test file (TruncatedNormal/TruncatedLogNormal/Gamma/TruncatedLogPearson3) are explicitly compute-then-pin-via-gate, the Phase-0 precedent.

**Type consistency:** `IDistributionFactory::create(type, params)`, `IDistribution::{pdf,cdf,inverse_cdf,fit,sample,type}`, the `Validation` base's `add_single_property_rule/validate/has_errors/error_level`, and the generic `dist_eval` glue are defined in A1/A3/A4 and consumed unchanged by every B/C/D task.

## Notes for the executor

- Task A4 is the linchpin: it builds the generic four-runner dispatch so B1-B11 are lightweight. Do A1→A2→A3→A4 in order; then B/C tasks are largely parallel-safe (each adds a disjoint header + fixture + one factory case), though they share `i_distribution_factory.hpp` and `test_fixtures.cpp` — dispatch them sequentially to avoid merge conflicts in those two shared files, or have each task append its factory case + dispatch entry carefully.
- The dotnet gate already subset-compiles `HEC.FDA.Statistics` (referenced as a clean project in Phase 0), so distributions validate against real C# for free once the emitter's `distribution` dispatch (A4) exists. The Truncated/Gamma oracles that lack a test file are still gate-verified — that is their oracle.
- Carry the Phase-0 `lower_bound` vs `Array.BinarySearch` duplicate-value risk forward; it is a Phase-2 (paired-data) concern, not Phase 1, but Empirical (B11) also interpolates over ordinate pairs — check its lookup against the C# for duplicate handling.
