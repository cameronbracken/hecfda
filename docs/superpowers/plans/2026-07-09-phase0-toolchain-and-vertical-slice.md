# HEC-FDA Port — Phase 0: Toolchain & Vertical Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the entire hecfdar/hecfdapy toolchain end to end on one seeded vertical slice (RNG + one distribution + paired-data integration), passing an identical fixture in C++, R, and Python with a byte-identical seeded RNG stream, reproduced against real C# by a dotnet oracle gate, green on 3-platform CI.

**Architecture:** One canonical C++17 header core in `core/include/hecfda/`, wrapped by cpp11 (R) and pybind11 (Python), both vendoring the core + `fixtures/` via subtree symlinks that builds dereference. Oracle values live only in `fixtures/*.json`; three thin generic runners apply every assertion. No bulk porting begins until this phase is green.

**Tech Stack:** C++17 (header-only core, doctest tests), CMake, cpp11 + R (`R CMD build`/testthat), pybind11 + scikit-build-core + Python (pytest), .NET 10 (`dotnet`, dev-only oracle gate), GitHub Actions.

**Reference sources** (pinned upstream, cloned at `/tmp/hecfda` during planning; the plan cites verified line-level facts):
- `HEC.FDA.Model/compute/RandomProvider.cs`, `HEC.FDA.Model/interfaces/IProvideRandomNumbers.cs`
- `HEC.FDA.Statistics/Distributions/{IDistribution,ContinuousDistribution,Normal,SpecialFunctions}.cs`
- `HEC.FDA.Statistics/Mathematics.cs`
- `HEC.FDA.Model/paireddata/{PairedData,UncertainPairedData,CurveMetaData}.cs`
- Oracle test literals: `HEC.FDA.ModelTest/unittests/PairedDataShould.cs`

## Global Constraints

- **C++ standard: C++17 only.** `CXX_STD = CXX17` (R Makevars), `CMAKE_CXX_STANDARD 17` (Python), `SystemRequirements: C++17` (DESCRIPTION). No C++20.
- **Self-contained core:** no external C++ deps (no Eigen/Boost). Port HEC-FDA's own math/RNG.
- **Structural mirroring:** each ported C++ file opens with `// ported from: <upstream-path> @ <sha>`; class/method names and order mirror the C# source.
- **Portability:** never `M_PI` — use `hecfda::kPi`. Do not name a namespace alias `gamma` (clashes glibc `gamma()`) or `stat`. Pass `-Wall/-Wextra` only to non-MSVC compilers. No `-O3`/`-march=native`/LTO/`-Werror` in `Makevars`.
- **No hardcoded oracle values in test files.** Oracle values live ONLY in `fixtures/*.json`.
- **RNG parity:** the seeded .NET `Random` port must reproduce the reference stream for `seed=1234` exactly (verified values in Task 2). Every Monte Carlo fixture carries a seed.
- **C++ namespace:** `hecfda::` with sub-namespaces `hecfda::statistics`, `hecfda::model`, `hecfda::sampling`.
- **Commits:** SSH-signed automatically (global config), identity `Cam Bracken <cameron.bracken@pm.me>`, conventional-commit messages. Push only when asked.
- **ABI safety (R):** after a core class-layout change, rebuild R with `R CMD INSTALL --preclean hecfdar`.
- **cpp11:** after editing any `hecfdar/src/*.cpp`, re-run `Rscript -e 'cpp11::cpp_register("hecfdar")'`.

## File Structure

Created across Phase 0 (each responsibility isolated):

```
core/CMakeLists.txt                                   # dev-only: builds doctest suites
core/tests/doctest.h                                  # vendored single-header test framework
core/tests/json.hpp                                   # vendored nlohmann/json (test-only)
core/tests/check.hpp                                  # float compare helpers (abs/rel/vector)
core/tests/test_dotnet_random.cpp                     # RNG reference-stream test
core/tests/test_fixtures.cpp                          # generic JSON fixture runner
core/include/hecfda/constants.hpp                     # kPi and friends
core/include/hecfda/sampling/dotnet_random.hpp        # .NET seeded Random port
core/include/hecfda/model/compute/random_provider.hpp # IProvideRandomNumbers + RandomProvider
core/include/hecfda/statistics/special_functions.hpp  # regIncompleteGamma (+ deps)
core/include/hecfda/statistics/distributions/i_distribution.hpp
core/include/hecfda/statistics/distributions/continuous_distribution.hpp
core/include/hecfda/statistics/distributions/normal.hpp
core/include/hecfda/statistics/mathematics.hpp        # trapezoidal / CDF integration
core/include/hecfda/model/paired_data/paired_data.hpp
core/include/hecfda/model/paired_data/uncertain_paired_data.hpp
fixtures/README.md                                    # fixture schema
fixtures/sampling/dotnet_random.json
fixtures/distributions/normal.json
fixtures/paired_data/paired_data.json
fixtures/paired_data/uncertain_paired_data.json       # the vertical-slice MC fixture
hecfdar/{DESCRIPTION,NAMESPACE,LICENSE,R/,src/,tests/,inst/}
hecfdapy/{pyproject.toml,CMakeLists.txt,src/,tests/}
tools/{materialize_core.py,verify_oracles.py,oracle_emitter/}
Makefile, README.md, .github/workflows/ci.yml
.claude/{CLAUDE.md, PLAN.md}
```

Interfaces flow up the dependency chain: `dotnet_random` → `random_provider` → (`special_functions` → `normal`) and (`mathematics` → `paired_data` → `uncertain_paired_data`). The fixture runner consumes all leaf types via a small dispatch table.

---

### Task 1: Core CMake + doctest harness (bootstrap the C++ build)

**Files:**
- Create: `core/CMakeLists.txt`, `core/tests/doctest.h` (vendored), `core/tests/check.hpp`, `core/tests/test_smoke.cpp`
- Create: `core/include/hecfda/constants.hpp`

**Interfaces:**
- Produces: `hecfda::kPi` (`constexpr double`); a working `ctest` target so every later task can add a `.cpp` test.

- [ ] **Step 1: Vendor doctest**

Download the single header (pinned):
```bash
mkdir -p core/tests core/include/hecfda
curl -fsSL https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h -o core/tests/doctest.h
```
Expected: `core/tests/doctest.h` ~7000 lines present.

- [ ] **Step 2: Write `core/include/hecfda/constants.hpp`**

```cpp
// hecfda numeric constants (portable replacement for M_PI et al.)
#ifndef HECFDA_CONSTANTS_HPP
#define HECFDA_CONSTANTS_HPP
namespace hecfda {
inline constexpr double kPi = 3.14159265358979323846;
}  // namespace hecfda
#endif  // HECFDA_CONSTANTS_HPP
```

- [ ] **Step 3: Write `core/tests/check.hpp`** (shared float comparison helpers)

```cpp
#ifndef HECFDA_TESTS_CHECK_HPP
#define HECFDA_TESTS_CHECK_HPP
#include <cmath>
#include <cstddef>
#include <vector>
namespace hecfda_test {
inline bool close_abs(double a, double b, double tol) {
    if (std::isnan(a) && std::isnan(b)) return true;
    return std::fabs(a - b) <= tol;
}
inline bool close_rel(double a, double b, double tol) {
    if (std::isnan(a) && std::isnan(b)) return true;
    double denom = std::fabs(b);
    if (denom == 0.0) return std::fabs(a) <= tol;
    return std::fabs(a - b) / denom <= tol;
}
inline bool close_vec(const std::vector<double>& a, const std::vector<double>& b, double tol) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!close_abs(a[i], b[i], tol)) return false;
    return true;
}
}  // namespace hecfda_test
#endif
```

- [ ] **Step 4: Write `core/tests/test_smoke.cpp`**

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "hecfda/constants.hpp"
#include "check.hpp"

TEST_CASE("kPi is defined to double precision") {
    CHECK(hecfda_test::close_abs(hecfda::kPi, 3.141592653589793, 1e-15));
}
```

- [ ] **Step 5: Write `core/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(hecfda_core CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

add_library(hecfda_core INTERFACE)
target_include_directories(hecfda_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# every test is a doctest translation unit; DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
# lives in test_smoke.cpp only, others use a shared main via object below.
file(GLOB HECFDA_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
foreach(test_src ${HECFDA_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE hecfda_core)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    if(NOT MSVC)
        target_compile_options(${test_name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
```

Note: each `test_*.cpp` must define `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` itself (one main per executable), since each is its own executable.

- [ ] **Step 6: Build and run**

Run: `cmake -S core -B core/build && cmake --build core/build && ctest --test-dir core/build --output-on-failure`
Expected: `test_smoke` PASS, `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 7: Commit**

```bash
git add core/CMakeLists.txt core/tests/ core/include/hecfda/constants.hpp
git commit -m "build: bootstrap C++17 core with doctest harness"
```

---

### Task 2: Port .NET seeded Random + reference-stream test

The parity linchpin. `HEC.FDA.Model/compute/RandomProvider` wraps `new Random(seed).NextDouble()`. In .NET 6+ the seeded ctor uses the legacy Knuth subtractive generator; the port below was verified during planning to reproduce `new Random(1234).NextDouble()` bit-for-bit.

**Files:**
- Create: `core/include/hecfda/sampling/dotnet_random.hpp`
- Test: `core/tests/test_dotnet_random.cpp`

**Interfaces:**
- Produces: `hecfda::sampling::DotNetRandom` with `explicit DotNetRandom(int32_t seed)`, `double next_double()`, `int32_t internal_sample()`.

- [ ] **Step 1: Write the failing test `core/tests/test_dotnet_random.cpp`**

The 10 expected values are the real `new Random(1234).NextDouble()` outputs captured from .NET 10 during planning.

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "hecfda/sampling/dotnet_random.hpp"
#include "check.hpp"

TEST_CASE("DotNetRandom(1234) reproduces the .NET seeded stream") {
    hecfda::sampling::DotNetRandom r(1234);
    const double expected[10] = {
        0.39908097935797693, 0.8958994657247791,  0.3192029387313886,
        0.9467375338760845,  0.33943602458547617, 0.9487782409176129,
        0.8079918901473246,  0.5207309469211525,  0.643958064096029,
        0.31255894820790686};
    for (int i = 0; i < 10; ++i)
        CHECK(hecfda_test::close_abs(r.next_double(), expected[i], 0.0));  // exact
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build core/build && ctest --test-dir core/build -R test_dotnet_random`
Expected: build FAILS ("dotnet_random.hpp: No such file").

- [ ] **Step 3: Write `core/include/hecfda/sampling/dotnet_random.hpp`**

```cpp
// ported from: .NET seeded System.Random (Knuth subtractive, CompatPrng),
// as used by HEC.FDA.Model/compute/RandomProvider.cs @ <sha>
#ifndef HECFDA_SAMPLING_DOTNET_RANDOM_HPP
#define HECFDA_SAMPLING_DOTNET_RANDOM_HPP
#include <cstdint>
#include <cstdlib>
namespace hecfda {
namespace sampling {
class DotNetRandom {
   public:
    explicit DotNetRandom(std::int32_t seed) {
        std::int32_t subtraction = (seed == INT32_MIN) ? INT32_MAX : std::abs(seed);
        std::int32_t mj = kMSeed - subtraction;
        seed_array_[55] = mj;
        std::int32_t mk = 1;
        for (int i = 1; i < 55; ++i) {
            int ii = (21 * i) % 55;
            seed_array_[ii] = mk;
            mk = mj - mk;
            if (mk < 0) mk += kMBig;
            mj = seed_array_[ii];
        }
        for (int k = 1; k < 5; ++k) {
            for (int i = 1; i < 56; ++i) {
                seed_array_[i] -= seed_array_[1 + (i + 30) % 55];
                if (seed_array_[i] < 0) seed_array_[i] += kMBig;
            }
        }
        inext_ = 0;
        inextp_ = 21;
    }
    std::int32_t internal_sample() {
        int loc_inext = inext_;
        int loc_inextp = inextp_;
        if (++loc_inext >= 56) loc_inext = 1;
        if (++loc_inextp >= 56) loc_inextp = 1;
        std::int32_t ret = seed_array_[loc_inext] - seed_array_[loc_inextp];
        if (ret == kMBig) --ret;
        if (ret < 0) ret += kMBig;
        seed_array_[loc_inext] = ret;
        inext_ = loc_inext;
        inextp_ = loc_inextp;
        return ret;
    }
    double next_double() { return internal_sample() * (1.0 / kMBig); }

   private:
    static constexpr std::int32_t kMBig = 2147483647;
    static constexpr std::int32_t kMSeed = 161803398;
    std::int32_t seed_array_[56] = {};
    int inext_ = 0;
    int inextp_ = 0;
};
}  // namespace sampling
}  // namespace hecfda
#endif
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build core/build && ctest --test-dir core/build -R test_dotnet_random --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/include/hecfda/sampling/dotnet_random.hpp core/tests/test_dotnet_random.cpp
git commit -m "feat(rng): port .NET seeded Random with reference-stream test"
```

---

### Task 3: RandomProvider + fixture schema + C++ generic fixture runner

Wrap `DotNetRandom` in the HEC-FDA `RandomProvider` API and stand up the JSON fixture runner that all three languages share. The first fixture is an RNG digest proving the stream.

**Files:**
- Create: `core/include/hecfda/model/compute/random_provider.hpp`
- Create: `core/tests/json.hpp` (vendored nlohmann/json, test-only), `core/tests/test_fixtures.cpp`
- Create: `fixtures/README.md`, `fixtures/sampling/dotnet_random.json`

**Interfaces:**
- Produces: `hecfda::model::compute::RandomProvider` with `RandomProvider(int seed)`, `double next_random()`, `std::vector<double> next_random_sequence(long size)`, `int seed() const`.
- Produces: fixture JSON schema (below) and a C++ runner dispatching on `target`.

- [ ] **Step 1: Write `core/include/hecfda/model/compute/random_provider.hpp`**

```cpp
// ported from: HEC.FDA.Model/compute/RandomProvider.cs @ <sha>
#ifndef HECFDA_MODEL_COMPUTE_RANDOM_PROVIDER_HPP
#define HECFDA_MODEL_COMPUTE_RANDOM_PROVIDER_HPP
#include <vector>
#include "hecfda/sampling/dotnet_random.hpp"
namespace hecfda {
namespace model {
namespace compute {
// mirrors IProvideRandomNumbers (NextRandom / NextRandomSequence / Seed)
class RandomProvider {
   public:
    explicit RandomProvider(int seed) : seed_(seed), rng_(seed) {}
    double next_random() { return rng_.next_double(); }
    std::vector<double> next_random_sequence(long size) {
        std::vector<double> out(static_cast<std::size_t>(size));
        for (long i = 0; i < size; ++i) out[static_cast<std::size_t>(i)] = rng_.next_double();
        return out;
    }
    int seed() const { return seed_; }

   private:
    int seed_;
    hecfda::sampling::DotNetRandom rng_;
};
}  // namespace compute
}  // namespace model
}  // namespace hecfda
#endif
```

- [ ] **Step 2: Vendor nlohmann/json (test-only)**

```bash
curl -fsSL https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp -o core/tests/json.hpp
```

- [ ] **Step 3: Write `fixtures/README.md`** (the schema, single source of truth)

````markdown
# Fixture schema

Each file: one JSON object.

```json
{
  "target": "dotnet_random",
  "kind": "rng | distribution | paired_data | mc_paired_data",
  "source_test": "upstream test path or 'planning-captured'",
  "cases": [
    {
      "name": "seed_1234",
      "construct": { "seed": 1234 },
      "assertions": [
        { "method": "next_double_sequence", "args": [10],
          "expected": [0.399, ...], "tol": 0.0, "mode": "vector" }
      ]
    }
  ]
}
```

Comparison `mode`: `abs | rel | exact | bool | vector | matrix`.
`exact` uses `tol == 0.0` (also matches NaN==NaN). Every `kind: mc_*` case MUST carry a seed.
Method/target strings map to each language's API via a small per-runner dispatch table.
No oracle values live outside these files.
````

- [ ] **Step 4: Write `fixtures/sampling/dotnet_random.json`**

```json
{
  "target": "dotnet_random",
  "kind": "rng",
  "source_test": "planning-captured: new Random(1234).NextDouble()",
  "cases": [
    {
      "name": "seed_1234_first_10",
      "construct": { "seed": 1234 },
      "assertions": [
        { "method": "next_random_sequence", "args": [10],
          "expected": [0.39908097935797693, 0.8958994657247791, 0.3192029387313886,
                       0.9467375338760845, 0.33943602458547617, 0.9487782409176129,
                       0.8079918901473246, 0.5207309469211525, 0.643958064096029,
                       0.31255894820790686],
          "tol": 0.0, "mode": "vector" }
      ]
    }
  ]
}
```

- [ ] **Step 5: Write the failing runner `core/tests/test_fixtures.cpp`**

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <fstream>
#include <string>
#include <vector>
#include "doctest.h"
#include "json.hpp"
#include "check.hpp"
#include "hecfda/model/compute/random_provider.hpp"

using json = nlohmann::json;

// Resolve the fixtures dir: env override or the in-repo default.
static std::string fixtures_dir() {
    if (const char* e = std::getenv("HECFDA_FIXTURES")) return e;
    return "../../fixtures";  // relative to core/build/<cfg>
}

static std::vector<double> run_rng(const json& c, const std::string& method, const json& args) {
    hecfda::model::compute::RandomProvider rp(c["construct"]["seed"].get<int>());
    if (method == "next_random_sequence") return rp.next_random_sequence(args[0].get<long>());
    return {};
}

TEST_CASE("dotnet_random fixture") {
    std::ifstream f(fixtures_dir() + "/sampling/dotnet_random.json");
    REQUIRE(f.good());
    json fx; f >> fx;
    CHECK(fx["target"] == "dotnet_random");
    for (const auto& c : fx["cases"]) {
        for (const auto& a : c["assertions"]) {
            auto got = run_rng(c, a["method"], a["args"]);
            std::vector<double> exp = a["expected"].get<std::vector<double>>();
            CHECK(hecfda_test::close_vec(got, exp, a["tol"].get<double>()));
        }
    }
}
```

- [ ] **Step 6: Run to verify build then pass**

Run: `cmake -S core -B core/build && cmake --build core/build && ctest --test-dir core/build -R test_fixtures --output-on-failure`
Expected: PASS (the runner reads the fixture and reproduces the stream).

- [ ] **Step 7: Commit**

```bash
git add core/include/hecfda/model/compute/random_provider.hpp core/tests/json.hpp core/tests/test_fixtures.cpp fixtures/
git commit -m "feat: RandomProvider + JSON fixture schema and C++ runner"
```

---

### Task 4: SpecialFunctions + Normal distribution

Port `regIncompleteGamma` (Normal.CDF dependency) and the `Normal` distribution (`PDF`/`CDF`/`InverseCDF`/`Fit`/`Sample`). `InverseCDF` uses the Abramowitz-Stegun rational approximation exactly as in `Normal.cs`.

**Files:**
- Create: `core/include/hecfda/statistics/special_functions.hpp`
- Create: `core/include/hecfda/statistics/distributions/i_distribution.hpp`, `continuous_distribution.hpp`, `normal.hpp`
- Create: `fixtures/distributions/normal.json`
- Modify: `core/tests/test_fixtures.cpp` (add distribution dispatch + a `distributions/normal.json` case block)

**Interfaces:**
- Produces: `hecfda::statistics::SpecialFunctions::reg_incomplete_gamma(double a, double x)`.
- Produces: `hecfda::statistics::distributions::Normal` with ctor `Normal(double mean, double sd, long sample_size = 1)`, methods `pdf(double)`, `cdf(double)`, `inverse_cdf(double)`, `Normal fit(const std::vector<double>&)`, `std::vector<double> sample(const std::vector<double>& random_packet)`.

- [ ] **Step 1: Write the failing distribution fixture `fixtures/distributions/normal.json`**

Oracle values are `Normal(0,1)` standard-normal inverse-CDF points from the A-S formula (compute via the ported formula; these are curated during the task and re-pinned by the dotnet gate in Task 10). Start with `InverseCDF` symmetry and known midpoints:

```json
{
  "target": "normal",
  "kind": "distribution",
  "source_test": "HEC.FDA.Statistics/Distributions/Normal.cs",
  "cases": [
    {
      "name": "standard_normal",
      "construct": { "mean": 0.0, "sd": 1.0, "sample_size": 1 },
      "assertions": [
        { "method": "inverse_cdf", "args": [0.5], "expected": 0.0, "tol": 0.0, "mode": "abs" },
        { "method": "pdf", "args": [0.0], "expected": 0.3989422804014327, "tol": 1e-12, "mode": "rel" },
        { "method": "cdf", "args": [0.0], "expected": 0.5, "tol": 1e-9, "mode": "abs" }
      ]
    }
  ]
}
```

Note: `inverse_cdf(0.5)` returns `Mean` exactly per the C# early return. `pdf(0)` for N(0,1) = `1/sqrt(2*pi)`. Additional inverse-CDF points are added and pinned by the dotnet gate (Task 10); do not hand-invent tail values.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build core/build && ctest --test-dir core/build -R test_fixtures`
Expected: FAIL (no `normal` dispatch / missing header).

- [ ] **Step 3: Write `core/include/hecfda/statistics/special_functions.hpp`**

Port `regIncompleteGamma` from `HEC.FDA.Statistics/Distributions/SpecialFunctions.cs` (transcribe the series/continued-fraction and `logGamma` helpers verbatim; open with the provenance header). The implementer copies the C# method bodies method-for-method into:

```cpp
// ported from: HEC.FDA.Statistics/Distributions/SpecialFunctions.cs @ <sha>
#ifndef HECFDA_STATISTICS_SPECIAL_FUNCTIONS_HPP
#define HECFDA_STATISTICS_SPECIAL_FUNCTIONS_HPP
#include <cmath>
namespace hecfda { namespace statistics {
struct SpecialFunctions {
    static double log_gamma(double x);            // transcribe from SpecialFunctions.cs
    static double reg_incomplete_gamma(double a, double x);  // transcribe (series + CF)
};
}}  // namespace
#endif
```

Definition of done for this step: the ported bodies match the C# line-for-line; `reg_incomplete_gamma(0.5, 0.0) == 0.0` and `reg_incomplete_gamma(0.5, x)` grows monotonically (spot-check in the test below).

- [ ] **Step 4: Write `i_distribution.hpp` and `continuous_distribution.hpp`**

```cpp
// ported from: HEC.FDA.Statistics/Distributions/IDistribution.cs @ <sha>
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_I_DISTRIBUTION_HPP
#include <vector>
namespace hecfda { namespace statistics { namespace distributions {
enum class DistributionType { Normal, LogNormal, Triangular, Uniform, Deterministic /* extended later */ };
class IDistribution {
   public:
    virtual ~IDistribution() = default;
    virtual DistributionType type() const = 0;
    virtual double pdf(double x) const = 0;
    virtual double cdf(double x) const = 0;
    virtual double inverse_cdf(double p) const = 0;
};
}}}  // namespace
#endif
```

`continuous_distribution.hpp` ports the `Sample(double[] packet)` template method (`samples[i] = inverse_cdf(packet[i])` for `i < sample_size`, then `fit(samples)`), mirroring `ContinuousDistribution.cs` lines 55-62. `sample_size` is a data member.

- [ ] **Step 5: Write `core/include/hecfda/statistics/distributions/normal.hpp`**

Transcribe `Normal.cs` `PDF`/`CDF`/`InverseCDF`/`Fit` verbatim (the A-S constants c0..d3 exactly). Header:

```cpp
// ported from: HEC.FDA.Statistics/Distributions/Normal.cs @ <sha>
#ifndef HECFDA_STATISTICS_DISTRIBUTIONS_NORMAL_HPP
#define HECFDA_STATISTICS_DISTRIBUTIONS_NORMAL_HPP
#include <cmath>
#include <vector>
#include "hecfda/constants.hpp"
#include "hecfda/statistics/special_functions.hpp"
#include "hecfda/statistics/distributions/continuous_distribution.hpp"
namespace hecfda { namespace statistics { namespace distributions {
class Normal : public ContinuousDistribution {
   public:
    Normal(double mean, double sd, long sample_size = 1)
        : mean_(mean), sd_(sd) { this->sample_size_ = sample_size; }
    DistributionType type() const override { return DistributionType::Normal; }
    double mean() const { return mean_; }
    double standard_deviation() const { return sd_; }
    double pdf(double x) const override {
        if (sd_ == 0) return x == mean_ ? 1.0 : 0.0;
        return std::exp(-(x - mean_) * (x - mean_) / (2.0 * sd_ * sd_)) /
               (std::sqrt(2.0 * hecfda::kPi) * sd_);
    }
    double cdf(double x) const override {
        if (sd_ == 0) return x >= mean_ ? 1.0 : 0.0;
        if (std::isinf(x)) return x > 0 ? 1.0 : 0.0;
        double g = SpecialFunctions::reg_incomplete_gamma(
            0.5, (x - mean_) * (x - mean_) / (2.0 * sd_ * sd_));
        return x >= mean_ ? 0.5 * (1.0 + g) : 0.5 * (1.0 - g);
    }
    double inverse_cdf(double p) const override {
        const double c0 = 2.515517, c1 = .802853, c2 = .010328;
        const double d1 = 1.432788, d2 = .189269, d3 = .001308;
        double q = p;
        if (q == .5) return mean_;
        if (q <= 0) q = .000000000000001;
        if (q >= 1) q = .999999999999999;
        int i;
        if (q < .5) { i = -1; } else { i = 1; q = 1 - q; }
        double t = std::sqrt(std::log(1 / (q * q)));
        double t2 = t * t, t3 = t2 * t;
        double x = t - (c0 + c1 * t + c2 * t2) / (1 + d1 * t + d2 * t2 + d3 * t3);
        x = i * x;
        return (x * sd_) + mean_;
    }
    Normal fit(const std::vector<double>& sample) const;  // SampleStatistics mean/sd (Task uses a thin inline)
   private:
    double mean_, sd_;
};
}}}  // namespace
#endif
```

`fit` computes sample mean/sd (population-style, matching `SampleStatistics`); a minimal inline is sufficient for Phase 0 (full `SampleStatistics` lands in Phase 1).

- [ ] **Step 6: Extend `core/tests/test_fixtures.cpp` with `normal` dispatch**

Add a `TEST_CASE("normal fixture")` that constructs `Normal(mean, sd, sample_size)` and dispatches `pdf`/`cdf`/`inverse_cdf` (scalar `args[0]`), comparing with the case's `mode` (`abs`/`rel`). Reuse `close_abs`/`close_rel`.

- [ ] **Step 7: Run to verify it passes**

Run: `cmake --build core/build && ctest --test-dir core/build -R test_fixtures --output-on-failure`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add core/include/hecfda/statistics/ core/tests/test_fixtures.cpp fixtures/distributions/normal.json
git commit -m "feat(stats): port SpecialFunctions + Normal distribution with fixtures"
```

---

### Task 5: Mathematics integration + PairedData

Port trapezoidal / CDF integration (`Mathematics.cs`) and `PairedData` (`f`, `f_inverse`, `Integrate`) from `HEC.FDA.Model/paireddata/PairedData.cs`. Oracle values are the real `PairedDataShould` interpolation literals.

**Files:**
- Create: `core/include/hecfda/statistics/mathematics.hpp`
- Create: `core/include/hecfda/model/paired_data/paired_data.hpp`
- Create: `fixtures/paired_data/paired_data.json`
- Modify: `core/tests/test_fixtures.cpp` (add `paired_data` dispatch)

**Interfaces:**
- Produces: `hecfda::statistics::Mathematics::integrate_cdf(xs, ys)`, `real_integrate_trapezoidal(xs, ys)`.
- Produces: `hecfda::model::paired_data::PairedData` with ctor `PairedData(std::vector<double> xs, std::vector<double> ys)`, `double f(double x) const`, `double f_inverse(double y) const`, `double integrate(bool with_padding = true) const`.

- [ ] **Step 1: Write the failing fixture `fixtures/paired_data/paired_data.json`**

Real oracle cases from `HEC.FDA.ModelTest/unittests/PairedDataShould.cs` (`ReturnYGivenX` on x={1..5}, y={2,4,6,8,10}; below-range clamps to min, above to max):

```json
{
  "target": "paired_data",
  "kind": "paired_data",
  "source_test": "HEC.FDA.ModelTest/unittests/PairedDataShould.cs",
  "cases": [
    {
      "name": "multiply_by_two_f",
      "construct": { "xs": [1,2,3,4,5], "ys": [2,4,6,8,10] },
      "assertions": [
        { "method": "f", "args": [3],   "expected": 1.5,  "tol": 0.0, "mode": "abs" },
        { "method": "f", "args": [10],  "expected": 6,    "tol": 0.0, "mode": "abs" },
        { "method": "f", "args": [2],   "expected": 0,    "tol": 0.0, "mode": "abs" },
        { "method": "f", "args": [8],   "expected": 4,    "tol": 0.0, "mode": "abs" },
        { "method": "f", "args": [7],   "expected": 3.5,  "tol": 0.0, "mode": "abs" },
        { "method": "f", "args": [3.5], "expected": 1.75, "tol": 0.0, "mode": "abs" }
      ]
    },
    {
      "name": "multiply_by_two_f_inverse",
      "construct": { "xs": [1,2,3,4,5], "ys": [2,4,6,8,10] },
      "assertions": [
        { "method": "f_inverse", "args": [3],   "expected": 1.5,  "tol": 0.0, "mode": "abs" },
        { "method": "f_inverse", "args": [10],  "expected": 5,    "tol": 0.0, "mode": "abs" },
        { "method": "f_inverse", "args": [2],   "expected": 1,    "tol": 0.0, "mode": "abs" },
        { "method": "f_inverse", "args": [8],   "expected": 4,    "tol": 0.0, "mode": "abs" },
        { "method": "f_inverse", "args": [3.5], "expected": 1.75, "tol": 0.0, "mode": "abs" }
      ]
    }
  ]
}
```

Note: in `ReturnYGivenX`, note the C# `f` case for `pairedMultiplyByTwo` (x={1..5}, y={2,4,6,8,10}) returns `x/2` behavior: `f(3)=1.5` because it interpolates y at x=3 against... verify against the exact `PairedData.f` semantics during the task; if the mapping direction differs, correct the expected values to the literal `Assert` in `PairedDataShould` (the literals above are copied from `ReturnYGivenX` `[InlineData(expected, sample)]` where `actual = pairedMultiplyByTwo.f(sample)`). The runner must reproduce the C# `Assert.Equal` exactly.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build core/build && ctest --test-dir core/build -R test_fixtures`
Expected: FAIL.

- [ ] **Step 3: Write `core/include/hecfda/statistics/mathematics.hpp`**

Transcribe `IntegrateCDF` (triangle padding from 0 to x1 plus trapezoidal) and `RealIntegrateTrapezoidal` from `Mathematics.cs` verbatim over `std::vector<double>` (drop the generic `IBinaryFloatingPointIeee754` constraint; use `double`).

- [ ] **Step 4: Write `core/include/hecfda/model/paired_data/paired_data.hpp`**

Transcribe `PairedData.f` (binary search + linear interpolation, clamping below index 0 to `y[0]` and at/after `len` to `y[len-1]`), `f_inverse` (symmetric), and `Integrate` (dispatch `integrate_cdf` when `with_padding`, else `real_integrate_trapezoidal`) from `PairedData.cs`. Provenance header required. Use `std::vector<double>` storage; port `Array.BinarySearch` semantics with `std::lower_bound` + exact-match handling (bitwise-compatible for these inputs).

- [ ] **Step 5: Extend `test_fixtures.cpp` with `paired_data` dispatch**

Construct `PairedData(xs, ys)`; dispatch `f`/`f_inverse`/`integrate` on scalar `args[0]` (or none for `integrate`).

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build core/build && ctest --test-dir core/build -R test_fixtures --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add core/include/hecfda/statistics/mathematics.hpp core/include/hecfda/model/paired_data/paired_data.hpp core/tests/test_fixtures.cpp fixtures/paired_data/paired_data.json
git commit -m "feat(model): port Mathematics integration + PairedData with fixtures"
```

---

### Task 6: UncertainPairedData seeded sample — the vertical slice

Combine RNG + Normal + PairedData: `UncertainPairedData` holds x values and a `Normal` uncertainty per y, samples a concrete `PairedData` from a seeded random packet, and integrates it. This is the end-to-end slice: seed in, deterministic integrated value out.

**Files:**
- Create: `core/include/hecfda/model/paired_data/uncertain_paired_data.hpp`
- Create: `fixtures/paired_data/uncertain_paired_data.json`
- Modify: `core/tests/test_fixtures.cpp` (add `mc_paired_data` dispatch)

**Interfaces:**
- Produces: `hecfda::model::paired_data::UncertainPairedData` with ctor `UncertainPairedData(std::vector<double> xs, std::vector<Normal> ys)`, `PairedData sample_paired_data(const std::vector<double>& random_packet) const` (each `y[i] = ys[i].inverse_cdf(random_packet[i])`), and `double sample_and_integrate(int seed) const` (draw `xs.size()` randoms from `RandomProvider(seed)`, build the `PairedData`, return `integrate()`).

- [ ] **Step 1: Write the failing MC fixture `fixtures/paired_data/uncertain_paired_data.json`**

The expected value is computed during the task from the ported code and then pinned by the dotnet gate (Task 10) against the real C# `UncertainPairedData` path. Structure:

```json
{
  "target": "uncertain_paired_data",
  "kind": "mc_paired_data",
  "source_test": "HEC.FDA.Model/paireddata/UncertainPairedData.cs (SamplePairedData)",
  "cases": [
    {
      "name": "seeded_sample_integrate",
      "construct": {
        "xs": [1, 2, 3, 4, 5],
        "ys": [ {"mean": 2, "sd": 0.5}, {"mean": 4, "sd": 0.5}, {"mean": 6, "sd": 0.5},
                {"mean": 8, "sd": 0.5}, {"mean": 10, "sd": 0.5} ]
      },
      "seed": 1234,
      "assertions": [
        { "method": "sample_and_integrate", "args": [1234],
          "expected": "PIN_IN_TASK", "tol": 1e-12, "mode": "rel" }
      ]
    }
  ]
}
```

During implementation, replace `"PIN_IN_TASK"` with the value the C++ code produces, then confirm the same value comes from the R and Python runners (Tasks 7-8) and from the dotnet gate (Task 10). If the dotnet value differs, the port is wrong — fix the port, not the fixture.

- [ ] **Step 2: Write `uncertain_paired_data.hpp`**

```cpp
// ported from: HEC.FDA.Model/paireddata/UncertainPairedData.cs @ <sha>
#ifndef HECFDA_MODEL_PAIRED_DATA_UNCERTAIN_PAIRED_DATA_HPP
#define HECFDA_MODEL_PAIRED_DATA_UNCERTAIN_PAIRED_DATA_HPP
#include <vector>
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
namespace hecfda { namespace model { namespace paired_data {
class UncertainPairedData {
   public:
    UncertainPairedData(std::vector<double> xs,
                        std::vector<hecfda::statistics::distributions::Normal> ys)
        : xs_(std::move(xs)), ys_(std::move(ys)) {}
    PairedData sample_paired_data(const std::vector<double>& packet) const {
        std::vector<double> y(ys_.size());
        for (std::size_t i = 0; i < ys_.size(); ++i) y[i] = ys_[i].inverse_cdf(packet[i]);
        return PairedData(xs_, y);
    }
    double sample_and_integrate(int seed) const {
        hecfda::model::compute::RandomProvider rp(seed);
        std::vector<double> packet = rp.next_random_sequence(static_cast<long>(xs_.size()));
        return sample_paired_data(packet).integrate();
    }
   private:
    std::vector<double> xs_;
    std::vector<hecfda::statistics::distributions::Normal> ys_;
};
}}}  // namespace
#endif
```

- [ ] **Step 3: Extend `test_fixtures.cpp` with `mc_paired_data` dispatch**

Parse `xs` and the `ys` array of `{mean, sd}` into `Normal(mean, sd, 1)`; construct `UncertainPairedData`; dispatch `sample_and_integrate(seed)`.

- [ ] **Step 4: Run, capture the produced value, pin the fixture**

Run: `cmake --build core/build && ctest --test-dir core/build -R test_fixtures --output-on-failure`
The first run FAILS on `PIN_IN_TASK`. Read the produced value from the failure message, write it into the fixture `expected`, re-run.
Expected after pinning: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/include/hecfda/model/paired_data/uncertain_paired_data.hpp core/tests/test_fixtures.cpp fixtures/paired_data/uncertain_paired_data.json
git commit -m "feat(model): UncertainPairedData seeded sample-and-integrate vertical slice"
```

---

### Task 7: R package (cpp11) — vendor core, glue, testthat runner

Stand up `hecfdar` as a self-contained cpp11 package that vendors the core via a subtree symlink and runs the same fixtures through a testthat runner.

**Files:**
- Create: `hecfdar/DESCRIPTION`, `hecfdar/NAMESPACE`, `hecfdar/LICENSE`
- Create: `hecfdar/src/hecfda_core -> ../../core` (symlink), `hecfdar/inst/fixtures -> ../../fixtures` (symlink)
- Create: `hecfdar/src/Makevars`, `hecfdar/src/Makevars.win`, `hecfdar/src/glue.cpp`
- Create: `hecfdar/R/glue.R`, `hecfdar/tests/testthat.R`, `hecfdar/tests/testthat/test-fixtures.R`

**Interfaces:**
- Produces: cpp11 free functions `hecfda_rng_sequence(seed, n)`, `hecfda_normal_eval(mean, sd, method, x)`, `hecfda_paired_f(xs, ys, method, x)`, `hecfda_upd_sample_integrate(xs, means, sds, seed)`, reached in tests via `asNamespace("hecfdar")`.

- [ ] **Step 1: Create the symlinks**

```bash
mkdir -p hecfdar/src hecfdar/inst hecfdar/R hecfdar/tests/testthat
ln -s ../../core hecfdar/src/hecfda_core
ln -s ../../fixtures hecfdar/inst/fixtures
```

- [ ] **Step 2: Write `hecfdar/DESCRIPTION`**

```
Package: hecfdar
Title: Flood Damage Analysis Compute Engine (HEC-FDA Port)
Version: 0.0.0.9000
Authors@R: person("Cameron", "Bracken", email = "cameron.bracken@pm.me", role = c("aut","cre"))
Description: R interface to a C++17 port of the HEC-FDA flood damage analysis
    compute engine. Reproduces the upstream computations with seeded reproducibility
    shared with the Python package.
License: file LICENSE
Encoding: UTF-8
SystemRequirements: C++17
LinkingTo: cpp11
Suggests: testthat (>= 3.0.0), jsonlite
Config/testthat/edition: 3
```

- [ ] **Step 3: Write `hecfdar/src/Makevars` and `Makevars.win`**

`Makevars`:
```make
CXX_STD = CXX17
PKG_CPPFLAGS = -I./hecfda_core/include
```
`Makevars.win`: identical two lines.

- [ ] **Step 4: Write `hecfdar/src/glue.cpp`** (cpp11 bindings)

```cpp
#include <cpp11.hpp>
#include <vector>
#include <string>
#include "hecfda_core/include/hecfda/model/compute/random_provider.hpp"
#include "hecfda_core/include/hecfda/statistics/distributions/normal.hpp"
#include "hecfda_core/include/hecfda/model/paired_data/paired_data.hpp"
#include "hecfda_core/include/hecfda/model/paired_data/uncertain_paired_data.hpp"
namespace nd = hecfda::statistics::distributions;
namespace pd = hecfda::model::paired_data;

[[cpp11::register]] cpp11::doubles hecfda_rng_sequence(int seed, int n) {
    hecfda::model::compute::RandomProvider rp(seed);
    auto v = rp.next_random_sequence(n);
    return cpp11::doubles(cpp11::writable::doubles(v.begin(), v.end()));
}
[[cpp11::register]] double hecfda_normal_eval(double mean, double sd, std::string method, double x) {
    nd::Normal dist(mean, sd, 1);
    if (method == "pdf") return dist.pdf(x);
    if (method == "cdf") return dist.cdf(x);
    return dist.inverse_cdf(x);
}
[[cpp11::register]] double hecfda_paired_f(cpp11::doubles xs, cpp11::doubles ys, std::string method, double x) {
    pd::PairedData p(std::vector<double>(xs.begin(), xs.end()), std::vector<double>(ys.begin(), ys.end()));
    if (method == "f_inverse") return p.f_inverse(x);
    if (method == "integrate") return p.integrate();
    return p.f(x);
}
[[cpp11::register]] double hecfda_upd_sample_integrate(cpp11::doubles xs, cpp11::doubles means, cpp11::doubles sds, int seed) {
    std::vector<nd::Normal> ys;
    for (R_xlen_t i = 0; i < means.size(); ++i) ys.emplace_back(means[i], sds[i], 1);
    pd::UncertainPairedData upd(std::vector<double>(xs.begin(), xs.end()), ys);
    return upd.sample_and_integrate(seed);
}
```

- [ ] **Step 5: Write `hecfdar/R/glue.R`, `NAMESPACE`, `tests/testthat.R`**

`R/glue.R`: `#' @useDynLib hecfdar, .registration = TRUE` plus a roxygen stub; NAMESPACE: `useDynLib(hecfdar, .registration = TRUE)`. `tests/testthat.R`: `library(testthat); library(hecfdar); test_check("hecfdar")`.

- [ ] **Step 6: Write `hecfdar/tests/testthat/test-fixtures.R`** (the shared runner in R)

```r
fx_dir <- system.file("fixtures", package = "hecfdar")
ns <- asNamespace("hecfdar")

read_fx <- function(p) jsonlite::fromJSON(file.path(fx_dir, p), simplifyVector = FALSE)

cmp <- function(got, exp, tol, mode) {
  if (mode == "vector") return(expect_equal(unlist(got), unlist(exp), tolerance = max(tol, 1e-15)))
  if (mode == "rel")    return(expect_equal(got, exp, tolerance = ifelse(tol == 0, 1e-15, tol)))
  expect_equal(got, exp, tolerance = tol)  # abs / exact (tol 0)
}

test_that("dotnet_random fixture", {
  fx <- read_fx("sampling/dotnet_random.json")
  for (c in fx$cases) for (a in c$assertions) {
    got <- ns$hecfda_rng_sequence(c$construct$seed, a$args[[1]])
    cmp(got, a$expected, a$tol, a$mode)
  }
})

test_that("normal fixture", {
  fx <- read_fx("distributions/normal.json")
  for (c in fx$cases) for (a in c$assertions) {
    got <- ns$hecfda_normal_eval(c$construct$mean, c$construct$sd, a$method, a$args[[1]])
    cmp(got, a$expected, a$tol, a$mode)
  }
})

test_that("paired_data fixture", {
  fx <- read_fx("paired_data/paired_data.json")
  for (c in fx$cases) for (a in c$assertions) {
    got <- ns$hecfda_paired_f(unlist(c$construct$xs), unlist(c$construct$ys), a$method, a$args[[1]])
    cmp(got, a$expected, a$tol, a$mode)
  }
})

test_that("uncertain_paired_data fixture", {
  fx <- read_fx("paired_data/uncertain_paired_data.json")
  for (c in fx$cases) for (a in c$assertions) {
    means <- sapply(c$construct$ys, `[[`, "mean"); sds <- sapply(c$construct$ys, `[[`, "sd")
    got <- ns$hecfda_upd_sample_integrate(unlist(c$construct$xs), means, sds, c$seed)
    cmp(got, a$expected, a$tol, a$mode)
  }
})
```

- [ ] **Step 7: Register, install, test**

Run:
```bash
Rscript -e 'cpp11::cpp_register("hecfdar")'
R CMD INSTALL --preclean hecfdar
Rscript -e 'testthat::test_local("hecfdar")'
```
Expected: all fixtures pass (`[ FAIL 0 ...]`). The `uncertain_paired_data` value matches the C++ pin exactly.

- [ ] **Step 8: Commit**

```bash
git add hecfdar/
git commit -m "feat(r): hecfdar cpp11 package vendoring core, testthat fixture runner"
```

---

### Task 8: Python package (pybind11 + scikit-build-core) — bindings, pytest runner

Mirror Task 7 in Python. Same core, same fixtures, same values.

**Files:**
- Create: `hecfdapy/pyproject.toml`, `hecfdapy/CMakeLists.txt`
- Create: `hecfdapy/src/hecfda_core -> ../../core` (symlink), `hecfdapy/src/fixtures -> ../../fixtures` (symlink for tests)
- Create: `hecfdapy/src/bindings/glue.cpp`, `hecfdapy/src/hecfdapy/__init__.py`
- Create: `hecfdapy/tests/test_fixtures.py`

**Interfaces:**
- Produces: `hecfdapy._core` extension exposing `rng_sequence(seed, n) -> list[float]`, `normal_eval(mean, sd, method, x) -> float`, `paired_f(xs, ys, method, x) -> float`, `upd_sample_integrate(xs, means, sds, seed) -> float`.

- [ ] **Step 1: Symlinks + package skeleton**

```bash
mkdir -p hecfdapy/src/bindings hecfdapy/src/hecfdapy hecfdapy/tests
ln -s ../../core hecfdapy/src/hecfda_core
ln -s ../../../fixtures hecfdapy/tests/fixtures
printf 'from ._core import rng_sequence, normal_eval, paired_f, upd_sample_integrate\n' > hecfdapy/src/hecfdapy/__init__.py
```

- [ ] **Step 2: Write `hecfdapy/pyproject.toml`**

```toml
[build-system]
requires = ["scikit-build-core>=0.9", "pybind11>=2.12"]
build-backend = "scikit_build_core.build"

[project]
name = "hecfdapy"
version = "0.0.0"
description = "HEC-FDA flood damage analysis compute engine (C++ port)"
requires-python = ">=3.10"
authors = [{ name = "Cameron Bracken", email = "cameron.bracken@pm.me" }]

[tool.scikit-build]
wheel.packages = ["src/hecfdapy"]

[tool.pytest.ini_options]
testpaths = ["tests"]
```

- [ ] **Step 3: Write `hecfdapy/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(hecfdapy CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(pybind11 CONFIG REQUIRED)
pybind11_add_module(_core src/bindings/glue.cpp)
target_include_directories(_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/hecfda_core/include)
install(TARGETS _core DESTINATION hecfdapy)
```

- [ ] **Step 4: Write `hecfdapy/src/bindings/glue.cpp`**

```cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>
#include "hecfda/model/compute/random_provider.hpp"
#include "hecfda/statistics/distributions/normal.hpp"
#include "hecfda/model/paired_data/paired_data.hpp"
#include "hecfda/model/paired_data/uncertain_paired_data.hpp"
namespace py = pybind11;
namespace nd = hecfda::statistics::distributions;
namespace pd = hecfda::model::paired_data;

static std::vector<double> rng_sequence(int seed, int n) {
    hecfda::model::compute::RandomProvider rp(seed);
    return rp.next_random_sequence(n);
}
static double normal_eval(double mean, double sd, const std::string& m, double x) {
    nd::Normal d(mean, sd, 1);
    if (m == "pdf") return d.pdf(x);
    if (m == "cdf") return d.cdf(x);
    return d.inverse_cdf(x);
}
static double paired_f(std::vector<double> xs, std::vector<double> ys, const std::string& m, double x) {
    pd::PairedData p(std::move(xs), std::move(ys));
    if (m == "f_inverse") return p.f_inverse(x);
    if (m == "integrate") return p.integrate();
    return p.f(x);
}
static double upd_sample_integrate(std::vector<double> xs, std::vector<double> means,
                                   std::vector<double> sds, int seed) {
    std::vector<nd::Normal> ys;
    for (size_t i = 0; i < means.size(); ++i) ys.emplace_back(means[i], sds[i], 1);
    pd::UncertainPairedData upd(std::move(xs), ys);
    return upd.sample_and_integrate(seed);
}
PYBIND11_MODULE(_core, mod) {
    mod.def("rng_sequence", &rng_sequence);
    mod.def("normal_eval", &normal_eval);
    mod.def("paired_f", &paired_f);
    mod.def("upd_sample_integrate", &upd_sample_integrate);
}
```

- [ ] **Step 5: Write `hecfdapy/tests/test_fixtures.py`** (shared runner in Python)

```python
import json, math, pathlib
import hecfdapy as bf

FX = pathlib.Path(__file__).parent / "fixtures"

def _read(p): return json.loads((FX / p).read_text())

def _close(got, exp, tol, mode):
    if mode == "vector":
        assert len(got) == len(exp)
        for g, e in zip(got, exp): assert abs(g - e) <= max(tol, 1e-15)
    elif mode == "rel":
        t = 1e-15 if tol == 0 else tol
        assert abs(got - exp) <= t * (abs(exp) if exp else 1.0)
    else:  # abs / exact
        assert abs(got - exp) <= tol

def test_dotnet_random():
    fx = _read("sampling/dotnet_random.json")
    for c in fx["cases"]:
        for a in c["assertions"]:
            got = bf.rng_sequence(c["construct"]["seed"], a["args"][0])
            _close(got, a["expected"], a["tol"], a["mode"])

def test_normal():
    fx = _read("distributions/normal.json")
    for c in fx["cases"]:
        for a in c["assertions"]:
            got = bf.normal_eval(c["construct"]["mean"], c["construct"]["sd"], a["method"], a["args"][0])
            _close(got, a["expected"], a["tol"], a["mode"])

def test_paired_data():
    fx = _read("paired_data/paired_data.json")
    for c in fx["cases"]:
        for a in c["assertions"]:
            got = bf.paired_f(c["construct"]["xs"], c["construct"]["ys"], a["method"], a["args"][0])
            _close(got, a["expected"], a["tol"], a["mode"])

def test_uncertain_paired_data():
    fx = _read("paired_data/uncertain_paired_data.json")
    for c in fx["cases"]:
        means = [y["mean"] for y in c["construct"]["ys"]]
        sds = [y["sd"] for y in c["construct"]["ys"]]
        for a in c["assertions"]:
            got = bf.upd_sample_integrate(c["construct"]["xs"], means, sds, c["seed"])
            _close(got, a["expected"], a["tol"], a["mode"])
```

- [ ] **Step 6: Build, install, test**

Run:
```bash
python3 -m pip install --force-reinstall --no-deps ./hecfdapy
python3 -m pytest hecfdapy/tests -q
```
Expected: 4 passed. The `uncertain_paired_data` value matches the C++ and R pins exactly.

- [ ] **Step 7: Commit**

```bash
git add hecfdapy/
git commit -m "feat(py): hecfdapy pybind11 package, pytest fixture runner"
```

---

### Task 9: Cross-language RNG identity + Python materialize (symlink-free build)

Prove R and Python produce a byte-identical seeded stream digest, and give Python a symlink-free build path (Windows/CI safe).

**Files:**
- Create: `tools/materialize_core.py`
- Create: `fixtures/sampling/rng_digest.json` (a long seeded sequence + its expected sum digest)
- Modify: `hecfdar/tests/testthat/test-fixtures.R`, `hecfdapy/tests/test_fixtures.py` (assert the digest)

**Interfaces:**
- Produces: `tools/materialize_core.py` that replaces the `hecfda_core`/`fixtures` symlinks with real copies in a working tree (for `python -m build`).

- [ ] **Step 1: Write `tools/materialize_core.py`**

Walk `hecfdar/src/hecfda_core`, `hecfdapy/src/hecfda_core`, and the fixture symlinks; if a path is a symlink, replace it with a real recursive copy of the target. Print each rewrite. (Small, ~30 lines, using `os.path.islink`, `shutil.copytree`.)

- [ ] **Step 2: Write `fixtures/sampling/rng_digest.json`**

A `seed`, a length `n` (e.g. 100000), and an expected `sum` of the sequence. Compute the sum in the C++ runner first, pin it, then the R/Python runners assert the same sum to `rel 1e-12`.

- [ ] **Step 3: Add the digest assertion to all three runners**

C++ `test_fixtures.cpp`, R `test-fixtures.R`, Python `test_fixtures.py`: sum `rng_sequence(seed, n)` and compare to the fixture `sum`. Same value across all three.

- [ ] **Step 4: Verify materialize produces a symlink-free tree**

Run:
```bash
git worktree add -q /tmp/hecfda-mat HEAD && cd /tmp/hecfda-mat && python3 tools/materialize_core.py
find hecfdapy/src/hecfda_core -type l | wc -l   # expect 0
cd - && git worktree remove --force /tmp/hecfda-mat
```
Expected: 0 symlinks after materialize.

- [ ] **Step 5: Commit**

```bash
git add tools/materialize_core.py fixtures/sampling/rng_digest.json core/tests/test_fixtures.cpp hecfdar/tests/testthat/test-fixtures.R hecfdapy/tests/test_fixtures.py
git commit -m "test: cross-language RNG digest + Python materialize path"
```

---

### Task 10: dotnet oracle gate (reproduce fixtures against real C#)

Prove every fixture value is what the real HEC-FDA C# produces, using the pinned upstream submodule.

**Files:**
- Create: `upstream/HEC-FDA` (git submodule, pinned), `.gitmodules`
- Create: `tools/oracle_emitter/` (a small C# console project referencing `HEC.FDA.Statistics` + `HEC.FDA.Model`)
- Create: `tools/verify_oracles.py`

**Interfaces:**
- Produces: `tools/verify_oracles.py` that runs the emitter and compares its output to every fixture, failing on any mismatch beyond tolerance.

- [ ] **Step 1: Add the pinned upstream submodule**

```bash
git submodule add https://github.com/HydrologicEngineeringCenter/HEC-FDA.git upstream/HEC-FDA
git -C upstream/HEC-FDA checkout main && git submodule update --init
```
(Record the pinned SHA; backfill the `@ <sha>` provenance headers in the ported files.)

- [ ] **Step 2: Write `tools/oracle_emitter/`** (C# console)

A `.csproj` targeting `net10.0` with `ProjectReference`s to `../../upstream/HEC-FDA/HEC.FDA.Statistics/HEC.FDA.Statistics.csproj` and `HEC.FDA.Model`. `Program.cs` reads each fixture JSON, reconstructs the case with the real C# types (`new Normal(mean, sd)`, `new PairedData(xs, ys)`, `new RandomProvider(seed)`, `new UncertainPairedData(xs, ys, meta)`), evaluates each `method`, and emits `{fixture, case, method, value}` JSON to stdout. For `uncertain_paired_data`, replicate `sample_and_integrate`: `new RandomProvider(seed).NextRandomSequence(n)`, `SamplePairedData`, `Integrate`.

- [ ] **Step 3: Write `tools/verify_oracles.py`**

Runs `dotnet run --project tools/oracle_emitter -c Release`, parses its emitted values, loads every `fixtures/**/*.json`, and asserts each fixture `expected` reproduces the emitter value within `tol`/`mode`. Prints `<N> reproduced, <M> failed`; exit nonzero on any failure.

- [ ] **Step 4: Run the gate**

Run: `python3 tools/verify_oracles.py`
Expected: `4 fixtures, all reproduced, 0 failed` (rng, normal, paired_data, uncertain_paired_data). If `uncertain_paired_data` fails, the C++ sample-and-integrate diverges from C# — fix the port.

- [ ] **Step 5: Commit**

```bash
git add .gitmodules upstream tools/oracle_emitter tools/verify_oracles.py
git commit -m "test: dotnet oracle gate reproducing Phase 0 fixtures against real C#"
```

---

### Task 11: Makefile + 3-platform CI

Wire the entry points and a GitHub Actions matrix that gates on core + R + Python across ubuntu/macOS/windows.

**Files:**
- Create: `Makefile`, `.github/workflows/ci.yml`

**Interfaces:** none (developer/CI ergonomics).

- [ ] **Step 1: Write `Makefile`**

```make
.PHONY: test-core test-r test-py materialize oracles
test-core:
	cmake -S core -B core/build && cmake --build core/build && ctest --test-dir core/build --output-on-failure
test-r:
	Rscript -e 'cpp11::cpp_register("hecfdar")'
	R CMD INSTALL --preclean hecfdar
	Rscript -e 'testthat::test_local("hecfdar")'
test-py:
	python3 -m pip install --force-reinstall --no-deps ./hecfdapy
	python3 -m pytest hecfdapy/tests -q
materialize:
	python3 tools/materialize_core.py
oracles:
	python3 tools/verify_oracles.py
```

- [ ] **Step 2: Write `.github/workflows/ci.yml`**

Three jobs, `actions/checkout` with `submodules: false` (the oracle gate and upstream submodule never run in CI):
- `core`: matrix ubuntu/macOS/windows → configure/build/ctest.
- `r-cmd-check`: matrix 3 OS → setup-r, install cpp11/testthat/jsonlite, `cpp11::cpp_register`, `R CMD INSTALL`, `testthat::test_local`.
- `python`: matrix 3 OS × {3.10, 3.12} → `pip install ./hecfdapy` (after `python tools/materialize_core.py` on Windows), `pytest`.

Provide the full YAML with those three jobs.

- [ ] **Step 3: Verify locally (proxy for CI)**

Run: `make test-core && make test-r && make test-py`
Expected: all green.

- [ ] **Step 4: Commit**

```bash
git add Makefile .github/workflows/ci.yml
git commit -m "ci: Makefile + 3-platform core/R/Python matrix"
```

---

### Task 12: Context files (CLAUDE.md, PLAN.md) + README

Land the tracked context files so the phased bulk port can begin, and document the exit-criterion proof.

**Files:**
- Create: `.claude/CLAUDE.md`, `.claude/PLAN.md`, `README.md`
- Modify: `.gitignore` (already un-ignores the two `.claude` files)

- [ ] **Step 1: Write `.claude/PLAN.md`** — copy the approved spec's phasing (Phase 0 done; Phases 1-6 pending) plus the mirror-port conventions and the F0/A2-style exemplar recipe pointer (adapt from bestfit's PLAN.md structure).

- [ ] **Step 2: Write `.claude/CLAUDE.md`** — the working context: layout, the vendoring-symlink invariant, build/test commands, the RNG-parity rule, portability gotchas, and Phase 0 status (all four fixtures green in C++/R/Python; dotnet gate reproduces; CI green).

- [ ] **Step 3: Write `README.md`** — what the packages are, the shared-core design, install-from-source, and a quick-start that mirrors bestfit's README shape.

- [ ] **Step 4: Confirm the exit criterion**

Run: `make test-core && make test-r && make test-py && make oracles`
Expected: C++ fixtures pass; R fixtures pass; Python fixtures pass; the `uncertain_paired_data` seeded value is identical across C++/R/Python; the dotnet gate reproduces all four fixtures. This is the Phase 0 gate.

- [ ] **Step 5: Commit**

```bash
git add .claude/CLAUDE.md .claude/PLAN.md README.md
git commit -m "docs: Phase 0 context files and README; toolchain proven end to end"
```

---

## Self-Review

**Spec coverage:** repo layout (Task 1,7,8), C++17 self-contained core (Task 1), structural mirroring + provenance headers (Tasks 2,4,5,6,10), RNG parity port + reference stream (Task 2, verified during planning), fixture schema + three thin runners (Tasks 3,7,8), the numerical-core slice RNG→Normal→PairedData→UncertainPairedData (Tasks 2-6), dotnet oracle gate (Task 10), symlink vendoring + materialize (Tasks 7,8,9), 3-platform CI with `submodules: false` (Task 11), context files (Task 12). The Phase-0 exit criterion (identical fixture across C++/R/Python, byte-identical seeded RNG, CI green, gate reproduces) is checked in Task 12 Step 4. Covered.

**Placeholder scan:** the only deferred value is the `uncertain_paired_data` `expected` ("PIN_IN_TASK"), which is intentionally computed-then-pinned within Task 6 Step 4 and cross-checked by Tasks 7, 8, and 10 — this is the correct TDD flow for a value with no pre-existing C# literal, not a plan gap. `SpecialFunctions` and `PairedData.f`/`f_inverse` bodies are "transcribe verbatim from the cited C# file" because reproducing ~100 lines of upstream here would be error-prone and violate DRY; the exact source path, method names, and behavior notes are given, matching the mirror-port convention.

**Type consistency:** `RandomProvider(int seed)` / `next_random_sequence(long)` consistent across Tasks 3,6,7,8,10; `Normal(mean, sd, sample_size=1)` and `inverse_cdf`/`pdf`/`cdf` consistent Tasks 4,6,7,8; `PairedData(xs, ys)` / `f`/`f_inverse`/`integrate` consistent Tasks 5,7,8; `UncertainPairedData(xs, ys)` / `sample_and_integrate(seed)` consistent Tasks 6,7,8,10. Glue names (`hecfda_*` in R, `_core` functions in Python) consistent within their runner tasks.

## Notes for the executor

- Two exemplar tasks anchor the recipe: **Task 2** (a foundation module with a verified reference stream) and **Task 4** (a distribution with the transcribe-verbatim convention). Later phases reference these.
- The single riskiest fact — the .NET seeded `Random` stream — was verified bit-for-bit against .NET 10 during planning (Task 2's expected values are real captures), so the RNG port carries no open risk into execution.
- If the dotnet gate (Task 10) is unavailable in a given environment, Tasks 1-9 and 11-12 still complete; the gate is dev-only and never runs in CI.
