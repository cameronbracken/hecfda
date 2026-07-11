# HEC-FDA Port — Phase 3: Structures & Inventory Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Port the numeric core of `HEC.FDA.Model/structures` onto the Phase-2 paired-data + Phase-1 distribution layers — the three per-structure uncertainty samplers (`ValueUncertainty`, `ValueRatioWithUncertainty`, `FirstFloorElevationUncertainty`), `OccupancyType` + `DeterministicOccupancyType` (depth-percent-damage / first-floor / value sampling), the `Structure` per-structure damage compute, and the numeric subset of `Inventory` — each fixture-validated in C++ and reproduced against real HEC-FDA C# by the dotnet oracle gate, with a representative subset wired into R/Python.

**Architecture:** New headers under `core/include/hecfda/model/structures/`, each mirroring its C# file and carrying a `// ported from: <path> @ <sha>` header. The samplers reuse the Phase-1 distribution factory + the D1 `UncertainToDeterministicDistributionConverter`; `OccupancyType` reuses the Phase-2 `UncertainPairedData` (its `generate_random_numbers` + both `sample_paired_data` overloads already exist); `Structure` reuses `PairedData::f(x, int& index)`. Oracle values live only in `fixtures/structures/*.json`; the four runners (C++ doctest, R testthat, Python pytest, dotnet emitter) validate them. Spatial/metrics/persistence surfaces are severed at the port boundary (documented per task).

**Tech Stack:** C++17 (doctest), cpp11 (R), pybind11 (Python), dotnet gate (net9 build on net10 runtime via `DOTNET_ROLL_FORWARD=Major`).

**Branch:** `phase3` (off `main` @ the Phase-2 merge, commit `2a04a15`). Upstream pinned `upstream/HEC-FDA` @ `f63682a86a30dc306a105689714a92bfd95956c5`.

## Global Constraints

- **C++17 only.** No C++20. Self-contained core; no external C++ deps (no Eigen/Boost).
- **Structural mirroring:** each ported file opens with `// ported from: <upstream-path> @ f63682a86a30dc306a105689714a92bfd95956c5`; mirror the C# method layout/order. Transcribe verbatim; reproduce upstream bugs (do not "fix"), documenting each in a header comment.
- **Portability:** never `M_PI` (use `hecfda::kPi`); never a namespace alias `gamma`/`stat`; pass `-Wall/-Wextra` non-MSVC only (already handled in `core/CMakeLists.txt`).
- **FP-contraction parity (standing invariant):** `-ffp-contract=off` is already set project-wide (core CMake, R Makevars/Makevars.win, Python CMake). Do NOT remove or scope it down. Any new epsilon finite-difference / FMA-sensitive arithmetic depends on it.
- **No hardcoded oracle values in test code.** Oracles live in `fixtures/structures/*.json`, sourced from the upstream `HEC.FDA.ModelTest/unittests/structures` tests, and **captured from real C# via the dotnet gate — never hand-derived** (RNG/parity invariant). Where a fixture value is captured, write `"expected": "PIN"` first, then replace it with the gate-emitted value in the pin step. Upstream test literals (e.g. `ComputeStructureDamage`'s `200/180`) may be used directly as cross-checks.
- **Namespaces:** `hecfda::model::structures`.
- **RNG parity:** the samplers' `GenerateRandomNumbers(int seed, long size)` uses the Phase-0 ported .NET `Random` (`hecfda::sampling::DotNetRandom`), exactly as `UncertainPairedData::generate_random_numbers` already does. Seeded iteration-path fixtures (if any) carry a case-level `seed`.
- **Mutation:** the global "never mutate" rule stays relaxed for these stateful mirror objects (samplers hold `_RandomNumbers`; `Structure` holds `LastWSP…` segment indices), matching upstream design.
- **Reuse, don't duplicate:** distributions via `IDistributionFactory` / the concrete Phase-1 distribution headers; `UncertainToDeterministicDistributionConverter` for deterministic ratio sampling; `UncertainPairedData` and `PairedData` from Phase 2.
- **Commits:** SSH-signed; identity `Cam Bracken <cameron.bracken@pm.me>`; conventional-commit; no `Co-Authored-By`; push only when asked.
- **ABI (R):** after a core class-layout change, `R CMD INSTALL --preclean hecfdar`; after editing `hecfdar/src/*.cpp`, re-run `cpp11::cpp_register("hecfdar")`. **Python dev venv:** `~/venv/hecfdapy`.
- **Definition of done per task:** `ctest` green; `python3 tools/verify_oracles.py` green (gate count strictly increases); C++ fixture runner exercises the new fixture; commit. (R/Python runner wiring for the representative subset is Task 7.)

## What earlier phases already ported (extend, don't duplicate)

- **Phase 1** `statistics::`: `Validation` base (`add_single_property_rule`, `validate`, `has_errors`, `error_level`, `ErrorLevel` enum with faithful `[Flags]` bitwise aggregation) in `statistics/validation.hpp`; all 13 distributions (`Normal` with static `standard_normal_inverse_cdf`, `LogNormal`, `Triangular`, `Uniform`, `Deterministic`, …) under `statistics/distributions/`; the `IDistributionEnum`; `UncertainToDeterministicDistributionConverter` (`convert_distribution_to_deterministic`); `ConvergenceCriteria` (`convergence/`).
- **Phase 2** `model::paired_data::`: `PairedData` with `f(double x)`, `f(double x, int& index_of_previous_top_of_segment)`, `f_inverse(double y)`, `integrate`, compose/multiply/sum, `CurveMetaData`; `UncertainPairedData` (move-only, holds `unique_ptr<IDistribution>`) with `generate_random_numbers(int seed, long size)`, `sample_paired_data(double probability)`, `sample_paired_data(long iteration, bool retrieve_deterministic_representation)`.

## Scope: severed at the port boundary (numeric only)

Port these numeric members; DO NOT port the listed severed surfaces (document each severance in the file header):

| Class | Port (numeric) | Sever (adapter/later phase) |
|-------|----------------|-----------------------------|
| `ValueUncertainty` | ctors, `GenerateRandomNumbers`, `Sample(probability)`, `Sample(iteration,bool)`, `AddRules`, `DistributionType` | MVVM messaging base |
| `ValueRatioWithUncertainty` | 3 ctors, `GenerateRandomNumbers`, both `Sample` overloads (incl. deterministic-via-converter), `AddRules`/`MaxGreaterThanCentral`/`MinLessThanCentral` | — |
| `FirstFloorElevationUncertainty` | ctors, `GenerateRandomNumbers`, both `Sample` overloads, `AddRules`, `DistributionType` | — |
| `OccupancyType` + `OccupancyTypeBuilder` | fields, per-category seeds, `GenerateRandomNumbers(size)`, `Sample(probability)`, `Sample(iteration,bool)`, `Validate`/`GetErrorsFromProperties`, builder | `MessageHub`/`MessageReport` messaging |
| `DeterministicOccupancyType` | full (pure data holder, 24 fields) | — |
| `Structure` | `Fid`/values/`FirstFloorElevation`/`GroundElevation`/`YearInService`/`NumberOfStructures`/`BeginningDamageDepth`/occtype/damcat/impactAreaID fields, `AddRules`, the tuple `ComputeDamage(float, DeterministicOccupancyType, priceIndex, analysisYear)`, `FindOccType`/`FindOccTypeIndex`, `ResetIndexTracking` | `PointM`/`Geospatial.Vectors.Point`, the `ConsequenceResult`-returning `ComputeDamage` overload (Phase 5 metrics), `ProduceDetails*`/`CalculateDepthZeroDamage` (CSV/`f_inverse`), `Cbfips`/`Notes`/`Description` optional |
| `Inventory` | in-memory ctor `(occTypes, structures, priceIndex)`, `GetDamageCategories`, `GetInventoryTrimmedToImpactArea`, `GetInventoryAndWaterTrimmedToDamageCategory`, `GenerateRandomNumbers(ConvergenceCriteria)`, `SampleOccupancyTypes`, `Validate`, `GetGroundElevations` | shapefile/terrain ctors (`StructureFactory`/`RASHelper`/`Projection`), `ComputeDamages`→`ConsequenceResult` (Phase 5), `GetPointMs`, `StructureDetails`, messaging |

`Structure` stores occupancy-type identity by `OccTypeName` (string) as upstream does; no spatial point is stored. The two `Structure` C# ctors collapse to one numeric ctor (no `PointM`/`Point` parameter).

## File Structure

```
core/include/hecfda/model/structures/
  value_uncertainty.hpp                 # Task 1
  value_ratio_with_uncertainty.hpp      # Task 2
  first_floor_elevation_uncertainty.hpp # Task 3
  deterministic_occupancy_type.hpp      # Task 4
  occupancy_type.hpp                    # Task 4 (incl. OccupancyTypeBuilder)
  structure.hpp                         # Task 5
  inventory.hpp                         # Task 6
core/tests/test_fixtures.cpp            # each task: add loader + bespoke dispatch target
fixtures/structures/
  value_uncertainty.json                # Task 1
  value_ratio_with_uncertainty.json     # Task 2
  first_floor_elevation_uncertainty.json# Task 3
  occupancy_type.json                   # Task 4
  structure.json                        # Task 5
  inventory.json                        # Task 6
tools/oracle_emitter/HecFdaOracleEmitter.csproj  # each task: add <Compile Include> (or patched/)
tools/oracle_emitter/patched/                     # patched copies where MVVM/spatial deps intrude
tools/oracle_emitter/Program.cs                   # each task: Eval* method + target-switch case
hecfdar/src/*.cpp, hecfdapy/src/**                # Task 7: bind representative subset
hecfdar/tests/testthat/test-fixtures.R            # Task 7
hecfdapy/tests/test_fixtures.py                   # Task 7
```

**Emitter note (applies to every task):** the emitter subset-compiles the minimal source closure (it cannot reference `HEC.FDA.Model.csproj` — private RAS NuGet, 401). Add each new `structures/*.cs` via `<Compile Include>`. If a file drags in MVVM messaging (`MessageHub`, `MessageReportedEventHandler`), spatial types (`PointM`, `Geospatial`, `RasMapperLib`), or `metrics.ConsequenceResult`, add a **patched copy** under `tools/oracle_emitter/patched/` that stubs/strips exactly those (following the existing `CurveMetaData.cs`/`GraphicalDistribution.cs`/`GraphicalUncertainPairedData.cs` precedent) and `<Compile Include>` the patched copy instead of the upstream file. Keep every numeric line verbatim.

---

### Task 1: `ValueUncertainty`

**Files:** Create `core/include/hecfda/model/structures/value_uncertainty.hpp`; Create `fixtures/structures/value_uncertainty.json`; Modify `core/tests/test_fixtures.cpp` (loader + `value_uncertainty` target), `tools/oracle_emitter/HecFdaOracleEmitter.csproj` (+ `patched/` if needed), `tools/oracle_emitter/Program.cs` (`EvalValueUncertainty` + `case "value_uncertainty"`).

**Interfaces:**
- Consumes: `statistics::distributions::{Normal, Triangular, Uniform}` (their `InverseCDF`; `Normal::standard_normal_inverse_cdf`), `statistics::IDistributionEnum`, `statistics::Validation`, `sampling::DotNetRandom`.
- Produces: `hecfda::model::structures::ValueUncertainty` — ctors `ValueUncertainty()` and `ValueUncertainty(IDistributionEnum, double std_or_min, double max=100)`; `IDistributionEnum distribution_type() const`; `void generate_random_numbers(int seed, long size)`; `double sample(double probability) const`; `double sample(long iteration, bool compute_is_deterministic) const`. Extends `Validation` (the `AddRules` three rules).

- [ ] **Step 1: Read the source.** `upstream/HEC-FDA/HEC.FDA.Model/structures/ValueUncertainty.cs` (156 lines). Note the exact semantics to reproduce verbatim: center-of-distribution = `100/100 = 1`; `Normal(1, std/100)`; `LogNormal` branch = `exp(standard_normal_inverse_cdf(p) * std/100)` (NOT a `LogNormal` object); `Triangular(min/100, 1, max/100)`; `Uniform(min/100, max/100)`; default (Deterministic) = 1; negative result clamped to 0. `Sample(iteration, deterministic=true)` returns `1` unconditionally (no converter). The three `AddRules`: distribution ∈ {Normal,Uniform,Deterministic,Triangular} else Fatal; `std_or_min >= 0` else Fatal; `max >= 100` else Fatal.

- [ ] **Step 2: Write the failing fixture.** `fixtures/structures/value_uncertainty.json`, `"target": "value_uncertainty"`. One case per upstream `ValueUncertaintyShould` `[Theory]` row, each `construct` = `{dist, std_or_min, max}` and an assertion `{method:"sample", args:[probability], expected:"PIN", tol:1e-9, mode:"rel"}`. The C# test post-processes the offset (`inv*offset`, or `pow(offset, log(inv))*inv` for LogNormal); the **fixture asserts the raw `sample(probability)` offset** (the offset is the numeric unit under test), so the emitter returns `ValueUncertainty.Sample(p)` directly. Rows (dist, std_or_min, max, probability):
  1. `Normal, 10, 100, 0.2`   (upstream `max` omitted → default 100)
  2. `LogNormal, 20.3852, 100, 0.8`
  3. `Triangular, 90, 120, 0.95`
  4. `Uniform, 80, 130, 0.05`
  5. `Deterministic, 98.349, 100, 0.593497`  (expect `1.0`)
  6. `Normal, 150, 100, 0.01`  (expect `0.0` — negative clamp)

  Add one `mode:"exact"` deterministic-iteration case: `{method:"sample_iteration", args:[1, 1]}` (iteration=1, deterministic=true) on the `Normal,10` construct → expect `1.0`.

- [ ] **Step 3: Wire the emitter, run, confirm the fixture FAILS (target unknown / value unpinned).** In `Program.cs` add `case "value_uncertainty": val = EvalValueUncertainty(c, method, argsEl); break;` and `EvalValueUncertainty` building `new ValueUncertainty(ParseEnum(c.construct.dist), std_or_min, max)` and dispatching `sample`→`Sample(D(args[0]))`, `sample_iteration`→`Sample((long)D(args[0]), D(args[1])!=0)`. Add the source to the csproj closure (patched copy if MVVM messaging intrudes — `ValueUncertainty` extends `ValidationErrorLogger`; if that drags messaging, patch to extend the plain `Validation` base or stub the messaging). Run `DOTNET_ROLL_FORWARD=Major python3 tools/verify_oracles.py` — expect it to emit values for the new case (still unpinned → C++ mismatch).

- [ ] **Step 4: Implement `value_uncertainty.hpp`.** Transcribe verbatim into `hecfda::model::structures`. Add the loader in `test_fixtures.cpp` (`fixtures/structures/value_uncertainty.json`) and a bespoke `value_uncertainty` dispatch: construct from `{dist,std_or_min,max}`, call `sample(p)` or `sample(iteration,det)`. Reuse the enum-name→`IDistributionEnum` mapping already used by the distribution fixtures.

- [ ] **Step 5: Pin + verify.** Replace each `"PIN"` with the gate-emitted value (cross-check row 1 ≈ `0.9158379`, row 5 = `1`, row 6 = `0`). Run `cmake --build core/build && ctest --test-dir core/build --output-on-failure` and `python3 tools/verify_oracles.py`. Both green; gate count strictly up.

- [ ] **Step 6: Commit** `feat(structures): port ValueUncertainty sampler + fixtures`.

---

### Task 2: `ValueRatioWithUncertainty`

**Files:** Create `core/include/hecfda/model/structures/value_ratio_with_uncertainty.hpp`; Create `fixtures/structures/value_ratio_with_uncertainty.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Consumes: Phase-1 `Normal`/`LogNormal`/`Triangular`/`Uniform`, `UncertainToDeterministicDistributionConverter::convert_distribution_to_deterministic`, `Validation`, `DotNetRandom`.
- Produces: `ValueRatioWithUncertainty` — ctors `()`, `(IDistributionEnum, double std_or_min, double central, double max=double_max)`, `(double deterministic_ratio)`; `generate_random_numbers(int,long)`; `double sample(double probability) const`; `double sample(long iteration, bool compute_is_deterministic) const`. Extends `Validation`.

- [ ] **Step 1: Read the source.** `ValueRatioWithUncertainty.cs` (203 lines). Semantics: `Sample(p)` uses `Normal(central, std)`, `LogNormal(central, std)`, `Triangular(min, central, max)`, `Uniform(min, max)`, default = `central`; clamp negative → 0. `Sample(iteration, deterministic=true)`: LogNormal → `convert_distribution_to_deterministic(LogNormal(central,std)).InverseCDF(0.5)`; Uniform → `convert(...).InverseCDF(0.5)`; default → `central`. `Sample(iteration,false)` pulls `_RandomNumbers[iteration]` through the same `Sample(p)` switch. `AddRules`: params ≥ 0; `max ≥ std_or_min`; `MaxGreaterThanCentral` (deterministic bypasses → true); `MinLessThanCentral` (only Triangular checks `min ≤ central`, else true).

- [ ] **Step 2: Write the failing fixture** from `ValueRatioWithUncertaintyShould` `[Theory]`. `"target":"value_ratio_with_uncertainty"`, `construct` = `{dist, std_or_min, central, max}`, assertion `{method:"sample", args:[probability], expected:"PIN", tol:1e-9, mode:"rel"}`. Rows (dist, std_or_min, central, max, probability):
  1. `Normal, 0.1, 0.6, 0, 0.95`
  2. `LogNormal, 0.57, -0.85, 0, 0.05`
  3. `Triangular, 0.7, 0.85, 0.9, 0.88`
  4. `Uniform, 0.6, 0.8, 0.95, 0.40`
  5. `Deterministic, 394802, 0.8, 20442, 333`  (expect `0.8`)
  6. `Normal, 0.5, 0.1, 0, 0.01`  (expect `0.0` — negative clamp)

  Add two deterministic-iteration cases to cover the converter path: `{method:"sample_iteration", args:[1,1]}` on `LogNormal, 0.57, -0.85, 0` and on `Uniform, 0.6, 0.8, 0.95` — expected `"PIN"` (captured; these exercise `convert_distribution_to_deterministic(...).InverseCDF(0.5)`).

- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalValueRatioWithUncertainty` + `case "value_ratio_with_uncertainty"`; source (or patched) in csproj.

- [ ] **Step 4: Implement** `value_ratio_with_uncertainty.hpp` verbatim; loader + `value_ratio_with_uncertainty` dispatch in `test_fixtures.cpp`.

- [ ] **Step 5: Pin + verify** (cross-check row 1 ≈ `0.764485`, row 5 = `0.8`, row 6 = `0`). `ctest` + gate green.

- [ ] **Step 6: Commit** `feat(structures): port ValueRatioWithUncertainty sampler + fixtures`.

---

### Task 3: `FirstFloorElevationUncertainty`

**Files:** Create `core/include/hecfda/model/structures/first_floor_elevation_uncertainty.hpp`; Create `fixtures/structures/first_floor_elevation_uncertainty.json`; Modify `test_fixtures.cpp`, csproj (+`patched/`), `Program.cs`.

**Interfaces:**
- Produces: `FirstFloorElevationUncertainty` — ctors `()`, `(IDistributionEnum, double std_or_min, double max=double_max)`; `IDistributionEnum distribution_type() const`; `generate_random_numbers(int,long)`; `double sample(double probability) const`; `double sample(long iteration, bool compute_is_deterministic) const`. Extends `Validation`.

- [ ] **Step 1: Read the source.** `FirstFloorElevationUncertainty.cs` (158 lines). Semantics: center = `0`; `Normal(0, std)`; `LogNormal` = `exp(standard_normal_inverse_cdf(p) * std)` (NOT a `LogNormal` object); `Triangular(-std, 0, max)`; `Uniform(-std, max)`; default = 0. **No negative clamp** (offsets may be negative — differs from the value samplers). `Sample(iteration, deterministic=true)`: LogNormal → `1`, else `0`. Note the ctor's third arg is named `maximum`/`_FeetAboveInventoryValue` and `std_or_min` is `_StandardDeviationFromOrFeetBelowInventoryValue`. `AddRules`: distribution ∈ {Normal,Uniform,Deterministic,Triangular} else Fatal; `std ≥ 0 && max ≥ 0` else Fatal.

- [ ] **Step 2: Write the failing fixture** from `FirstFloorElevationUncertaintyShould` `[Theory]`. `construct` = `{dist, std_or_min, max}`, assertion on raw `sample(probability)` offset. Rows (dist, std_or_min, max, probability):
  1. `Normal, 0.5, 0, 0.4`
  2. `LogNormal, 0.050636, 0, 0.7`
  3. `Triangular, 0.5, 1, 0.75`
  4. `Uniform, 0.5, 1, 0.45`
  5. `Deterministic, 203958, 20935, 0.456`  (expect `0.0`)

  Add a deterministic-iteration case: `{method:"sample_iteration", args:[1,1]}` on `LogNormal, 0.050636, 0` → expect `1.0`, and on `Normal, 0.5, 0` → expect `0.0`.

- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalFirstFloorElevationUncertainty` + `case "first_floor_elevation_uncertainty"`.

- [ ] **Step 4: Implement** verbatim; loader + `first_floor_elevation_uncertainty` dispatch.

- [ ] **Step 5: Pin + verify** (cross-check: row 1 offset ≈ `4.873326 - 5 = -0.126674`; row 5 = `0`). `ctest` + gate green.

- [ ] **Step 6: Commit** `feat(structures): port FirstFloorElevationUncertainty sampler + fixtures`.

---

### Task 4: `OccupancyType` + `DeterministicOccupancyType` + `OccupancyTypeBuilder`

**Files:** Create `core/include/hecfda/model/structures/deterministic_occupancy_type.hpp` and `core/include/hecfda/model/structures/occupancy_type.hpp`; Create `fixtures/structures/occupancy_type.json`; Modify `test_fixtures.cpp`, csproj (+`patched/` — `OccupancyType` uses `MessageHub`, so a patched copy stubbing the messaging is expected), `Program.cs`.

**Interfaces:**
- Consumes: Task 1–3 samplers, Phase-2 `UncertainPairedData` (`generate_random_numbers`, both `sample_paired_data` overloads) + `PairedData` + `CurveMetaData`, Phase-1 `IDistributionEnum`.
- Produces: `DeterministicOccupancyType` (pure holder, all 24 fields + accessors, `is_null()`), and `OccupancyType` with `static OccupancyTypeBuilder builder()`; `void generate_random_numbers(long size)`; `DeterministicOccupancyType sample(double probability) const`; `DeterministicOccupancyType sample(long iteration, bool compute_is_deterministic=false) const`; `void validate()`, `std::string get_errors_from_properties()`, `bool compute_content_damage()/...`, name/damage-category accessors. `OccupancyTypeBuilder` with the `with_*` chain + `build()`.

- [ ] **Step 1: Read the source.** `OccupancyType.cs` (443), `DeterministicOccupancyType.cs` (67). Note: per-category seed constants `DEPTH_DAMAGE_SEED=1234`, `FIRST_FLOOR_SEED=2345`, `STRUCTURE_VALUE_SEED=3456`, `CONTENT_VALUE_SEED=4567`, `OTHER_VALUE_SEED=5678`, `VEHICLE_VALUE_SEED=6789`. `GenerateRandomNumbers(size)` calls each held sampler's `generate_random_numbers(seed, size)` conditioned on `ComputeContentDamage`/`UseContentToStructureValueRatio`/`ComputeOtherDamage`/`ComputeVehicleDamage`. `Sample(...)` builds a `DeterministicOccupancyType` from `_StructureDepthPercentDamageFunction.SamplePairedData(...)`, the sampled FFE/value offsets/ratios, and the `_…IsLogNormal` flags (set by the builder when a sampler's `DistributionType == LogNormal`). Non-computed content/vehicle/other paired-data default to `PairedData({0},{0})` (the "hack"). The builder sets `ComputeContentDamage=true` when `WithContentDepthPercentDamage` is called, etc. `Sample` throws when `ErrorLevel >= Major`. **Sever** `MessageHub`/`ReportMessage` (messaging) — keep `Validate`/`GetErrorsFromProperties` (the `PropertyValidationHelper` aggregation: `ValidateProperty` runs a child `validate()` and lifts its `ErrorLevel`).

- [ ] **Step 2: Write the failing fixture** from `OccupancyTypeShould`. `"target":"occupancy_type"`. Build the occ-type via the fixture `construct`: `depths=[0,1,2,3,4,5]`, `struct/content percentDamages` = `Normal(0,0),Normal(10,5),Normal(20,6),Normal(30,7),Normal(40,8),Normal(50,9)`, FFE `Normal,0.5`, structure value `Normal,0.1`, content-to-structure ratio `Normal,10,90`, name `"MyOccupancyType"`, damage-category `"DamageCategory"`. Assertions on `sample(iteration=1, deterministic=true)`:
  - `{method:"sample_iteration_struct_yvals", args:[1,1], expected:[0,10,20,30,40,50], mode:"vector"}`
  - `{method:"sample_iteration_content_yvals", args:[1,1], expected:[0,10,20,30,40,50], mode:"vector"}`
  - `{method:"sample_iteration_structure_value_offset", args:[1,1], expected:1, mode:"exact"}`
  - `{method:"sample_iteration_ffe_offset", args:[1,1], expected:0, mode:"exact"}`
  - `{method:"sample_iteration_csvr", args:[1,1], expected:90, mode:"exact"}`

  (These are the exact deterministic assertions from `OccupancyTypeShould`; all exact, no tolerance.) Add one seeded case that exercises the RNG path end to end: `{method:"generate_then_sample_iteration_struct_yvals", args:[1,0], size:100, expected:"PIN", mode:"vector"}` — calls `generate_random_numbers(100)` then `sample(1, false)` and returns the struct paired-data yvals (captured from the gate; proves the per-category-seed RNG wiring reproduces C# bit-for-bit).

- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalOccupancyType` building the occ-type via the C# `OccupancyType.Builder()` chain from the fixture `construct`, dispatching the `sample_iteration_*` accessors against `Sample(1,true)` / (for the seeded case) `GenerateRandomNumbers(size)` then `Sample(1,false)`. Add the source (patched to stub `MessageHub`/`MessageReport`) to the csproj. `DeterministicOccupancyType.cs` is clean — add as-is.

- [ ] **Step 4: Implement** `deterministic_occupancy_type.hpp` then `occupancy_type.hpp` (incl. the nested `OccupancyTypeBuilder`) verbatim. Because `UncertainPairedData` is move-only, `OccupancyType` holds its four depth-damage functions by value and is itself move-only (mirror this; document). Add loader + `occupancy_type` bespoke dispatch in `test_fixtures.cpp`. **After this core change rebuild is header-only**, but the R package is untouched this task.

- [ ] **Step 5: Pin + verify.** Pin the seeded case from the gate. `ctest` + gate green; the five deterministic assertions match exactly; the seeded yvals reproduce C#.

- [ ] **Step 6: Commit** `feat(structures): port OccupancyType + DeterministicOccupancyType + builder + fixtures`.

---

### Task 5: `Structure` (numeric depth-damage compute)

**Files:** Create `core/include/hecfda/model/structures/structure.hpp`; Create `fixtures/structures/structure.json`; Modify `test_fixtures.cpp`, csproj (+`patched/Structure.cs` — strips `PointM`/`Geospatial`/`ConsequenceResult` overload/`ProduceDetails`), `Program.cs`.

**Interfaces:**
- Consumes: Task 4 `DeterministicOccupancyType`, Phase-2 `PairedData::f(double x, int& index_of_previous_top_of_segment)`.
- Produces: `Structure` — numeric ctor `Structure(std::string fid, double first_floor_elevation, double val_struct, std::string st_damcat, std::string occtype, int impact_area_id, double val_cont=0, double val_vehic=0, double val_other=0, std::string cbfips="unassigned", double begin_damage=DEFAULT_MISSING_VALUE, double ground_elevation=DEFAULT_MISSING_VALUE, double foundation_height=DEFAULT_MISSING_VALUE, int year=DEFAULT_MISSING_VALUE, int num_structures=1, std::string notes="", std::string description="")`; `std::tuple<double,double,double,double> compute_damage(float wse, const DeterministicOccupancyType&, double price_index=1, int analysis_year=9999)`; `DeterministicOccupancyType find_occ_type(const std::vector<DeterministicOccupancyType>&) const` + `int find_occ_type_index(...) const`; `void add_rules()` (extends `Validation`); `void reset_index_tracking()`; accessors for `inventoried_structure_value()`, `damage_catagory()` (keep the upstream spelling), `occ_type_name()`, `impact_area_id()`, `ground_elevation()`. Define `DEFAULT_MISSING_VALUE` locally (port `utilities.IntegerGlobalConstants.DEFAULT_MISSING_VALUE`; confirm its value in `upstream/.../utilities/IntegerGlobalConstants.cs`).

- [ ] **Step 1: Read the source.** `Structure.cs` (371). Port ONLY the tuple `ComputeDamage(float, DeterministicOccupancyType, priceIndex, analysisYear)` (lines ~138–280) verbatim: the log-normal-vs-additive FFE branch, `depthabovefoundHeight = wse - sampledFFE`, the `YearInService <= analysisYear` and `BeginningDamageDepth <= depthabovefoundHeight` guards, the per-component `f(depth, ref index)` sequential-search lookups with the 0–100 percent clamps, the `IsStructureValueLogNormal` `pow(offset, log(inv))*inv` branch, CSVR/OSVR ratio branches (note OSVR divides ratio by 100; CSVR divides by 100 in the damage line). Reproduce the four `LastWSPStageDamageSegmentTopIndex*` mutable members (init `1`) and `ResetIndexTracking`. `AddRules`: seven Fatal rules (FFE > -300; four values ≥ 0; damcat/occtype non-empty). **Sever** both `PointM`/`Point` ctors → one numeric ctor; the `ConsequenceResult` overload; `ProduceDetails*`/`CalculateDepthZeroDamage`/`FindHighestDepthZeroPercentDamage` (CSV, Phase 5).

- [ ] **Step 2: Write the failing fixture** from `StructureShould`. `"target":"structure"`. Reproduce the shared `ComputeStructureDamage` occ-type (`depths=[0..5]`, struct+content `Normal` percentDamages as in Task 4, FFE `Normal,0.5`, struct value `Normal,0.1`, CSVR `Normal,10,90`; `firstFloorElevation=100`, `inventoriedStructureValue=1000`), sample the occ-type deterministically (`sample(1,true)`), and assert `compute_damage(wse, det, 1, 9999)` returns `(struct, content, …)`:
  - `{method:"compute_damage_struct", args:[102], expected:200, tol:0.5, mode:"abs"}` and `compute_damage_content` → `180`.
  - `{method:"compute_damage_struct", args:[104], expected:400, mode:"abs", tol:0.5}` and content → `360`.

  Add the `SELA_StructureDamage_Should` cases (triangular occ-type, structures `232549`/`233375`, wse `-1.27`/`-4.17`): assert `compute_damage_struct` ≈ `0.4213*74.946944` and `0.04476*88.204817` (`tol:0.02*value`, `mode:"abs"`). Capture full-precision expecteds via the gate (`"PIN"`), using the rounded literals as cross-check.

- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalStructure` building the occ-type + structure from the fixture `construct`, sampling `Sample(1,true)`, dispatching `compute_damage_struct`/`compute_damage_content`/`_vehicle`/`_other` = the tuple items of `ComputeDamage(wse, det, priceIndex, analysisYear)`. Add `patched/Structure.cs` (numeric-only) to the csproj.

- [ ] **Step 4: Implement** `structure.hpp` verbatim (numeric ctor + tuple compute + find-occ-type + validation). Add loader + `structure` bespoke dispatch in `test_fixtures.cpp`. **`compute_damage` is non-const** (mutates the segment indices) — mirror faithfully; the fixture calls it on a mutable `Structure`.

- [ ] **Step 5: Pin + verify.** Pin from the gate; the `200/180`, `400/360` literals reproduce; SELA cases within tolerance. `ctest` + gate green.

- [ ] **Step 6: Commit** `feat(structures): port Structure numeric depth-damage compute + fixtures`.

---

### Task 6: `Inventory` (numeric subset)

**Files:** Create `core/include/hecfda/model/structures/inventory.hpp`; Create `fixtures/structures/inventory.json`; Modify `test_fixtures.cpp`, csproj (+`patched/Inventory.cs` — in-memory only), `Program.cs`.

**Interfaces:**
- Consumes: Task 4 `OccupancyType`, Task 5 `Structure`, Phase-1 `ConvergenceCriteria`.
- Produces: `Inventory` — in-memory ctor `Inventory(std::map<std::string,OccupancyType> occ_types, std::vector<Structure> structures, double price_index=1)`; `std::vector<std::string> get_damage_categories() const`; `Inventory get_inventory_trimmed_to_impact_area(int impact_area_fid) const`; `std::pair<Inventory, std::vector<std::vector<float>>> get_inventory_and_water_trimmed_to_damage_category(std::string, const std::vector<std::vector<float>>&) const`; `void generate_random_numbers(const ConvergenceCriteria&)`; `std::vector<DeterministicOccupancyType> sample_occupancy_types(long iteration, bool compute_is_deterministic)`; `std::vector<float> get_ground_elevations() const`; `void validate()` + `has_errors()`/`error_level()` (via the `PropertyValidationHelper` aggregation). Because `OccupancyType`/`Structure` may be move-only, decide storage (see Step 4) and document.

- [ ] **Step 1: Read the source.** `Inventory.cs` (405). Port the in-memory ctor `Inventory(occTypes, structures, priceIndex)` and the numeric methods listed above verbatim. `GenerateRandomNumbers(ConvergenceCriteria cc)` → `quantity = Convert.ToInt32(cc.MaxIterations * 2)` then each occ-type's `GenerateRandomNumbers(quantity)`. `SampleOccupancyTypes` iterates `OccTypes.Values` (insertion/iteration order matters — use an ordered map to match C# `Dictionary` enumeration; **confirm** whether the fixture depends on order and document the choice). `Validate` aggregates occ-type + structure errors and sets `Minor` when `Structures.Count == 0`. **Sever** the shapefile/terrain ctors, `ComputeDamages`→`ConsequenceResult`, `GetPointMs`, `StructureDetails`, messaging.

- [ ] **Step 2: Write the failing fixture.** The upstream `InventoryShould` tests are all shapefile/terrain/ImpactArea-bound (severed), so build a **bespoke constructed inventory**: a `construct` block of 2–3 structures (varying `impact_area_id` and `st_damcat`) + one occ-type (the Task-4 occ-type), then assert:
  - `{method:"get_damage_categories", ... , mode:"vector"}` (string vector — add a string-vector compare mode if not present, else assert count/first via a numeric proxy; prefer capturing the categories as an ordered list).
  - `{method:"trim_to_impact_area_count", args:[<iaid>], expected:<n>, mode:"exact"}` (structure count after trim).
  - `{method:"generate_then_sample_struct_yvals", args:[<iteration>,0], convergence:{...}, expected:"PIN", mode:"vector"}` — `generate_random_numbers(cc)` then `sample_occupancy_types(iter,false)[0]` struct yvals (proves the `MaxIterations*2` seed-count wiring + per-occ-type RNG reproduce C#).
  - `{method:"validate_error_level", ... , mode:"exact"}` on an inventory with a bad structure (expect the aggregated `ErrorLevel` numeric value) and on an empty inventory (expect `Minor`).

  Keep the fixture's string-comparison needs minimal; if a string-vector mode is required, add it to all three runners in Task 7 (note it here).

- [ ] **Step 3: Wire emitter, run, confirm FAIL.** `EvalInventory` constructing the in-memory `Inventory` from the fixture and dispatching the methods. Add `patched/Inventory.cs` (in-memory ctor + numeric methods only) to the csproj.

- [ ] **Step 4: Implement** `inventory.hpp`. Resolve move-only storage: if `OccupancyType`/`Structure` are move-only, store `std::vector<Structure>` (move-constructed) and `std::map<std::string,OccupancyType>`; `get_inventory_trimmed_to_impact_area` must build a new `Inventory` — since the trimmed inventory shares occ-types, decide whether occ-types are copied or referenced and document (C# shares references; a faithful numeric port may copy the sampled state — ensure `generate_random_numbers` is called on the inventory actually sampled). Add loader + `inventory` dispatch in `test_fixtures.cpp`.

- [ ] **Step 5: Pin + verify.** `ctest` + gate green; gate count strictly up.

- [ ] **Step 6: Commit** `feat(structures): port Inventory numeric subset + fixtures`.

---

### Task 7: R/Python representative subset + closeout

**Files:** Modify `hecfdar/src/*.cpp` (+ `cpp11::cpp_register`), `hecfdar/tests/testthat/test-fixtures.R`, `hecfdapy/src/**` bindings, `hecfdapy/tests/test_fixtures.py`; Modify `.claude/CLAUDE.md`, `.claude/PLAN.md`, `.superpowers/sdd/progress.md`, and the memory file `~/.claude/projects/-Users-cam-projects-usace-hec-fda/memory/hecfda-port-project.md`.

**Interfaces:**
- Consumes: everything Tasks 1–6 produced.
- Produces: R/Python bindings + fixture-runner dispatch for a **representative subset** of the structures targets, matching the established R/Python scope convention (the R/Python runners validate a representative subset; full per-item parity lives in C++ + the gate).

- [ ] **Step 1:** Choose the representative subset to bind: `value_uncertainty` (a leaf sampler exercising the distribution factory through a new namespace) and `structure` (the end-to-end depth-damage compute exercising occ-type sampling + `PairedData::f(x,ref)`). Document in `.claude/CLAUDE.md` that the other structures targets traverse the identical binding/compiled core and are validated in C++ + the gate.

- [ ] **Step 2:** Add R binding functions (`[[cpp11::register]]` `hecfda_value_uncertainty_*`, `hecfda_structure_*`) in `hecfdar/src/*.cpp` that mirror the C++ `value_uncertainty`/`structure` fixture dispatch; run `cpp11::cpp_register("hecfdar")`; `R CMD INSTALL --preclean hecfdar`.

- [ ] **Step 3:** Extend `hecfdar/tests/testthat/test-fixtures.R` to load `fixtures/structures/value_uncertainty.json` + `fixtures/structures/structure.json` and dispatch through the new bindings (reuse the generic assertion loop). Run `Rscript -e 'testthat::test_local("hecfdar")'`.

- [ ] **Step 4:** Mirror in Python: add pybind11 bindings, `~/venv/hecfdapy/bin/python -m pip install --force-reinstall --no-deps ./hecfdapy`, extend `hecfdapy/tests/test_fixtures.py` to load the same two fixtures, `pytest hecfdapy/tests -q`.

- [ ] **Step 5:** If Task 6 required a string-vector or new compare mode, add it symmetrically to all three runners now; otherwise skip.

- [ ] **Step 6: Full four-leg exit gate.** Run `make test-core`, `make test-r`, `make test-py PYTHON=~/venv/hecfdapy/bin/python`, `make oracles`. All green; record the new gate count (should be well above Phase 2's 492). Confirm the Phase-0/2 invariants still hold: `sample_and_integrate(1234)==24.425549382855987` and the RNG digest unchanged.

- [ ] **Step 7: Docs + memory.** Update `.claude/CLAUDE.md` (Status → Phase 3 complete; add a "Phase 3 added `hecfda::model::structures`…" paragraph + any new faithful-bug entries discovered), `.claude/PLAN.md` (mark Phase 3 done, note Phase 4 stage-damage next), `.superpowers/sdd/progress.md` (final line), and the project memory file. Commit `docs(structures): Phase 3 closeout — bindings, exit gate, status`.

- [ ] **Step 8:** Stop. Do NOT open a PR or merge — the controller runs `superpowers:finishing-a-development-branch` and the user chooses.

---

## Self-Review

- **Spec coverage:** design-spec Phase 3 line ("Structures & inventory — `Structure`, `Inventory`, `OccupancyType`, the value / first-floor / depth-damage uncertainty sampling") — every listed class has a task (T1–T3 samplers, T4 OccupancyType, T5 Structure, T6 Inventory). Severed surfaces (spatial/metrics/CSV/persistence) are enumerated in the scope table and deferred to their phases.
- **Type consistency:** the sampler `sample(double)` / `sample(long,bool)` signatures are reused identically by `OccupancyType` (T4 consumes T1–T3); `DeterministicOccupancyType` field names feed `Structure::compute_damage` (T5 consumes T4); `Inventory::sample_occupancy_types` returns `std::vector<DeterministicOccupancyType>` consumed nowhere further this phase. `PairedData::f(x, int&)` and `UncertainPairedData` sampling are confirmed present from Phase 2.
- **Oracle discipline:** no hand-derived values; every non-literal expected is `"PIN"` then gate-captured. Deterministic upstream literals (`200/180`, `400/360`, occ-type `[0,10,20,30,40,50]`) are used as cross-checks, not as the sole source.
- **Placeholder scan:** each task carries exact file paths, the upstream source + line ranges to transcribe, the fixture rows with literal inputs, and the emitter/runner wiring steps. The only intentional deferral is gate-captured expecteds (`"PIN"`), per the project convention.
