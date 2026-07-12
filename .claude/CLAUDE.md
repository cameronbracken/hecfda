# CLAUDE.md -- hecfda

Context for Claude Code working in the `hecfda` repo. See `PLAN.md` (same dir) for the full
approved architecture and phasing.

## What this is

`hecfda` provides **R (`hecfdar`) and Python (`hecfdapy`) packages** for flood-damage-reduction
compute, built on a **single shared C++17 core** that is a faithful port of the USACE-HEC C#
libraries **HEC.FDA.Statistics** and the pure-numeric parts of **HEC.FDA.Model**.

Write the math once in C++, bind it twice (cpp11 for R, pybind11 for Python). Because both
packages run the same compiled code with a bit-exact port of .NET's seeded `Random`, seeded
results are identical across R and Python. Target: publishable to CRAN and PyPI.

Upstream C# source is vendored as a **dev-only git submodule** at `upstream/HEC-FDA`, pinned at
`f63682a86a30dc306a105689714a92bfd95956c5`. It is the diff baseline for provenance headers and the
dotnet oracle gate, and is NOT referenced by either package (CRAN/PyPI installs and CI are
unaffected; `actions/checkout` uses `submodules: false`). `dotnet` is installed locally, so oracle
values are curated from the C# test files AND verified reproducible against the real
`HEC.FDA.Statistics`/`HEC.FDA.Model` code.

Scope is the **numerical core only**. RAS-Mapper terrain/inundation reading, GIS/`Spatial`,
`hydraulics` RAS-grid ingest, `SQLite`/DBF persistence, `Serialization`, `LifeLoss` (LifeSim), and
all MVVM/messaging boilerplate are severed, never ported. Users of `hecfdar`/`hecfdapy` supply
already-extracted inputs (stage-frequency curves, per-frequency hydraulic stage profiles,
structure inventory tables) as plain arrays/data frames.

## Layout & the vendoring invariant

- `core/` -- canonical C++17 core (`include/hecfda/`, `tests/`, `CMakeLists.txt`). All numerical
  development happens here.
- `fixtures/` -- canonical language-neutral oracle fixtures (JSON). Single source of truth for
  expected values; see `fixtures/README.md` for the schema.
- `hecfdar/`, `hecfdapy/` -- the packages. Each vendors the core + fixtures as **subtree
  symlinks**, not committed copies: `hecfdar/src/hecfda_core -> ../../core`,
  `hecfdapy/src/hecfda_core -> ../../core`, `hecfdar/inst/fixtures -> ../../fixtures`,
  `hecfdapy/tests/fixtures -> ../../../fixtures`. Editing a core header is live through the
  symlink; nothing to re-sync.
- Builds dereference the symlinks into self-contained, symlink-free artifacts: `R CMD build` does
  it automatically for the R tarball; `tools/materialize_core.py` does it for Python (also needed
  on checkouts where symlinks don't survive, e.g. some Windows configurations -- CI runs it before
  every R and Python job).
- `tools/oracle_emitter/` (C#) + `tools/verify_oracles.py` -- the dotnet oracle gate: replays every
  fixture against the real `HEC.FDA.Statistics`/`HEC.FDA.Model` and fails on any value that
  doesn't reproduce to tolerance. Dev-only (needs `dotnet` + the submodule); not wired into CI.

Current core contents (Phase 0): `hecfda::sampling::DotNetRandom` (the .NET seeded `Random`
port), `hecfda::model::compute::RandomProvider` (`next_random`/`next_random_sequence`),
`hecfda::statistics::SpecialFunctions` (`reg_incomplete_gamma`, `log_gamma`),
`hecfda::statistics::distributions::Normal` (`pdf`/`cdf`/`inverse_cdf`/`fit`, over
`ContinuousDistribution`/`IDistribution`), `hecfda::statistics::Mathematics` (trapezoidal / CDF
integration), `hecfda::model::paired_data::PairedData` (`f`/`f_inverse`/`integrate`), and
`hecfda::model::paired_data::UncertainPairedData` (`sample_paired_data`/`sample_and_integrate`,
the vertical-slice type).

Phase 1 added the rest of `hecfda::statistics`: the validation subsystem (`ErrorLevel` + rules),
the full `SpecialFunctions` gamma/beta closure, `SampleStatistics`, the `IDistribution`
base/enum/factory with generic four-runner dispatch, all 13 distributions under
`statistics::distributions` (Normal, Uniform, Triangular, Deterministic, LogNormal,
TruncatedNormal, TruncatedLogNormal, PearsonIII, LogPearson3, TruncatedLogPearson3, Gamma,
ShiftedGamma, Empirical), `statistics::convergence::ConvergenceCriteria`,
`statistics::histograms::DynamicHistogram`, and
`distributions::UncertainToDeterministicDistributionConverter`. `Gamma`/`ShiftedGamma`/
`PearsonIII` are internal helper classes (not `IDistribution` members of the factory dispatch).

Phase 2 added the paired-data compute layer under `hecfda::model::paired_data`: the faithful .NET
`Array.BinarySearch` (replacing `std::lower_bound` in `PairedData`/`Empirical` lookups),
`CurveMetaData`, `PairedData` compose/multiply/`sum_ys_for_given_x`/monotonicity,
`UncertainPairedData` generalized from `Normal` to `IDistribution` with every sample path
(including deterministic-via-converter), and `GraphicalUncertainPairedData` +
`GraphicalDistribution` + `GraphicalFrequencyUncertaintyCalculators` + `InterpolateQuantiles`.
Phase 2 also added `-ffp-contract=off` project-wide (see "FP-contraction (FMA) parity" below).

Phase 3 added `hecfda::model::structures`: the three per-structure uncertainty samplers
(`ValueUncertainty`, `ValueRatioWithUncertainty`, `FirstFloorElevationUncertainty`),
`OccupancyType`/`DeterministicOccupancyType`/`OccupancyTypeBuilder`, `Structure::compute_damage`,
and `Inventory`'s numeric subset.

Phase 4 added `hecfda::model::metrics` (the consequence-binning substrate: `ConsequenceResult`,
`AggregatedConsequencesBinned`, `StudyAreaConsequencesBinned`, `ConsequenceExtensions`),
`Inventory::compute_damages` (the parallel per-structure damage collector), and
`hecfda::model::stage_damage` (`HydraulicProfiles` + `CorrectDryStructureWSEs` -- the
hydraulics-as-arrays input boundary replacing disk-backed `HydraulicDataset` --
`ImpactAreaStageDamage`'s aggregation-stage geometry + `Compute()`, and `ScenarioStageDamage`'s
outer per-impact-area compute loop). Deterministic-only scope this phase (`compute_is_deterministic
= true`); the tractable-curve oracle (`TractableStageDamageTests`) is the headline cross-check.
All phases 5-6 build outward from this base -- see `PLAN.md` for the dependency-ordered bulk-port
sequence.

## Build & test commands

```bash
# C++ core
cmake -S core -B core/build && cmake --build core/build && ctest --test-dir core/build --output-on-failure
# R (regenerate registration only after editing hecfdar/src/*.cpp)
Rscript -e 'cpp11::cpp_register("hecfdar")'; R CMD INSTALL --preclean hecfdar
Rscript -e 'testthat::test_local("hecfdar")'
# Python (dev venv at ~/venv/hecfdapy)
~/venv/hecfdapy/bin/python -m pip install --force-reinstall --no-deps ./hecfdapy
~/venv/hecfdapy/bin/python -m pytest hecfdapy/tests -q
# vendoring: the core + fixtures are subtree symlinks; builds dereference them (R CMD build for R,
# tools/materialize_core.py for Python). To get a symlink-free tree:
python3 tools/materialize_core.py    # rewrites the working tree; run in a throwaway checkout
# oracle reproduction gate (dev-only; needs dotnet + the upstream submodule)
DOTNET_ROLL_FORWARD=Major python3 tools/verify_oracles.py

# Makefile wraps all of the above:
make test-core
make test-r
make test-py PYTHON=~/venv/hecfdapy/bin/python   # PYTHON defaults to python3
make materialize
make oracles
```

Toolchain present: clang++/gcc, cmake, R with cpp11/testthat/jsonlite installed, Python 3 with a
dedicated venv at `~/venv/hecfdapy` (system Python is not used for `test-py`), and `dotnet 10` for
the oracle gate (not in CI). After any core change that alters a class layout, rebuild R clean
(`R CMD INSTALL --preclean hecfdar`) -- stale `hecfdar/src/*.o` from a prior ABI can otherwise
return garbage or abort R.

## RNG parity (non-negotiable)

`hecfda::sampling::DotNetRandom(seed)` must reproduce .NET's seeded `Random(seed).NextDouble()`
stream exactly -- verified against a real .NET capture in `core/tests/test_dotnet_random.cpp` and
against the live C# by the oracle gate. Every Monte Carlo fixture carries a seed; when porting new
sampling code, never hand-derive an expected value -- capture it from the real C# (via the oracle
gate) or from an existing upstream test literal. The pinned Phase 0 values:
`UncertainPairedData::sample_and_integrate(seed=1234) == 24.425549382855987` and the RNG digest
`sum(seed=12345, n=100000) == 50124.341288393982`, both identical across C++/R/Python and matching
the real C#.

## FP-contraction (FMA) parity (standing invariant, added Phase 2)

Clang and GCC default to fusing multiply-add expressions (`FP_CONTRACT` on, even at `-O0`), which
rounds differently than .NET's non-fused, strict left-to-right IEEE 754 arithmetic. Task P2T4b
found this the hard way: `GraphicalFrequencyUncertaintyCalculators::compute_slope`'s epsilon
(1e-5) finite-difference division amplifies a single ULP difference by roughly 1e5x, breaking
bit-for-bit reproduction at scattered indices for some inputs. `-ffp-contract=off` is now set
project-wide, non-MSVC only (MSVC already defaults to no contraction):

- `core/CMakeLists.txt` -- per test target
- `hecfdar/src/Makevars` and `Makevars.win` -- `PKG_CXXFLAGS = -ffp-contract=off`
- `hecfdapy/CMakeLists.txt` -- `target_compile_options(_core PRIVATE
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-ffp-contract=off>)`

Treat this like RNG parity: any future core code using epsilon finite-differences or other
FMA-sensitive arithmetic depends on this flag to reproduce identically across C++/R/Python/C#.
Never remove or scope it down without re-verifying every fixture in all three languages.

## Validation model (DRY)

Oracle values live ONLY in `fixtures/*.json`. Three thin generic runners load the same JSON and
apply every assertion: C++ `core/tests/test_fixtures.cpp` (nlohmann/json, vendored test-only),
R `hecfdar/tests/testthat/test-fixtures.R` (jsonlite), Python `hecfdapy/tests/test_fixtures.py`
(stdlib). Adding a distribution/model = new fixture file + a couple of dispatch-table entries per
runner -- no new per-item glue. Don't hardcode oracle values in test files. `verify_oracles.py` is
the fourth, dev-only check that the fixtures still match the C# source.

**R/Python distribution coverage scope:** the R and Python fixture runners exercise the generic
`distribution` target dispatch (`dist_eval`/`hecfda_dist_eval`) only against Normal and Uniform
fixtures; the remaining distributions traverse the identical binding and compiled core, so they are
validated transitively through R/Python and explicitly in C++ (`test_fixtures.cpp` loads every
`fixtures/distributions/*.json`) and against real C# via the `verify_oracles.py` gate -- this is not
full four-way per-distribution parity in R/Python, by design.

**R/Python structures coverage scope (Phase 3):** the R (`hecfda_value_uncertainty`/
`hecfda_structure`) and Python (`value_uncertainty`/`structure`) bindings cover a representative
subset of the new `hecfda::model::structures` targets: `value_uncertainty` (a leaf sampler
exercising the distribution factory through the new namespace) and `structure` (the end-to-end
depth-damage compute exercising occ-type sampling + `PairedData::f(x, ref)`). The remaining
structures targets -- `value_ratio_with_uncertainty`, `first_floor_elevation_uncertainty`,
`occupancy_type`, `inventory` -- traverse the identical binding and compiled core, so they stay
validated in C++ (`test_fixtures.cpp` loads every `fixtures/structures/*.json`) and against real
C# via the `verify_oracles.py` gate, matching the distribution-coverage convention above rather
than duplicating it with bespoke R/Python glue for every target.

**R/Python stage-damage coverage scope (Phase 4):** the R (`hecfda_consequence_result`/
`hecfda_impact_area_stage_damage`) and Python (`consequence_result`/`impact_area_stage_damage`)
bindings cover a representative subset of the new Phase-4 targets: `consequence_result` (a metrics
leaf exercising the increment accumulator through the new `hecfda::model::metrics` namespace) and
`impact_area_stage_damage` (the end-to-end deterministic stage-damage compute -- the phase's
headline result, reconstructing the tractable Residential/Commercial scenario from
`fixtures/stage_damage/impact_area_stage_damage.json`'s construct block exactly as
`test_fixtures.cpp`'s TEST_CASE body does, then running `Compute(true)`). The remaining Phase-4
targets -- `aggregated_consequences_binned`, `study_area_consequences_binned`,
`inventory_compute_damages`, `correct_dry_structure_wses`/`hydraulic_profiles`,
`stage_damage_geometry`, `scenario_stage_damage` -- traverse the identical binding and compiled
core, so they stay validated in C++ (`test_fixtures.cpp` loads every `fixtures/metrics/*.json` and
`fixtures/stage_damage/*.json`) and against real C# via the `verify_oracles.py` gate, matching the
distribution/structures-coverage convention above rather than duplicating it with bespoke R/Python
glue for every target.

## Conventions & gotchas

- **Structural mirroring:** C++ mirrors the C# file/class/method layout so upstream diffs map
  almost line-for-line. Each ported file carries a `// ported from: <path> @ <sha>` header.
- **Portability:** never use `M_PI` (absent under strict `-std=c++17` on Linux and on MSVC) -- use
  `hecfda::kPi`. Don't name a namespace alias `gamma` (clashes with glibc's `gamma()`) or `stat`
  (clashes with the MSVC/POSIX CRT `stat` symbol). Pass `-Wall/-Wextra` only to non-MSVC compilers
  in CMake (`core/CMakeLists.txt` already does this).
- **Self-contained core:** no external C++ deps (port HEC-FDA's own math/RNG). Don't add Eigen or
  Boost to `core/`.
- **cpp11 (R):** `Makevars`/`Makevars.win`: `CXX_STD = CXX17`, no `-O3`/`-march`/LTO/`-Werror`.
  Internal `[[cpp11::register]]` functions (`hecfda_*`) are unexported -- tests reach them via
  `asNamespace("hecfdar")`. After editing any `hecfdar/src/*.cpp`, re-run
  `cpp11::cpp_register("hecfdar")`.
- **ABI safety (R):** a core class-layout change invalidates stale `.o` files silently. Always
  `R CMD INSTALL --preclean hecfdar` after touching a header that changes a class's members.
- **Mutation:** the global "never mutate" rule is relaxed for distribution/model objects that
  mirror the C# stateful API (matches upstream design; see `PLAN.md`).
- **Dotnet gate subset-compile:** `tools/oracle_emitter/` targets `net9.0` and compiles only the
  minimal `HEC.FDA.Model` paired-data closure (`RandomProvider`, `PairedData`,
  `UncertainPairedData`, and their direct interface/attribute dependencies) directly as
  `<Compile Include=...>` items, rather than referencing `HEC.FDA.Model.csproj` (which needs a
  private RAS NuGet feed and 401s in this environment). It references the clean
  `HEC.FDA.Statistics.csproj` normally. Two local patches make this compile: a copy of
  `CurveMetaData.cs` under `tools/oracle_emitter/patched/` with `WriteToXML`/`ReadFromXML`
  stubbed out (avoids pulling in the `Serialization` XML closure), and an empty
  `namespace Amazon.Runtime { }` stub in `Stubs.cs` (the upstream file has a dead
  `using Amazon.Runtime;` that would otherwise need the AWS SDK). The emitter runs on the
  installed .NET 10 runtime against a net9.0 target via `RollForward=Major` in the csproj (also
  set as `DOTNET_ROLL_FORWARD=Major` by `verify_oracles.py`) -- do not "fix" this by bumping
  `TargetFramework` to net10.0 without checking upstream still builds clean there.
- **Fixture schema and comparison modes:** `abs | rel | exact | bool | vector | matrix`; see
  `fixtures/README.md`. `exact` uses `tol == 0.0` and also matches `NaN == NaN`.
- **Distribution factory keys (Phase 1):** `i_distribution_enum.hpp` uses **port-internal** enum
  values `TruncatedLogNormal = 1005` and `TruncatedLogPearson3 = 1006` for distributions whose C#
  `Type` property aliases an existing `DistributionType` (both alias `Normal`/`LogPearson3`
  respectively at the instance level). The factory key and the instance's `type()` are
  intentionally different values; `equals()` uses a checked `dynamic_cast` (not `type()`
  comparison) to avoid relying on that aliasing, and to avoid UB if the cast target is wrong.
- **Bespoke fixture targets:** `Gamma`, `ShiftedGamma`, and `PearsonIII` are internal helper
  classes, not `IDistribution`, so they are not reachable through the generic factory dispatch.
  Their fixtures use dedicated runner targets (`shifted_gamma`/`pearson3`) rather than the
  `construct`-by-enum-name path; `ConvergenceCriteria` and `DynamicHistogram` fixtures
  (`convergence_criteria`/`histogram`) follow the same bespoke pattern for the same reason.
- **Adding a distribution (Phase 1 recipe):** header under `statistics/distributions/` + a
  factory `switch` case + a name-mapping entry (the enum <-> string lookup used by fixtures) + a
  fixture file. No other runner glue needed -- the generic dispatch picks it up in all three
  languages automatically.
- **Faithful upstream bugs (deliberately reproduced, do NOT "fix" without an explicit upstream
  change to port):**
  - `SampleStatistics`'s median getter indexes into the **unsorted** input array (C# bug; the
    port copies it verbatim).
  - `Empirical`'s PDF returns 0 for any `x` that isn't an exact grid point, and its `Equals`
    has a field-mismatch (compares the wrong pair of members) inherited from the C# source.
  - `UncertainToDeterministicDistributionConverter`'s `LogPearsonIII` branch computes
    `pow(mean, 10)` (i.e. `logMean^10`), not the mathematically expected `10^logMean` --
    transcribed exactly as upstream wrote it.
  - `DynamicHistogram::EstimateIterationsRemaining` has a copy-paste bug reusing the wrong
    quantile in its estimate.
  - `LogNormal` mixes natural-log (`ln`) and `log10` semantics inconsistently between methods,
    matching the C# inconsistency rather than normalizing it.
  - `CurveMetaData` has an "unassiged" typo (misspelled, not "unassigned") in a member/constant
    name, transcribed verbatim.
  - `PairedData::multiply`'s doc comment claims "damages below curve are 0," which is inaccurate;
    the actual code clamps to `y[0]` (the first y-value), not zero. Comment kept as-is, behavior
    matches upstream code (not the comment).
  - `ExtrapolateFrequencyFunction` has an array-length mismatch bug in the narrow `-1e-4..0`
    input range, reproduced rather than fixed.
  - `GraphicalFrequencyUncertaintyCalculators` takes a `curveMetaData` constructor parameter that
    is dead -- never read by any method -- reproduced verbatim rather than dropped.
  - `OccupancyType::get_errors_from_properties()`'s `ComputeOtherDamage` block checks
    `use_content_to_structure_value_ratio_` (not `use_other_to_structure_value_ratio_`) to decide
    whether to query `_OtherToStructureValueRatio` vs. `_OtherValueError` for error messages -- a
    real C# copy-paste bug (`validate()`'s equivalent block correctly checks
    `use_other_to_structure_value_ratio_`); transcribed verbatim.
  - `Structure::compute_damage`'s "other" damage branch scales by
    `other_to_structure_value_ratio() / 100` twice when `use_osvr()` is true: once when the ratio
    is sampled in `OccupancyType::sample` (`.../ 100`) and again in `compute_damage`'s
    `(other_to_structure_value_ratio() / 100)` factor -- the CSVR path applies only one `/100`.
    Transcribed exactly as upstream wrote it, not reconciled.
  - `ValueUncertainty`'s rule message "The percent of inventory value must be greaeter than or
    equal to zero." misspells "greater" as "greaeter" in the real C# string; kept verbatim.
  - `FirstFloorElevationUncertainty`'s distribution-allow-list rule message says "...can be used
    for value ratio uncertainty" -- a copy-paste wording quirk inherited verbatim from
    `ValueRatioWithUncertainty::add_rules()` (this class is `FirstFloorElevationUncertainty`, not
    a value-ratio type); transcribed as-is.
  - `ValueUncertainty::add_rules()` validates `distribution_type` against {Normal, Uniform,
    Deterministic, Triangular} -- `LogNormal` is NOT in that allow-list even though `sample()`'s
    switch has a live `LogNormal` case, so a LogNormal `ValueUncertainty` is "invalid" per
    `validate()` but still functionally sampleable; transcribed exactly as upstream wrote it, not
    reconciled.
  - `Inventory::compute_damages`'s other/vehicle store-swap: the scratch collections are written
    with `other`/`vehicle` SWAPPED, then `aggregate_results` is called with those two collections
    positionally swapped back -- two "bugs" that compose to the correct result (see
    `inventory.hpp`'s doc comment above `compute_damages` for the full symbol trace). Transcribed
    exactly as upstream wrote it (both the store step and the aggregate-results call), not
    collapsed to the equivalent direct call, so a future upstream fix to only one side breaks this
    port's tests instead of silently diverging.
  - `ImpactAreaStageDamage::compute_damage_with_uncertainty_all_coordinates`'s convergence loop
    hard-wires `compute_chunk_quantity = 100` (a literal reassignment, not `+= 100` and not a
    remaining-iterations estimate) on every non-converged pass after the first. Transcribed
    verbatim from the upstream "TODO: hard-wire in an additional 10000 iterations" comment; never
    actually exercised by this phase's fixtures because the deterministic path
    (`compute_is_deterministic=true`) always converges after the first chunk (zero-variance
    histogram), but ported for interface completeness ahead of Phase 5's Monte Carlo path.
  - `UncertainToDeterministicDistributionConverter`'s `IHistogram` branch (Phase 4 Task 4)
    downcasts to `DynamicHistogram` and reads its `SampleMean` -- this closes the converter's
    previously-DEFERRED "Phase 2" IHistogram case now that `DynamicHistogram` is a live
    `UncertainPairedData` Yvals type; not a bug, but recorded here since the class comment's
    original "SCOPE" note called it out as deferred and that is no longer true as of Phase 4.
- **By-value capture in Validation-rule predicates (standing invariant, Phase 3):**
  `ValueUncertainty`, `ValueRatioWithUncertainty`, `FirstFloorElevationUncertainty`, and
  `Structure` are all held **by value** inside containers that relocate them after construction
  (move-only `OccupancyType`, move-assigned by `OccupancyTypeBuilder`; `std::vector<Structure>`
  inside `Inventory`, which move-constructs elements on `push_back`/growth). Any
  `add_single_property_rule` predicate that captures `[this]` would dangle after such a
  relocation -- confirmed via ASan (stack-use-after-scope) while building `Inventory` in Task 6.
  All four classes' `add_rules()` capture checked fields **by value**
  (`[value = field_]`/`[dist = distribution_type_]`) instead; safe because every captured field is
  set once in the ctor and never mutated after. Apply the same by-value-capture pattern to any
  future `Validation`-derived class held by value inside a relocating container. **Open follow-up
  (STILL OPEN, now closer to relevant):** `ConvergenceCriteria` (Phase 1) has the same
  `[this]`-capture shape and was deliberately left unfixed in Phase 3 because it wasn't reachable
  through a relocating container. Phase 4's `ImpactAreaStageDamage` holds a `ConvergenceCriteria`
  member BY VALUE, and `ScenarioStageDamage` holds `std::vector<ImpactAreaStageDamage>` (built via
  `push_back`, which relocates on growth) -- so `ConvergenceCriteria` is now transitively inside a
  relocating container. No crash has been observed (nothing in this port's `ImpactAreaStageDamage`/
  `ScenarioStageDamage` code path calls `ConvergenceCriteria::validate()` on that member, so its
  `[this]`-capturing rules are registered but never invoked), but the ASan hazard is now live in
  principle -- fix in Phase 5 or a cleanup pass before anything calls `validate()` on a
  `ConvergenceCriteria` reached this way.

## Git & CI

- Commits are SSH-signed automatically, identity `Cam Bracken <cameron.bracken@pm.me>`. Push only
  when asked.
- `.github/workflows/ci.yml`: `core` (3 OS) + `r-cmd-check` (3 OS) + `python` (3 OS x {3.10,
  3.12}). Every `actions/checkout` uses `submodules: false` -- the upstream submodule and the
  dotnet gate never run in CI. `tools/materialize_core.py` runs before the R and Python jobs so a
  checkout that didn't preserve the symlinks (e.g. some Windows runners) still builds.
- Never commit: OS junk, IDE settings, secrets, dotnet build output, other AI-tool files
  (`.gitignore` covers these). **Exception (deliberate):** this repo tracks two curated context
  files -- `.claude/CLAUDE.md` and `.claude/PLAN.md` -- because the C++/R/Python-from-C# port is a
  complex, multi-phase process whose plan and porting guidance should travel with the repo. The
  `.gitignore` un-ignores exactly those two; everything else in `.claude/` (settings, scratch
  plans) stays ignored.

## Status

**Phase 0, Phase 1, Phase 2, Phase 3, and Phase 4 are complete.** The Phase 0 vertical slice (.NET `Random` ->
`Normal` -> `PairedData` -> `UncertainPairedData.sample_and_integrate`) passes identically in
C++, R, and Python; the seeded RNG stream is byte-identical across all three and matches a real
.NET capture. Phase 1 (Statistics foundation) ported the validation subsystem, full
`SpecialFunctions`, `SampleStatistics`, the distribution base/enum/factory with generic
four-runner dispatch, all 13 distributions, `ConvergenceCriteria`, `DynamicHistogram`, and the
`UncertainToDeterministicDistributionConverter`.

Phase 2 (paired-data compute) delivered the full paired-data curve algebra (`PairedData`
compose/multiply/`sum_ys_for_given_x`/monotonicity, `CurveMetaData`), the faithful .NET
`Array.BinarySearch` (closing the top Phase-1 risk: `std::lower_bound` diverged from C# on
duplicate x/y values), `UncertainPairedData` generalized to `IDistribution` with all sample paths
including deterministic-via-converter, and `GraphicalUncertainPairedData` +
`GraphicalDistribution` + the graphical uncertainty calculators + `InterpolateQuantiles`. Phase 2
also found and closed a cross-language FMA divergence: `-ffp-contract=off` is now set in
`core/CMakeLists.txt`, `hecfdar/src/Makevars`/`Makevars.win`, and `hecfdapy/CMakeLists.txt` (see
"FP-contraction (FMA) parity" above) -- a standing invariant alongside RNG parity.

Phase 3 (structures & inventory) ported `hecfda::model::structures`: the three per-structure
uncertainty samplers (`ValueUncertainty`, `ValueRatioWithUncertainty`,
`FirstFloorElevationUncertainty`), `OccupancyType` + `DeterministicOccupancyType` + the move-only
`OccupancyTypeBuilder` (binding the three samplers to the Phase-2 `UncertainPairedData`
depth-percent-damage curves via per-category seeded RNG), `Structure`'s numeric
`ComputeDamage`/`FindOccType`, and `Inventory`'s numeric subset (in-memory construction,
damage-category enumeration, impact-area trim, per-occ-type RNG generation + sampling, ground
elevations, `Validate` aggregation). RAS-Mapper/GIS/CSV/persistence/LifeSim surfaces on these
classes stay severed per the project's numerical-core scope. Phase 3 also fixed a C++-port-only
memory-safety defect (ASan-confirmed use-after-scope): `ValueUncertainty`, `ValueRatioWithUncertainty`,
`FirstFloorElevationUncertainty`, and `Structure` all switched their `Validation`-rule predicates
from `[this]` to by-value field capture -- see "By-value capture in Validation-rule predicates"
above for the full invariant and the still-open `ConvergenceCriteria` follow-up.

Phase 4 (stage-damage) ported `hecfda::model::metrics` (`ConsequenceResult`,
`AggregatedConsequencesBinned`, `StudyAreaConsequencesBinned`, `ConsequenceExtensions`),
`Inventory::compute_damages`, and `hecfda::model::stage_damage` (`HydraulicProfiles` +
`CorrectDryStructureWSEs`, `ImpactAreaStageDamage`'s aggregation-stage geometry + `Compute()`, and
`ScenarioStageDamage`'s outer compute loop). This closes the end-to-end deterministic
stage-damage path: seeded structure inventory -> per-stage consequence binning -> a
damage-category/asset-category stage-damage `UncertainPairedData`, cross-checked against the real
`TractableStageDamageTests` literals. Deterministic-only scope (`compute_is_deterministic=true`);
Monte Carlo stage-damage and EAD-level aggregation are Phase 5. R and Python bind a representative
subset (`consequence_result`, `impact_area_stage_damage`) per the "R/Python stage-damage coverage
scope" convention above; the remaining Phase-4 targets are validated in C++ + the gate only.

The exit gate is green on all four legs: `make test-core` (ctest, C++, all passing, ~61s --
`test_fixtures` alone accounts for most of that; see "Deferred from Phase 4" below), `make test-r`
(testthat, 134 passed / 0 failed), `make test-py` (pytest, 12 passed / 0 failed), and `make
oracles` (dotnet gate, 641 reproduced / 0 failed -- up from Phase 3's 537). The Makefile targets
and 3-platform CI are green. The `24.425549382855987` cross-language value and the RNG digest
(FMA-insensitive) reproduce unchanged in all four legs, and the Phase 3 structures fixtures
(`value_uncertainty`, `structure`) still pass in R and Python.

Severed/deferred from Phase 2: `CurveMetaData`/`GraphicalDistribution`/
`GraphicalUncertainPairedData` XML + `ValidationErrorLogger`/GUI wiring;
`UncertainPairedData.CombineWithWeights` (depends on severed `Empirical` stacking, untested),
`Equals`, and `ConvertDamagedElementCountToText`; the graphical path is validated in C++ and the
oracle gate but not yet bound in R/Python (the `-ffp-contract=off` flag is already in place for
when it lands).

Deferred from Phase 3: `Inventory::get_inventory_and_water_trimmed_to_damage_category` has no
fixture coverage (not exercised by any Task 6 case); `OccupancyType::error_messages_for()` formats
`ErrorLevel` as a raw int rather than the C# `[Flags]` enum name (e.g. "2" instead of "Minor") --
a pre-existing gap in `hecfda::statistics::Validation`, not new to Phase 3, and not exercised by
any fixture.

Deferred from Phase 4 (Minor -- revisit in Phase 5 or a cleanup pass): the analytical-frequency
branch of `ImpactAreaStageDamage::identify_central_stage_frequency_at_index_location` (analytical
flow frequency WITH a discharge-stage function) throws `std::logic_error` rather than computing,
because it needs the unported `ContinuousDistribution::to_coordinates` (a UI/graphing concern,
never ported -- see `continuous_distribution.hpp`); no fixture reaches this branch, every oracle
case uses the graphical-frequency path instead. `ImpactAreaStageDamage::produce_zero_damage_functions`
(the empty-inventory path) and the length/empty-input guards on
`HydraulicProfiles::get_corrected_wses`/`correct_dry_structure_wses`/`set_coordinate_quantity` are
implemented straight from the C# source but have no fixture exercising an empty inventory or
empty/mismatched-length input arrays -- add explicit guard-clause fixtures if a future caller can
reach these paths with malformed input. The `ConvergenceCriteria` by-value-in-a-relocating-
container hazard (see "By-value capture in Validation-rule predicates" above) is now reachable via
`ScenarioStageDamage`'s `vector<ImpactAreaStageDamage>` but not yet triggered by any code path that
calls `validate()` on it. `core/tests/test_fixtures.cpp`'s `test_fixtures` ctest target costs ~61s
because `impact_area_stage_damage`/`scenario_stage_damage` each run the full 1000-iteration
`Compute()` convergence loop per case even on the deterministic (zero-variance, converges-after-
first-chunk) path -- a known, accepted cost of exercising the real convergence loop rather than a
regression.

See `PLAN.md` for the conventions established in Phase 1-4 (port-internal factory keys, bespoke
fixture targets, the faithful-bug list, the by-value-capture invariant, the hydraulics-as-arrays
boundary), the Phase 5-6 plan, and the FMA parity detail. **Next: Phase 5 -- compute + metrics**
(`ImpactAreaScenarioSimulation`/EAD Monte Carlo and EAD-level metrics/results/thresholds/
performance), which builds on the stage-damage layer Phase 4 delivered.
