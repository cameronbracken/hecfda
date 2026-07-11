# HEC-FDA Port — Phase 2: Paired-Data Compute Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Complete the `HEC.FDA.Model/paireddata` curve-algebra layer on the shared C++17 core — the full `PairedData` (compose/multiply/sum/monotonicity), the generalized `UncertainPairedData` (holding any `IDistribution`, all sample paths incl. deterministic), `GraphicalUncertainPairedData`, `CurveMetaData`, and `InterpolateQuantiles` — each fixture-validated in C++/R/Python and reproduced against real HEC-FDA C# by the dotnet oracle gate. Close the top Phase-1 risk (`lower_bound` vs `Array.BinarySearch`) first.

**Architecture:** Extend the Phase-0 thin paired-data slice (`core/include/hecfda/model/paired_data/{paired_data,uncertain_paired_data}.hpp`) using the Phase-1 distribution factory + the D1 converter. Each ported file mirrors the C# and carries a `// ported from: <path> @ <sha>` header. Oracle values live only in `fixtures/*.json`; the four runners (C++ doctest, R testthat, Python pytest, dotnet emitter) validate them.

**Tech Stack:** C++17 (doctest), cpp11 (R), pybind11 (Python), dotnet gate (net9 build on net10 runtime via `DOTNET_ROLL_FORWARD=Major`).

**Branch:** `phase2` (off `main` @ the Phase-1 merge). Upstream pinned `upstream/HEC-FDA` @ `f63682a86a30dc306a105689714a92bfd95956c5`.

## Global Constraints

- **C++17 only.** No C++20. Self-contained core.
- **Structural mirroring:** each ported file opens with `// ported from: <upstream-path> @ f63682a86a30dc306a105689714a92bfd95956c5`; mirror the C# method layout/order. Transcribe verbatim; reproduce upstream bugs (do not "fix"), documenting each.
- **Portability:** never `M_PI` (use `hecfda::kPi`); `-Wall/-Wextra` non-MSVC only.
- **No hardcoded oracle values in test code.** Oracles live in `fixtures/*.json`, sourced from the upstream `HEC.FDA.ModelTest` paired-data tests, gate-reproduced.
- **Namespaces:** `hecfda::model::paired_data`, `hecfda::model` as needed.
- **RNG parity:** MC sample fixtures carry a seed read from the case-level `seed` field. `UncertainPairedData.GenerateRandomNumbers(seed, size)` uses the ported .NET `Random` (from Phase 0), so seeded draws must reproduce C# bit-for-bit.
- **Reuse Phase 1:** distributions via `IDistributionFactory::create(type, params)`; the D1 `convert_distribution_to_deterministic` for the deterministic sample path.
- **Commits:** SSH-signed; identity `Cam Bracken <cameron.bracken@pm.me>`; conventional-commit; push only when asked.
- **ABI (R):** after a core class-layout change, `R CMD INSTALL --preclean hecfdar`; after editing `hecfdar/src/*.cpp`, re-run `cpp11::cpp_register`. **Python dev venv:** `~/venv/hecfdapy`.
- **Definition of done per task:** `ctest` green; R fixtures pass; Python fixtures pass; `python3 tools/verify_oracles.py` green; commit.

## What Phase 0 already ported (extend, don't duplicate)

- `paired_data.hpp`: `PairedData(xs, ys)`, `f(x)`, `f_inverse(y)`, `integrate(with_padding)`. Uses `std::lower_bound` (TO BE FIXED in Task 1).
- `uncertain_paired_data.hpp`: a THIN slice holding `std::vector<Normal>` with `sample_paired_data_raw(probability)` + `sample_and_integrate(seed)`. TO BE GENERALIZED to `IDistribution` (Task 3).

## File Structure

```
core/include/hecfda/model/paired_data/
  dotnet_binary_search.hpp          # Task 1: faithful Array.BinarySearch
  curve_meta_data.hpp               # Task 2: full CurveMetaData (no XML)
  i_paired_data.hpp i_composable.hpp i_integrate.hpp i_multiply.hpp i_sample.hpp
  i_category.hpp i_metadata.hpp i_paired_data_producer.hpp   # Task 2: interfaces (as needed)
  paired_data.hpp                   # Tasks 1,2: fix search + add compose/multiply/sum/monotonicity
  uncertain_paired_data.hpp         # Task 3: generalize to IDistribution + all sample paths
  interpolate_quantiles.hpp         # Task 4
  graphical_uncertain_paired_data.hpp   # Task 4
core/include/hecfda/model/utilities/graphical_frequency_uncertainty_calculators.hpp  # Task 4 (if needed by graphical)
core/tests/  (bespoke paired_data / uncertain_paired_data / graphical fixture targets already exist from Phase 0; extend)
fixtures/paired_data/*.json
hecfdar/src/glue.cpp  hecfdapy/src/bindings/glue.cpp   # Task 5: generalized UPD/PairedData binding
tools/oracle_emitter/Program.cs                        # each task: emitter dispatch for new methods
```

---

### Task 1: Faithful `Array.BinarySearch` + fix `PairedData` search (THE TOP RISK)

**Files:** Create `core/include/hecfda/model/paired_data/dotnet_binary_search.hpp`; Modify `paired_data.hpp` (`f`, `f_inverse`) and `core/include/hecfda/statistics/distributions/empirical.hpp` (its InverseCDF/CDF binary search); Fixture `fixtures/paired_data/duplicate_values.json`.

**Interfaces:**
- Produces: `hecfda::model::paired_data::dotnet_binary_search(const std::vector<double>& arr, double value) -> long` returning the match index or the bitwise complement `~insertionPoint`, EXACTLY as .NET `Array.BinarySearch<double>` does.

**Why:** `std::lower_bound` returns the FIRST equal element; .NET `Array.BinarySearch` returns an arbitrary-but-deterministic matching index (midpoint-driven). On duplicate x (flat frequency) or duplicate y (flat damage) segments — ubiquitous in real FDA curves — `f`/`f_inverse` pick a different neighbor and interpolate differently. The port must match the C# index.

- [ ] **Step 1:** Read `Array.BinarySearch` semantics. Port the exact algorithm: `int lo=0, hi=n-1; while(lo<=hi){ int i = lo + ((hi-lo)>>1); int c = compare(arr[i], value); if(c==0) return i; if(c<0) lo=i+1; else hi=i-1; } return ~lo;` (with `compare` = `Comparer<double>.Default`, i.e. `arr[i].CompareTo(value)` — note NaN ordering if relevant). Return a signed index; `~lo` is the "not found, insertion point" encoding the C# `f` decodes with `~index`.
- [ ] **Step 2:** Write the failing `fixtures/paired_data/duplicate_values.json`: `paired_data` target, construct a curve with DUPLICATE x values and one with DUPLICATE y values (flat segments), assert `f`/`f_inverse` at points that land inside/adjacent to the duplicates. Set `expected` = "PIN"; the values come from real C# via the gate. (Also add a case from `PairedDataShould.cs` if it has a duplicate-value test.)
- [ ] **Step 3:** Run — confirm the CURRENT `lower_bound`-based `f`/`f_inverse` produce values that DISAGREE with the C# gate on the duplicate cases (this demonstrates the bug). Capture.
- [ ] **Step 4:** Implement `dotnet_binary_search.hpp`; replace the `std::lower_bound` + `*it==x` logic in `paired_data.hpp` `f` and `f_inverse` with `dotnet_binary_search` + the `~index` decode (mirroring the C# `Array.BinarySearch(...)>=0 ? match : ~index` branch exactly). Do the same in `empirical.hpp` wherever it binary-searches its ordinate arrays.
- [ ] **Step 5:** Pin the fixture from the gate; re-run. `ctest` + gate green; the duplicate cases now reproduce C#. Confirm ALL prior fixtures still pass (Empirical's existing fixtures must not regress).
- [ ] **Step 6:** Commit `fix(model): faithful .NET Array.BinarySearch in PairedData/Empirical (closes lower_bound duplicate-value divergence)`.

---

### Task 2: CurveMetaData + interfaces + PairedData completion

**Files:** Create `curve_meta_data.hpp` + the paired-data interface headers actually used; Modify `paired_data.hpp` (add methods); Fixtures `fixtures/paired_data/paired_data_ops.json`.

**Interfaces:**
- Produces: `CurveMetaData` (fields XLabel/YLabel/Name/DamageCategory/AssetCategory/IsNull/ImpactAreaID + the 5 ctors, per `CurveMetaData.cs`; SKIP WriteToXML/ReadFromXML — documented severance). `PairedData` gains `compose(const IPairedData&)`, `sum_ys_for_given_x(const IPairedData&)`, `multiply(const IPairedData&)`, `force_weak_monotonicity_bottom_up()`, `force_strict_monotonicity_top_down()`, `force_strict_monotonicity_bottom_up()`, `sort_to_increasing_x_vals()`, and the `f(x, int& index_of_previous_top_of_segment)` overload, plus a `metadata()` accessor.

- [ ] **Step 1:** Port `CurveMetaData.cs` (5 ctors + fields; XML severed). Give `PairedData` a `CurveMetaData` member (default null-equivalent) matching the C# `PairedData(xs, ys, metadata=null)`.
- [ ] **Step 2:** Port the paired-data interfaces (`IPairedData`, `IComposable`, `IIntegrate`, `IMultiply`, `ISample`, `ICategory`, `IMetaData`, `IPairedDataProducer`) — small; port the ones `PairedData`/`UncertainPairedData` implement or consume. `compose`/`sum_ys_for_given_x`/`multiply` take an `IPairedData` (so `PairedData` implements `IPairedData`).
- [ ] **Step 3:** Write failing `fixtures/paired_data/paired_data_ops.json` from `PairedDataShould.cs`: the `Compose`, `SumYsForGivenX_Test`, `multiply` (fragility), and monotonicity-forcing test cases (oracle values verbatim from the test). Extend the bespoke `paired_data` fixture dispatch in `test_fixtures.cpp` to handle these methods (compose/sum/multiply take a second curve from the fixture; the monotonicity methods mutate then read xvals/yvals).
- [ ] **Step 4:** Transcribe `compose`, `SumYsForGivenX` (the sorted-x-union merge), `multiply` (the fragility-weighted damage, "all damages below the curve are 0"), and the three monotonicity-forcing methods + `SortToIncreasingXVals` + the `f(x, ref index)` overload from `PairedData.cs` VERBATIM.
- [ ] **Step 5:** Add the matching emitter cases (real C# `PairedData` compose/SumYsForGivenX/multiply/monotonicity). Gate green. `ctest`/R/Python green.
- [ ] **Step 6:** Commit `feat(model): CurveMetaData + PairedData compose/multiply/sum/monotonicity`.

---

### Task 3: Generalize UncertainPairedData to IDistribution + all sample paths

**Files:** Modify `uncertain_paired_data.hpp` (major generalization); Fixtures `fixtures/paired_data/uncertain_paired_data_ops.json`; update the R/Python glue + emitter for the new construct.

**Interfaces:**
- Produces: `UncertainPairedData(std::vector<double> xs, std::vector<std::unique_ptr<IDistribution>> ys, CurveMetaData metadata)`; `void generate_random_numbers(int seed, long size)`; `PairedData sample_paired_data(double probability)` (applies `ForceWeakMonotonicityBottomUp`); `PairedData sample_paired_data_raw(double probability)`; `PairedData sample_paired_data_raw_deterministic()`; `PairedData sample_paired_data(long iteration_number, bool retrieve_deterministic_representation)`; `static UncertainPairedData combine_with_weights(...)`. The deterministic path uses `hecfda::statistics::distributions::convert_distribution_to_deterministic` (D1).

- [ ] **Step 1:** Read `UncertainPairedData.cs` fully. Rewrite the Phase-0 thin slice: store `std::vector<std::unique_ptr<IDistribution>> ys_` (not `Normal`). The fixture construct becomes an array of `{type, params}` distribution specs; the dispatch builds each via `IDistributionFactory::create`. Keep the Phase-0 `sample_and_integrate(seed)` behavior working (it becomes: `generate_random_numbers`/one draw → `sample_paired_data_raw` → integrate) — the Phase-0 pinned value `24.425549382855987` MUST still reproduce (it used one Normal-per-point sampled at one probability; confirm the generalized path reproduces it, or update that fixture's expectation if the generalization changes the semantics — but prefer preserving it).
- [ ] **Step 2:** Port `GenerateRandomNumbers(seed, size)` (fills `_RandomNumbers` via the ported .NET `Random`), `IsDistributionArrayValid`, all `SamplePairedData*` overloads (probability / raw / raw-deterministic / iterationNumber+deterministic — the last uses the converter), and `CombineWithWeights`. `SamplePairedData(double)` and the iteration overload call `ForceWeakMonotonicityBottomUp` (from Task 2). Reproduce any upstream quirks.
- [ ] **Step 3:** Write `fixtures/paired_data/uncertain_paired_data_ops.json` from `UncertainPairedDataShould.cs`: seeded sample cases (construct with mixed distribution types incl. non-Normal), the deterministic-representation cases, and `CombineWithWeights` if tested. Seeds in the case-level `seed` field. Extend the bespoke `uncertain_paired_data` dispatch.
- [ ] **Step 4:** Add emitter cases (real C# `UncertainPairedData` with the same distribution-typed ys + seed); the gate proves seeded draws + deterministic paths reproduce C# bit-for-bit. Gate green.
- [ ] **Step 5:** Update the R (`hecfdar/src/glue.cpp` `hecfda_upd_*`) and Python (`hecfdapy/.../glue.cpp`) bindings for the generalized construct (ys as a list of {type, params} or parallel arrays), keep the Phase-0 `upd_sample_integrate` entry working, `cpp11::cpp_register`, `--preclean`. R/Python fixtures green; the `24.425549382855987` cross-language value preserved.
- [ ] **Step 6:** Commit `feat(model): generalize UncertainPairedData to IDistribution + all sample paths (deterministic via converter)`.

---

### Task 4: InterpolateQuantiles + GraphicalUncertainPairedData

**Files:** Create `interpolate_quantiles.hpp`, `graphical_uncertain_paired_data.hpp`, and (if the graphical curve needs it) `core/include/hecfda/model/utilities/graphical_frequency_uncertainty_calculators.hpp`; Fixtures `fixtures/paired_data/graphical.json`.

**Interfaces:**
- Produces: `InterpolateQuantiles` (the small quantile-interpolation helper) and `GraphicalUncertainPairedData` (the non-parametric uncertain frequency curve — read `GraphicalUncertainPairedData.cs`; it produces an `UncertainPairedData`/samples curves with graphical uncertainty). Port `GraphicalFrequencyUncertaintyCalculators` from `HEC.FDA.Model/utilities/` if `GraphicalUncertainPairedData` depends on it.

- [ ] **Step 1:** Read `InterpolateQuantiles.cs` (37 LOC) + `GraphicalUncertainPairedData.cs` (208 LOC) + `utilities/GraphicalFrequencyUncertaintyCalculators.cs`. Port `InterpolateQuantiles` first (dependency).
- [ ] **Step 2:** Port `GraphicalUncertainPairedData` + the calculators it needs. Note the RNG usage (graphical uncertainty sampling is seeded).
- [ ] **Step 3:** Write `fixtures/paired_data/graphical.json` from `GraphicalUncertaintyPairedDataTests.cs` + `GraphicalFrequencyUncertaintyCalculatorsTests.cs` (seeded). Add a bespoke `graphical_uncertain_paired_data` fixture target + emitter case.
- [ ] **Step 4:** Gate green (seeded graphical draws reproduce C#). `ctest`/R/Python green.
- [ ] **Step 5:** Commit `feat(model): InterpolateQuantiles + GraphicalUncertainPairedData`.

---

### Task 5: Bindings + Phase-2 closeout

**Files:** finalize R/Python glue for the paired-data surface users need; update `.claude/CLAUDE.md`/`PLAN.md`; run the full exit gate.

- [ ] **Step 1:** Ensure the R/Python packages expose the paired-data operations a user needs for the compute layer (at minimum the generalized `UncertainPairedData` sampling + `PairedData` compose/multiply/integrate), reached through the generic bindings; add fixtures exercising them in R/Python where they're user-facing.
- [ ] **Step 2:** Run `make test-core && make test-r && make test-py PYTHON=~/venv/hecfdapy/bin/python && make oracles` — all green (report the gate count).
- [ ] **Step 3:** Update `.claude/CLAUDE.md`/`PLAN.md`: Phase 2 COMPLETE; list the paired-data surface delivered; confirm the `lower_bound` risk is CLOSED (Task 1); document any severances (CurveMetaData XML; any graphical sub-feature deferred) and any faithful upstream bugs reproduced. Carry forward the next-phase target: **Phase 3 — structures & inventory** (`HEC.FDA.Model/structures`), which builds structure depth-damage uncertainty on this paired-data layer.
- [ ] **Step 4:** Commit `docs: Phase 2 complete (paired-data compute layer)`.

---

## Self-Review

**Spec coverage:** the design spec's Phase 2 (paired-data) maps to: the `Array.BinarySearch` fix (Task 1, closing the top Phase-1 risk), `CurveMetaData` + interfaces + `PairedData` completion (Task 2), `UncertainPairedData` generalization incl. the deterministic path that Phase-1 D1 deferred (Task 3), `GraphicalUncertainPairedData` + `InterpolateQuantiles` (Task 4), and bindings/closeout (Task 5). Every `.cs` in `HEC.FDA.Model/paireddata/` is covered. Severed-as-boilerplate (documented): `CurveMetaData` XML methods; `UncertainPairedData.ConvertDamagedElementCountToText` / XML if present (text/serialization).

**Placeholder scan:** per-task cards direct the implementer to the cited C# file (the literal spec) rather than re-transcribing ~1200 lines — the mirror-port convention proven in Phases 0/1. Task 1's fixture and the compute-then-pin cases use the gate as the oracle (the Phase-0/1 precedent). No un-run fixtures: every fixture is wired into the C++ runner AND the gate (the Phase-1 closeout lesson).

**Type consistency:** `IPairedData`/`PairedData` (`f`/`f_inverse`/`integrate`/`compose`/`sum_ys_for_given_x`/`multiply`/monotonicity), `CurveMetaData`, `UncertainPairedData` (`generate_random_numbers`/`sample_paired_data*`/`combine_with_weights`), `dotnet_binary_search`, and the D1 converter are defined in Tasks 1-3 and consumed consistently downstream.

## Notes for the executor

- Task 1 is the linchpin and the whole reason paired-data is its own phase: do it first, and prove (Step 3) that the OLD code diverged before fixing, so the fix is demonstrably necessary.
- Task 3 generalizes a Phase-0 slice that R and Python already bind. Preserve the cross-language `24.425549382855987` proof (or consciously update it with a documented reason). This is the un-deferral of Phase-1 D1's UPD-deterministic-path note.
- The distributions (Phase 1) and the D1 converter are prerequisites and are on `main` — reuse them via the factory, do not re-port.
- Keep every faithful upstream bug you find (like Phases 0/1); document it in the header and let the gate lock it.
