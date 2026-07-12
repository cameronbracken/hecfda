# Plan: `hecfdar` (R) + `hecfdapy` (Python) from a shared C++ core, ported from HEC-FDA

> **Current status (kept in sync by hand):** Phase 0 through Phase 5 are **complete**. The full
> toolchain is proven end to end: seeded .NET `Random` -> `Normal` -> `PairedData` ->
> `UncertainPairedData` integrate-and-sample, identical in C++, R, and Python, and reproduced by
> the real HEC-FDA C#. Phase 1 added the validation subsystem, full `SpecialFunctions`,
> `SampleStatistics`, the distribution base/enum/factory with generic four-runner dispatch, all 13
> distributions, the `Gamma`/`ShiftedGamma`/`PearsonIII` helpers, `ConvergenceCriteria`,
> `DynamicHistogram`, and the `UncertainToDeterministicDistributionConverter`. Phase 2 added the
> full paired-data curve algebra, the faithful .NET `Array.BinarySearch`, `UncertainPairedData`
> generalized to `IDistribution`, and the graphical uncertainty path. Phase 3 added
> `hecfda::model::structures`: the three per-structure uncertainty samplers,
> `OccupancyType`/`DeterministicOccupancyType` + the move-only builder, `Structure`'s numeric
> `ComputeDamage`, and `Inventory`'s numeric subset, plus a project-wide by-value-capture fix for
> `Validation`-rule predicates on objects held inside relocating containers. Phase 4 added
> `hecfda::model::metrics` (`ConsequenceResult`/`AggregatedConsequencesBinned`/
> `StudyAreaConsequencesBinned`/`ConsequenceExtensions`), `Inventory::compute_damages`, and
> `hecfda::model::stage_damage` (`HydraulicProfiles`+`CorrectDryStructureWSEs`,
> `ImpactAreaStageDamage`'s geometry + `Compute()`, `ScenarioStageDamage`'s outer loop) --
> deterministic-only scope, cross-checked against the real `TractableStageDamageTests`. Phase 5
> added the rest of `hecfda::model::metrics` (`ThresholdEnum`, `AssuranceResultStorage`,
> `SystemPerformanceResults`, `Threshold`, `PerformanceByThresholds`,
> `CategoriedPairedData`/`CategoriedUncertainPairedData`, `ImpactAreaScenarioResults`), the
> analytical `bootstrap_to_paired_data` prerequisite, and the full seeded EAD Monte Carlo engine
> `hecfda::model::compute::ImpactAreaScenarioSimulation` + `SimulationBuilder` -- closing the
> end-to-end stage-frequency -> stage-damage -> EAD/AEP compute path, validated against both
> deterministic oracles (150000, 83333.33, 100150.179, 20.74, 0.026) and a bit-for-bit seeded
> benchmark (`121194.5159789352` at exactly 100 iterations). R and Python bind a representative
> subset per phase (structures: `value_uncertainty`, `structure`; stage-damage:
> `consequence_result`, `impact_area_stage_damage`; compute/metrics:
> `system_performance_results`'s `rng_conformance` case, `impact_area_scenario_simulation`'s
> `compute_ead` case); the rest are validated in C++ + the gate only, per the established
> coverage-scope convention. The oracle gate reproduces 695 fixture assertions, 0 failed. Phase 6
> (scenarios & alternatives: `Scenario`/`Alternative`/`AlternativeComparisonReport` + the
> `ScenarioResults`/`*ByQuantile` types) has not started and is next.
>
> Phase 0 delivered the canonical C++17 header core at `core/include/hecfda/`
> (`sampling::DotNetRandom`, `model::compute::RandomProvider`,
> `statistics::SpecialFunctions`/`Normal`/`Mathematics`,
> `model::paired_data::PairedData`/`UncertainPairedData`), doctest suites under `core/tests/`,
> language-neutral JSON fixtures under `fixtures/`, and three thin generic runners (C++
> `test_fixtures.cpp`, R `test-fixtures.R`, Python `test_fixtures.py`) that read the same
> fixtures. The R package `hecfdar` (cpp11) and Python package `hecfdapy` (pybind11 +
> scikit-build-core) each vendor `core/` + `fixtures/` via subtree symlinks dereferenced at
> build time. The pinned cross-language value is
> `UncertainPairedData::sample_and_integrate(seed=1234) = 24.425549382855987`, reproduced
> exactly in C++, R, Python, and the real C# `UncertainPairedData.SamplePairedDataRaw`; the RNG
> digest `sum(seed=12345, n=100000) = 50124.341288393982` matches the same way.
>
> Phase 1 delivered the rest of `HEC.FDA.Statistics`: the validation subsystem (`ErrorLevel` +
> rules reproducing C#'s intra-property bitwise-OR and cross-property overwrite semantics), the
> full `SpecialFunctions` gamma/beta closure, `SampleStatistics` (including the faithful
> population-moment getters and the upstream median-on-unsorted-array bug), the distribution
> base/enum/factory plus generic four-runner dispatch (adding a distribution is now header +
> factory case + name-mapping entry + fixture, no new glue), all 13 distributions (Normal,
> Uniform, Triangular, Deterministic, LogNormal, TruncatedNormal, TruncatedLogNormal, PearsonIII,
> LogPearson3, TruncatedLogPearson3, Gamma, ShiftedGamma, Empirical), `ConvergenceCriteria`,
> `DynamicHistogram` (the Monte Carlo accumulator), and the
> `UncertainToDeterministicDistributionConverter`. `Gamma`/`ShiftedGamma`/`PearsonIII` are
> internal helper classes (not `IDistribution`) with bespoke fixture targets rather than the
> generic dispatch. The dotnet oracle gate now reproduces 366 fixture assertions against the real
> upstream code, 0 failed (up from Phase 0's 18). The Makefile (`test-core`/`test-r`/`test-py`/
> `materialize`/`oracles`) and 3-platform CI (`.github/workflows/ci.yml`) are green.
>
> Phase 2 delivered the paired-data compute layer: the faithful .NET `Array.BinarySearch` (closing
> the top Phase-1 risk on duplicate x/y values, fixing `PairedData`/`Empirical` lookups),
> `CurveMetaData` and `PairedData` compose/multiply/`sum_ys_for_given_x`/monotonicity,
> `UncertainPairedData` generalized from `Normal` to `IDistribution` with every sample path
> including deterministic-via-converter, and `GraphicalUncertainPairedData` +
> `GraphicalDistribution` + the graphical uncertainty calculators + `InterpolateQuantiles`. Phase 2
> also found and fixed a cross-language FP-contraction (FMA) divergence: `-ffp-contract=off` is now
> set in `core/CMakeLists.txt`, `hecfdar/src/Makevars`/`Makevars.win`, and `hecfdapy/CMakeLists.txt`
> (all non-MSVC) -- see "FP-contraction (FMA) parity" below, a standing invariant alongside RNG
> parity. Exit gate: `test-core`/`test-r`/`test-py`/`oracles` all green, oracle gate at 492
> reproduced / 0 failed. See `CLAUDE.md` (same directory) for the working-context detail.

## Context

HEC-FDA (Hydrologic Engineering Center, Flood Damage Reduction Analysis) is a WPF/.NET desktop
application whose numerical heart is two C# libraries: `HEC.FDA.Statistics` (distributions,
histograms, convergence, sample statistics) and `HEC.FDA.Model` (the flood-damage compute engine:
paired-data curve algebra, structure inventory, stage-damage, the Monte Carlo EAD simulation,
metrics, scenarios, alternatives). The goal is to make that compute capability available as
idiomatic, CRAN- and PyPI-publishable R and Python packages that reproduce upstream functionality
and tests, built on a single shared C++17 core so both packages run the same compiled code and
return identical results with the same seed.

This is a mechanical mirror-port, following the design and process proven on the sibling project
`bestfit` (`../rmc/bestfit`, the RMC.BestFit / Numerics port). Write the math once in C++, bind it
twice (cpp11 for R, pybind11 for Python).

Full design rationale: `docs/superpowers/specs/2026-07-09-hecfda-port-design.md`.

## Architecture & repository layout

One canonical C++17 core, wrapped twice, validated by shared language-neutral JSON fixtures.

```
hecfda/
├── upstream/HEC-FDA/          # dev-only git submodule, pinned SHA (NOT vendored into packages)
├── core/                      # canonical C++17 core; all numerical dev here
│   ├── include/hecfda/{statistics/{distributions,histograms,convergence,special},
│   │                    model/{paired_data,structures,stage_damage,compute,metrics,
│   │                           scenarios,alternatives},sampling}/
│   ├── data/                  # embedded reference tables if needed
│   ├── tests/                 # doctest suites + test_fixtures.cpp runner
│   └── CMakeLists.txt
├── fixtures/                  # canonical oracle JSON (single source of truth)
├── hecfdar/                   # R package (cpp11), self-contained for CRAN
│   └── src/hecfda_core -> ../../core           # subtree symlink, dereferenced by R CMD build
├── hecfdapy/                  # Python package (scikit-build-core + pybind11)
│   └── src/hecfda_core -> ../../core           # symlink, materialized by tools/materialize_core.py
├── tools/{materialize_core.py, oracle_emitter/ (C#), verify_oracles.py}
├── Makefile, README.md, .github/workflows/ci.yml
└── .claude/{CLAUDE.md, PLAN.md}                # the only tracked .claude content
```

Core principles carried from bestfit:

- **C++17 only.** `CXX_STD = CXX17`, `SystemRequirements: C++17`. No C++20.
- **Self-contained.** No external C++ deps. Port HEC-FDA's own math; do NOT add Eigen/Boost. Keeps
  the CRAN dependency surface empty and preserves oracle fidelity.
- **Structural mirroring.** The C++ tree mirrors the C# file/class/method layout and order so an
  upstream diff maps almost line-for-line. Each ported file opens with
  `// ported from: <upstream-path> @ <sha>`.
- **Memory model.** Value types for distributions/small math objects; factory returns
  `std::unique_ptr`; polymorphic containers hold `std::vector<std::unique_ptr<...>>`. RTTI on for
  capability `dynamic_cast`. Enums to `enum class`; factory `if/else` to `switch`.
- **Mutation.** The global "never mutate" rule is relaxed for distribution/model objects that
  mirror the C# stateful API.

## Scope map: ported vs. adapter/severed

**Ported (numerical core):**

| Layer | Upstream | Contents |
|-------|----------|----------|
| Statistics foundation | `HEC.FDA.Statistics` | `SpecialFunctions`, `Mathematics`, `SampleStatistics`, the 13 distributions (Normal, LogNormal, Truncated{Normal,LogNormal,LogPearson3}, Triangular, Uniform, Gamma, ShiftedGamma, PearsonIII, LogPearson3, Empirical, Deterministic), `DynamicHistogram`, `ConvergeCriteria` |
| RNG | `Model/compute/RandomProvider` | Faithful port of .NET seeded `Random`, `NextRandom`/`NextRandomSequence`, `IProvideRandomNumbers` |
| Paired data | `Model/paireddata` | `PairedData`, `UncertainPairedData`, `GraphicalUncertainPairedData`, `InterpolateQuantiles`, integrate/sample/compose/multiply algebra + interfaces |
| Structures | `Model/structures` | `Structure`, `Inventory`, `OccupancyType`, `FirstFloorElevationUncertainty`, `ValueUncertainty`, `ValueRatioWithUncertainty`, depth-percent-damage sampling |
| Stage-damage | `Model/stageDamage` | `ImpactAreaStageDamage`, `ScenarioStageDamage` (hydraulic stage profiles passed in as arrays) |
| Compute + metrics | `Model/compute`, `Model/metrics` | `ImpactAreaScenarioSimulation` (EAD Monte Carlo) + the results/consequence/performance/threshold/assurance containers |
| Scenarios + alternatives | `Model/scenarios`, `alternatives`, `alternativeComparisonReport` | period-of-analysis EAD, EqAD, with/without comparison |

**Adapter / severed (NOT ported):** `HEC.RAS.Ras.Mapper` terrain/inundation reading, `Spatial/`
GIS, `hydraulics/` RAS-grid ingest, `SQLite/` + `utilities/dbfreader` + `Serialization`
persistence, `LifeLoss/` (RAS-dependent LifeSim), and all MVVM/messaging boilerplate. At the
binding edge these become thin adapters: users supply extracted inputs as data frames / dicts.
This scope holds for every phase below; nothing in this list is ever ported.

## RNG parity (the reproducibility payoff)

HEC-FDA seeds `System.Random` with an int (default 1234; tests pass explicit seeds). The seeded
constructor uses the legacy Knuth subtractive generator (`CompatPrng`), fully deterministic and
portable. `hecfda::sampling::DotNetRandom` ports it exactly, so `RandomProvider(seed).next_random()`
yields the identical `double` stream in C++, R, and Python -- verified bit-for-bit against a real
.NET capture in `core/tests/test_dotnet_random.cpp` and against the real C# by the dotnet oracle
gate. Every Monte Carlo fixture carries a seed; unseeded `new Random()` paths are never
fixture-tested because they are non-deterministic even in C#.

## FP-contraction (FMA) parity (standing invariant, found in Phase 2)

Clang and GCC default to fusing multiply-add expressions (`FP_CONTRACT` on, even at `-O0`), which
rounds differently than .NET's non-fused, strict left-to-right IEEE 754 arithmetic. This was found
in Task P2T4b: `GraphicalFrequencyUncertaintyCalculators::compute_slope`'s epsilon (1e-5)
finite-difference division amplified a single ULP difference by roughly 1e5x, breaking bit-for-bit
reproduction at scattered indices for some inputs. Fix: `-ffp-contract=off` is set project-wide,
non-MSVC only (MSVC already defaults to no contraction):

- `core/CMakeLists.txt` (per test target)
- `hecfdar/src/Makevars` and `Makevars.win` (`PKG_CXXFLAGS = -ffp-contract=off`)
- `hecfdapy/CMakeLists.txt` (`target_compile_options(_core PRIVATE
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>)`)

Any future core code using epsilon finite-differences or other FMA-sensitive arithmetic depends on
this flag to reproduce identically across C++/R/Python/C#. Treat it like RNG parity: never remove
or scope it down without re-verifying every fixture.

## Test-fixture strategy (validate identically, DRY)

Oracle values live ONLY in `fixtures/*.json`, never hardcoded in test files. Three thin generic
runners load the same JSON and apply every assertion: C++ `core/tests/test_fixtures.cpp`
(vendored nlohmann/json, test-only), R `hecfdar/tests/testthat/test-fixtures.R` (jsonlite), Python
`hecfdapy/tests/test_fixtures.py` (stdlib). Fixture schema per assertion carries a comparison
**mode** (`abs | rel | exact | bool | vector | matrix`) and **tolerance**. Each fixture:
`{target, kind, source_test, cases:[{name, construct, seed?, assertions:[{method, args, expected,
tol, mode}]}]}`. Monte Carlo fixtures add `seed` and an expected result-curve or digest to prove
identical streams across R and Python. Adding a distribution/model is a new fixture file plus a
couple of dispatch-table entries per runner, not new per-item glue. Schema detail:
`fixtures/README.md`.

### Oracle sourcing and the dotnet gate

HEC-FDA embeds expected values as C# literals in its unit tests (`UncertainPairedDataShould`,
`PairedDataShould`, `StructureShould`, `OccupancyTypeShould`, `ValueUncertaintyShould`,
`ThresholdShould`, `PerformanceTest`, `AlternativeTest`, `ScenarioShould`,
`ImpactAreaScenarioResultsShould`, `RandomTest`, `ContinuousDistributionExtensionsShould`, plus the
synthetic `TractableStageDamageTests` / `DefaultDataComputeOutcomes`). Two mechanisms:

1. A curation pass transcribes these literals into fixture JSON.
2. A dotnet oracle gate (`tools/oracle_emitter/` in C# + `tools/verify_oracles.py`)
   subset-compiles the real `HEC.FDA.Statistics` / `HEC.FDA.Model` numeric classes against the
   pinned submodule, replays every fixture, and fails on any value that does not reproduce to
   tolerance. Dev-only (needs `dotnet`); not wired into CI.

Integration tests requiring GDAL/RAS/terrain (Muncie resources) are excluded; their pure-numeric,
synthetic-input subset is harvested where it runs without spatial deps.

## Phasing (dependency-ordered, full parity)

**Phase 0 -- COMPLETE.** Proved the whole toolchain on one vertical slice: the .NET `Random` port,
`Normal` (with `inverse_cdf`/sampling), the `PairedData`/`UncertainPairedData`
integrate-and-sample path. Exit criterion met: the same fixture passes in C++/R/Python; the seeded
RNG stream is byte-identical across all three; symlink vendoring and the dotnet gate proven; CI
green.

**Phase 1 -- COMPLETE (Statistics foundation).** Ported the rest of `HEC.FDA.Statistics`:
validation, `SpecialFunctions`, `SampleStatistics`, the distribution base/enum/factory + generic
dispatch, all 13 distributions, the `Gamma`/`ShiftedGamma`/`PearsonIII` helpers,
`ConvergenceCriteria`, `DynamicHistogram`, and the `UncertainToDeterministicDistributionConverter`.
Exit criterion met: `test-core`/`test-r`/`test-py`/`oracles` all green, oracle gate at 366
reproduced / 0 failed. See `CLAUDE.md` for conventions established (factory keys, bespoke fixture
targets) and the faithful-bug list (deliberately reproduced upstream quirks, not to be "fixed"
later). Severed/deferred to Phase 2: `Empirical` stacking/weighting; `DynamicHistogram`
XML/plotting/`ConvertToEmpiricalDistribution`; the converter's `IHistogram` case; the
`UncertainPairedData` deterministic-sample-path wiring (needs `UncertainPairedData` generalized
from `Normal` to `IDistribution`, which is Phase 2 work); `TruncatedNormal`/`TruncatedLogNormal`/
`Gamma`/`TruncatedLogPearson3` currently have gate-only oracle coverage (no upstream unit-test
literals to transcribe beyond what the oracle gate already checks).

**Top risk carried into Phase 2 -- CLOSED.** C++ `std::lower_bound` vs. C#'s `Array.BinarySearch`
diverged on duplicate x/y values. Real FDA damage/frequency curves have flat segments, and both
`PairedData::f_inverse` and `Empirical`'s inverse-CDF lookup binary-search into paired value
arrays. Task P2T1 replaced the lookup with a faithful port of `Array.BinarySearch` before any other
Phase 2 work landed, so no later fixture could mask the divergence.

**Phase 2 -- COMPLETE (paired-data compute).** Delivered `CurveMetaData` + `PairedData`
compose/multiply/`sum_ys_for_given_x`/monotonicity, `UncertainPairedData` generalized to
`IDistribution` with all sample paths (including deterministic-via-converter), and
`GraphicalUncertainPairedData` + `GraphicalDistribution` + `GraphicalFrequencyUncertaintyCalculators`
+ `InterpolateQuantiles`. Also found and closed a cross-language FMA divergence (see
"FP-contraction (FMA) parity" above). Exit criterion met: `test-core`/`test-r`/`test-py`/`oracles`
all green, oracle gate at 492 reproduced / 0 failed. See `CLAUDE.md` for the faithful-bug list and
severances carried forward.

**Phase 3 -- COMPLETE (structures & inventory).** Ported `Structure`, `Inventory`, `OccupancyType`,
the value / first-floor / depth-damage uncertainty sampling, building structure depth-damage
sampling on the paired-data + distribution layer Phase 2 delivered. Also fixed a C++-port-only
ASan-confirmed use-after-scope defect: `Validation`-rule predicates on objects held by value inside
relocating containers now capture by value instead of `[this]` (see CLAUDE.md's "By-value capture
in Validation-rule predicates").

**Phase 4 -- COMPLETE (stage-damage).** Ported `ImpactAreaStageDamage`, `ScenarioStageDamage` over
paired-data + structures (hydraulic profiles as input arrays), plus `hecfda::model::metrics`'
consequence-binning substrate (`ConsequenceResult`, `AggregatedConsequencesBinned`,
`StudyAreaConsequencesBinned`, `ConsequenceExtensions`). Deterministic-only scope
(`compute_is_deterministic=true`), cross-checked against `TractableStageDamageTests`.

**Phase 5 -- COMPLETE (compute + metrics).** Ported `ImpactAreaScenarioSimulation` (the seeded EAD
Monte Carlo engine + `SimulationBuilder`) and the rest of the results / consequence / performance /
threshold / assurance surface (`ThresholdEnum`, `AssuranceResultStorage`,
`SystemPerformanceResults`, `Threshold`, `PerformanceByThresholds`,
`CategoriedPairedData`/`CategoriedUncertainPairedData`, `ImpactAreaScenarioResults`) plus the
analytical `bootstrap_to_paired_data` prerequisite. The convergence-driven MC loop with seeded
reproducibility -- the parity centerpiece -- is validated bit-for-bit against the real C# at a
fixed 100-iteration benchmark (`121194.5159789352`), on top of five deterministic oracles. Per-curve
RNG seeding uses seven fixed constants (1234/2345/3456/4567/5678/6789/7891); `Parallel.For` is
severed to a serial loop (safe: index-addressed sampling has no ordering dependency).

**Phase 6 -- NEXT (scenarios & alternatives).** `Scenario`, `Alternative` (period-of-analysis EAD,
EqAD), `AlternativeComparisonReport` (with/without comparison), `ScenarioResults`, and the two
`*ByQuantile` types, built on the EAD compute layer Phase 5 delivered.

Each phase merges only when its fixtures pass in all three harnesses and CI is green on all three
platforms. The user-facing R/Python API grows with each phase, reaching the full compute surface
at Phase 6.

## Development process (superpowers SDD)

Work proceeds as task cards under `.superpowers/sdd/`. Each porting task gets a brief (upstream C#
source path = the literal spec, C++ target path, constructor signature, dependencies, fixture
cases + oracle source, standard-vs-bespoke classification, exact verification commands), executed
by a subagent, followed by a code-review diff and a report.

**Exemplar recipe** for later phases: Phase 0's Task 2 (`core/tests/test_dotnet_random.cpp` +
`core/include/hecfda/sampling/dotnet_random.hpp`) is the pattern for a foundation module with a
verified reference stream; Task 4 (`core/include/hecfda/statistics/distributions/normal.hpp`) is
the pattern for a distribution using the transcribe-verbatim convention (copy the C# method body
line-for-line rather than reimplement). Reference both when writing a new task brief instead of
re-deriving the recipe.

**Definition of done per task:** `cmake --build core/build && ctest` green; R fixtures pass;
Python fixtures pass; `verify_oracles.py` green (when `dotnet` is available); then commit.

## Build systems, CI, and upstream sync

- **R (cpp11):** `src/Makevars`/`Makevars.win` with `CXX_STD = CXX17`, no `-O3`/`-march`/LTO/
  `-Werror`. After editing any `hecfdar/src/*.cpp`, re-run `cpp11::cpp_register`. After a core
  class-layout change, rebuild R with `R CMD INSTALL --preclean`.
- **Python (scikit-build-core + pybind11):** `pyproject.toml`, `CMakeLists.txt` with
  `CMAKE_CXX_STANDARD 17`, `pybind11_add_module`; wheels via cibuildwheel (future work; Phase 0
  ships sdist-style source installs only).
- **Makefile entry points:** `test-core`, `test-r`, `test-py`, `materialize`, `oracles`.
- **CI (GitHub Actions):** `core` (3 OS) + `r-cmd-check` (3 OS) + `python` (3 OS x {3.10, 3.12}).
  `actions/checkout` uses `submodules: false` so the upstream submodule and dotnet gate never
  touch CI.
- **Upstream sync:** pinned `upstream/HEC-FDA` submodule (`f63682a86a30dc306a105689714a92bfd95956c5`)
  is the diff baseline for provenance headers and the oracle gate. A `PORTING_MANIFEST.toml` +
  `tools/upstream_diff.py` worklist generator (bestfit precedent) is deferred until the bulk port
  needs it -- not required through the end of Phase 1.
- **Portability rules (bestfit-learned):** never `M_PI` (use `hecfda::kPi`); do not name a
  namespace alias `gamma` (clashes with glibc `gamma()`) or `stat`; pass `-Wall/-Wextra` only to
  non-MSVC compilers.

## Verification

- **C++:** `ctest` doctest suite green incl. the .NET `Random` reference-stream test.
- **R:** `testthat` reads `inst/fixtures` (symlinked to `fixtures/`); `R CMD check --as-cran`
  targeted for CRAN readiness once the API surface stabilizes past Phase 0.
- **Python:** `pytest` inside the installed package; cibuildwheel wiring targeted alongside CRAN
  readiness.
- **Cross-language identity:** a seeded computation is byte-identical between C++, R, and Python;
  every fixture passes identically to its stated tolerances.
- **dotnet oracle gate:** dev-only, reproduces every fixture against the real upstream C#.

## Open items carried into Phase 6

- **Next phase target:** Phase 6 -- scenarios & alternatives (`Scenario`, `Alternative`
  (period-of-analysis EAD, EqAD), `AlternativeComparisonReport` (with/without),
  `ScenarioResults`, and the two `*ByQuantile` types), which builds on the EAD compute layer Phase
  5 delivered.
- `CurveMetaData`/`GraphicalDistribution`/`GraphicalUncertainPairedData` XML +
  `ValidationErrorLogger`/GUI wiring -- severed (adapter/GUI layer, not the numeric core).
- `UncertainPairedData.CombineWithWeights` -- depends on severed `Empirical` stacking, currently
  untested; `Equals` and `ConvertDamagedElementCountToText` also deferred.
- The graphical path (`GraphicalUncertainPairedData`/`GraphicalDistribution`) is validated in C++
  and the oracle gate but not yet bound in R/Python. Documented follow-up; the
  `-ffp-contract=off` flag is already in place in both language builds for when it lands.
- `Empirical` stacking/weighting and `DynamicHistogram` XML/plotting/`ConvertToEmpiricalDistribution`
  -- severed from Phase 1, still pending. (The converter's `IHistogram` case was closed in Phase 4
  Task 4 -- no longer deferred.)
- `Inventory::get_inventory_and_water_trimmed_to_damage_category` -- ported but has no fixture
  coverage (not exercised by any Phase 3 Task 6 case); revisit when a future phase needs it.
- `OccupancyType::error_messages_for()` formats `ErrorLevel` as a raw int rather than the C#
  `[Flags]` enum name -- a pre-existing gap in `hecfda::statistics::Validation` (no enum-to-name
  map), not exercised by any fixture; carried forward from Phase 3.
- `ConvergenceCriteria` (Phase 1) still captures `[this]` in its `Validation`-rule predicates, the
  same UB shape fixed in the three Phase 3 structures samplers + `Structure`. **Still open as of
  Phase 5:** `Threshold` holds `SystemPerformanceResults` (itself holding `ConvergenceCriteria`) by
  value inside `PerformanceByThresholds`' `std::vector<Threshold>` -- another relocating container
  -- but no code path in Phase 5's fixtures calls `validate()` on a `ConvergenceCriteria` reached
  this way, so the ASan hazard remains latent rather than triggered. Fix in Phase 6 or a cleanup
  pass if a new call path reaches it (see CLAUDE.md's "By-value capture in Validation-rule
  predicates" for the full invariant).
- Phase 4's `ImpactAreaStageDamage::identify_central_stage_frequency_at_index_location` analytical-
  flow-frequency-with-discharge-stage branch throws (needs unported
  `ContinuousDistribution::to_coordinates`); no fixture reaches it. `produce_zero_damage_functions`
  (empty inventory) and the length/empty-input guards on `HydraulicProfiles`/
  `set_coordinate_quantity` are implemented but unexercised by any fixture -- add guard-clause
  fixtures if a future phase (or a cleanup pass) needs that coverage.
- Phase 5's `IProgressReport`/`ReportMessage`/`CancellationToken`/`[StoredProperty]` severances on
  `ImpactAreaScenarioSimulation` (repo-wide MVVM/messaging/reflection convention; see
  `impact_area_scenario_simulation.hpp`'s own SEVERANCES note) and the per-property
  validation-rule registration dropped from the `With*(UncertainPairedData)`/
  `With*(GraphicalUncertainPairedData)` builder overloads (no `validate()`/`has_errors()` surface
  on those two paired-data types in this port). `PerformanceByThresholds`/
  `SystemPerformanceResults`/`Threshold` throw on a lookup miss instead of C#'s
  log-and-return-a-dummy-fallback, matching the established severed-`ReportMessage`-miss
  convention; unreachable in practice since every miss this phase's compute path can reach is
  pre-registered.
- `PORTING_MANIFEST.toml` + `tools/upstream_diff.py` (deferred per bestfit precedent; needed once
  upstream churn must be tracked across many ported files).
- cibuildwheel / `R CMD check --as-cran` wiring (deferred until the package surface is broad
  enough to be worth packaging for real).
- Confirm 0BSD license compatibility holds as more of HEC-FDA (MIT-licensed) is ported; no
  incompatibility found so far.
