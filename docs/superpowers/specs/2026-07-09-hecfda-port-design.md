# Design: `hecfdar` (R) + `hecfdapy` (Python) from a shared C++ core, ported from HEC-FDA

Status: approved (brainstorming), 2026-07-09. Sibling project and process template:
`../../../rmc/bestfit` (the RMC.BestFit / Numerics port).

## Context

HEC-FDA (Hydrologic Engineering Center, Flood Damage Reduction Analysis) is a WPF/.NET 9 desktop
application whose numerical heart is two C# libraries: `HEC.FDA.Statistics` (distributions,
histograms, convergence, sample statistics) and `HEC.FDA.Model` (the flood-damage compute engine:
paired-data curve algebra, structure inventory, stage-damage, the Monte Carlo EAD simulation,
metrics, scenarios, alternatives). The goal is to make that compute capability available as
idiomatic, CRAN- and PyPI-publishable R and Python packages that reproduce all functionality and
tests, built on a single shared C++17 core so both packages run the same compiled code and return
identical results with the same seed.

This is a mechanical mirror-port, following the design and superpowers-driven development process
already proven on `bestfit`. Write the math once in C++, bind it twice (cpp11 for R, pybind11 for
Python).

### Settled decisions

- **Package names:** `hecfdar` (R), `hecfdapy` (Python). C++ namespace `hecfda::`. Chosen over
  `fdar`/`fdapy` to avoid collision with the Food & Drug Administration meaning and the existing
  `fda` functional-data-analysis package on CRAN.
- **Hosting:** GitHub public, `github.com/cameronbracken/hecfda`. Git identity personal
  (`Cam Bracken <cameron.bracken@pm.me>`), signed commits, push only when asked.
- **Scope: numerical core only.** Port `HEC.FDA.Statistics` and the pure-numeric `HEC.FDA.Model`
  compute engine. Treat RAS-Mapper terrain/inundation reading, GIS/`Spatial`, `hydraulics` RAS-grid
  ingest, `SQLite`/DBF persistence, `Serialization`, `LifeLoss` (LifeSim), and all MVVM/messaging
  boilerplate as thin adapters at the binding edge. Users supply already-extracted inputs
  (stage-frequency curves, per-frequency hydraulic stage profiles, structure inventory tables).
- **License:** 0BSD (confirm HEC-FDA's actual license when scaffolding; adjust if incompatible).
- **Approach: vertical slice first, then layered bulk port** (bestfit's Phase-0-then-phases model).

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
├── tools/{materialize_core.py, oracle_emitter/ (C#), verify_oracles.py, upstream_diff.py}
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
- **Mutation.** The global "never mutate" rule is relaxed for distribution/model objects that mirror
  the C# stateful API.

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
persistence, `LifeLoss/` (RAS-dependent LifeSim), and all MVVM/messaging boilerplate. At the binding
edge these become thin adapters: users supply extracted inputs as data frames / dicts.

## RNG parity (the reproducibility payoff)

HEC-FDA seeds `System.Random` with an int (default 1234; tests pass explicit seeds). In .NET 6+ the
**seeded** constructor uses the legacy Knuth subtractive generator (`CompatPrng`), which is fully
deterministic and portable. Port it exactly so `RandomProvider(seed).NextRandom()` yields the
identical `double` stream in C++, R, and Python. A C++ reference-stream test pins the port against
known .NET `Random(seed)` outputs. Every Monte Carlo fixture carries a seed; unseeded
`new Random()` paths are never fixture-tested because they are non-deterministic even in C#.

## Test-fixture strategy (validate identically, DRY)

Oracle values live ONLY in `fixtures/*.json`, never hardcoded in test files. Three thin generic
runners load the same JSON and apply every assertion:

- C++ `core/tests/test_fixtures.cpp` (vendored nlohmann/json, test-only)
- R `hecfdar/tests/testthat/test-fixtures.R` (jsonlite)
- Python `hecfdapy/tests/test_fixtures.py` (stdlib)

Fixture schema per assertion carries a comparison **mode** (`abs | rel | exact(NaN/Inf) | bool |
vector | matrix`) and **tolerance**, matching HEC-FDA's mix of exact and tolerance-based `Assert`.
Each fixture: `{target, kind, source_test, cases:[{name, construct|input, seed?,
assertions:[{method,args,expected,tol,mode}]}]}`. Monte Carlo fixtures add `seed` + an expected
result-curve or digest to prove identical streams across R and Python. Adding a distribution/model
is a new fixture file plus a couple of dispatch-table entries per runner, not new per-item glue.

### Oracle sourcing and the dotnet gate

HEC-FDA embeds expected values as C# literals in its unit tests (`UncertainPairedDataShould`,
`PairedDataShould`, `StructureShould`, `OccupancyTypeShould`, `ValueUncertaintyShould`,
`ThresholdShould`, `PerformanceTest`, `AlternativeTest`, `ScenarioShould`,
`ImpactAreaScenarioResultsShould`, `RandomTest`, `ContinuousDistributionExtensionsShould`, plus the
synthetic `TractableStageDamageTests` / `DefaultDataComputeOutcomes`). Two mechanisms:

1. A curation pass transcribes these literals into fixture JSON.
2. A **dotnet oracle gate** (`tools/oracle_emitter/` in C# + `tools/verify_oracles.py`)
   subset-compiles the real `HEC.FDA.Statistics` / `HEC.FDA.Model` numeric classes against the
   pinned submodule, replays every fixture, and fails on any value that does not reproduce to
   tolerance. Dev-only (needs `dotnet`, which is installed); not wired into CI.

Integration tests requiring GDAL/RAS/terrain (Muncie resources) are excluded; their pure-numeric,
synthetic-input subset is harvested where it runs without spatial deps.

## Phasing (dependency-ordered, full parity)

**Phase 0 — prove the whole toolchain on one vertical slice.** Port the minimum for a single seeded
computation exercising RNG + a distribution + paired-data integration end to end: the .NET `Random`
port, `Normal` (with `InverseCDF`/sampling), the `PairedData`/`UncertainPairedData`
integrate-and-sample path, and one small EAD-style integration fixture. **Exit criterion:** the same
fixture passes in C++/R/Python; the seeded RNG stream is byte-identical across R and Python; all CI
jobs green; symlink vendoring and the dotnet gate proven. No bulk porting begins until this is green.

**Phases 1-6 — bulk port up the dependency chain** (tests ported alongside each chunk):

1. **Statistics foundation** — `SpecialFunctions`, `Mathematics`, `SampleStatistics`, all 13
   distributions, `DynamicHistogram`, `ConvergeCriteria`. Parallelizable once the base exists.
2. **Paired-data library** — full `PairedData` / `UncertainPairedData` /
   `GraphicalUncertainPairedData` algebra (integrate, sample, compose, multiply, interpolate
   quantiles).
3. **Structures & inventory** — `Structure`, `Inventory`, `OccupancyType`, the value / first-floor /
   depth-damage uncertainty sampling.
4. **Stage-damage** — `ImpactAreaStageDamage`, `ScenarioStageDamage` over paired-data + structures
   (hydraulic profiles as input arrays).
5. **Compute + metrics** — `ImpactAreaScenarioSimulation` (EAD Monte Carlo) and the full
   results / consequence / performance / threshold / assurance surface. The convergence-driven MC
   loop with seeded reproducibility is the parity centerpiece.
6. **Scenarios & alternatives** — `Scenario`, `Alternative` (period-of-analysis EAD, EqAD),
   `AlternativeComparisonReport` (with/without).

Each phase merges only when its fixtures pass in all three harnesses and CI is green on all three
platforms. The user-facing R/Python API grows with each phase, reaching the full compute surface at
Phase 6.

## Development process (superpowers SDD)

Work proceeds as task cards under `.superpowers/sdd/`. Each porting task gets a brief (upstream C#
source path = the literal spec, C++ target path, constructor signature, dependencies, fixture cases +
oracle source, standard-vs-bespoke classification, exact verification commands), executed by a
subagent, followed by a code-review diff and a report. Two fully-worked exemplars (one distribution,
one foundation module) anchor the recipe; other cards reference them plus their specifics.

**Definition of done per task:** `cmake --build core/build && ctest` green; R fixtures pass; Python
fixtures pass; `verify_oracles.py` green; then commit.

## Build systems, CI, and upstream sync

- **R (cpp11):** `src/Makevars`/`Makevars.win` with `CXX_STD = CXX17`, no `-O3`/`-march`/LTO/`-Werror`.
  After editing any `hecfdar/src/*.cpp`, re-run `cpp11::cpp_register`. After a core class-layout
  change, rebuild R with `R CMD INSTALL --preclean`.
- **Python (scikit-build-core + pybind11):** `pyproject.toml`, `CMakeLists.txt` with
  `CMAKE_CXX_STANDARD 17`, `pybind11_add_module`; wheels via cibuildwheel.
- **Makefile entry points:** `test-core`, `test-r`, `test-py`, `materialize`, `oracles`.
- **CI (GitHub Actions)** after a fast `sync-check`: `core` (3 OS) + `r-cmd-check` (3 OS ×
  release/oldrel, `R CMD check --as-cran` clean) + `python` (cibuildwheel + pytest, 3 OS).
  `actions/checkout` uses `submodules: false` so the upstream submodule and dotnet gate never touch
  CI.
- **Upstream sync:** pinned `upstream/HEC-FDA` submodule is the diff baseline; `PORTING_MANIFEST.toml`
  maps each `.cs` to its C++ counterpart(s) + status (`ported | adapter | skipped:boilerplate |
  skipped:test`) + last-ported SHA/hash, so WPF/GIS/persistence churn generates zero porting noise;
  `tools/upstream_diff.py` emits a worklist on hash change. This loop and the manifest may be
  deferred until after Phase 0 (bestfit precedent).
- **Portability rules (bestfit-learned):** never `M_PI` (use a `hecfda::kPi`); do not name a
  namespace alias `gamma` (clashes with glibc `gamma()`) or `stat`; pass `-Wall/-Wextra` only to
  non-MSVC compilers.

## Verification

- **C++:** `ctest` doctest suite green incl. the .NET `Random` reference-stream test.
- **R:** `R CMD check --as-cran hecfdar` clean on 3 platforms; `testthat` reads `inst/fixtures`.
- **Python:** cibuildwheel builds wheels on 3 platforms; `pytest` passes inside each wheel; sdist
  installs.
- **Cross-language identity:** a seeded Monte Carlo EAD compute is byte-identical between R and
  Python; every fixture passes identically in C++/R/Python to its stated tolerances.
- **Phase-0 gate** is the first real proof; no bulk porting starts until it is green.

## Open items to resolve during scaffolding

- Confirm HEC-FDA's actual license and reconcile with the intended 0BSD.
- Confirm the exact .NET version's `Random` seeded algorithm and lock the reference stream.
- Identify the minimal Phase-0 EAD-style fixture from the synthetic-input tests
  (`DefaultDataComputeOutcomes` / `TractableStageDamageTests`) that runs without spatial deps.
