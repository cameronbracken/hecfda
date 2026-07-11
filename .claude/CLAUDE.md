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
All phases 2-6 build outward from this base -- see `PLAN.md` for the dependency-ordered bulk-port
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

## Validation model (DRY)

Oracle values live ONLY in `fixtures/*.json`. Three thin generic runners load the same JSON and
apply every assertion: C++ `core/tests/test_fixtures.cpp` (nlohmann/json, vendored test-only),
R `hecfdar/tests/testthat/test-fixtures.R` (jsonlite), Python `hecfdapy/tests/test_fixtures.py`
(stdlib). Adding a distribution/model = new fixture file + a couple of dispatch-table entries per
runner -- no new per-item glue. Don't hardcode oracle values in test files. `verify_oracles.py` is
the fourth, dev-only check that the fixtures still match the C# source.

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

**Phase 0 and Phase 1 are complete.** The Phase 0 vertical slice (.NET `Random` -> `Normal` ->
`PairedData` -> `UncertainPairedData.sample_and_integrate`) passes identically in C++, R, and
Python; the seeded RNG stream is byte-identical across all three and matches a real .NET capture.
Phase 1 (Statistics foundation) ported the validation subsystem, full `SpecialFunctions`,
`SampleStatistics`, the distribution base/enum/factory with generic four-runner dispatch, all 13
distributions, `ConvergenceCriteria`, `DynamicHistogram`, and the
`UncertainToDeterministicDistributionConverter`. The exit gate is green on all four legs:
`make test-core` (ctest, C++), `make test-r` (testthat, 37 passed / 0 failed), `make test-py`
(pytest, 6 passed), and `make oracles` (dotnet gate, 366 reproduced / 0 failed -- up from Phase
0's 18). The Makefile targets and 3-platform CI are green. See `PLAN.md` for the conventions
established in Phase 1 (port-internal factory keys, bespoke fixture targets, the faithful-bug
list), the Phase 2-6 plan, and the top Phase-2 risk (binary-search divergence on duplicate x/y
values in paired data).
