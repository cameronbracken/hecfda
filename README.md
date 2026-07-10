# hecfda

R (`hecfdar`) and Python (`hecfdapy`) packages for flood-damage-reduction compute. Both wrap a
shared **C++17 core library** ported from the USACE Hydrologic Engineering Center's
[HEC-FDA](https://github.com/HydrologicEngineeringCenter/HEC-FDA) C# libraries
`HEC.FDA.Statistics` and the pure-numeric parts of `HEC.FDA.Model`. Both packages are designed to
return identical results with the same random seed.

## Development status

Early development. Phase 0 (toolchain + a vertical slice: seeded RNG -> distribution ->
paired-data integration) is complete and proven identical across C++, R, Python, and the real
HEC-FDA C#. The bulk port -- distributions, structures, stage-damage, the Monte Carlo EAD
simulation, scenarios and alternatives -- has not started.

Neither package is on CRAN or PyPI yet.

## What's ported

Scope is the **numerical core only**: `HEC.FDA.Statistics` (distributions, special functions,
sample statistics) and the pure-numeric parts of `HEC.FDA.Model` (paired-data curve algebra,
structure inventory, stage-damage, the Monte Carlo EAD simulation, metrics, scenarios,
alternatives). RAS-Mapper terrain/inundation reading, GIS, SQLite/DBF persistence, `LifeLoss`
(LifeSim), and all WPF/MVVM boilerplate are deliberately not ported -- users supply
already-extracted inputs (stage-frequency curves, hydraulic stage profiles, structure inventory
tables) as plain arrays or data frames.

## Install

Compiling from source requires a C++17 compiler (clang++/gcc/MSVC).

R:

```r
# install.packages("pak")
pak::pak("cameronbracken/hecfda/hecfdar")
```

Or from a local checkout:

```r
Rscript -e 'cpp11::cpp_register("hecfdar")'
R CMD INSTALL hecfdar
```

Python (3.10+):

```bash
pip install "git+https://github.com/cameronbracken/hecfda.git#subdirectory=hecfdapy"
```

Or from a local checkout:

```bash
pip install ./hecfdapy
```

## Quick start

The Phase 0 slice: seed a random provider, sample a paired-data curve with normally distributed
uncertainty at each point, integrate it. Both languages return the same number for the same seed.

R:

```r
library(hecfdar)

# random draws from the ported .NET-compatible seeded generator
hecfda_rng_sequence(seed = 1234L, n = 5L)

# evaluate a Normal(mean=0, sd=1) distribution
hecfda_normal_eval(mean = 0, sd = 1, method = "cdf", x = 0)   # 0.5

# sample-and-integrate an uncertain paired-data curve, seeded
xs <- c(1, 2, 3, 4, 5)
means <- c(2, 4, 6, 8, 10)
sds <- rep(0.5, 5)
hecfda_upd_sample_integrate(xs, means, sds, seed = 1234L)   # 24.425549382855987
```

Python:

```python
import hecfdapy as fda

fda.rng_sequence(seed=1234, n=5)

fda.normal_eval(mean=0, sd=1, method="cdf", x=0)   # 0.5

xs = [1, 2, 3, 4, 5]
means = [2, 4, 6, 8, 10]
sds = [0.5] * 5
fda.upd_sample_integrate(xs, means, sds, seed=1234)   # 24.425549382855987
```

## Reproducibility

Both packages port .NET's seeded `System.Random` algorithm exactly (the legacy Knuth subtractive
generator), so the same seed gives identical output across R, Python, and the real HEC-FDA C#:

```r
hecfda_upd_sample_integrate(xs, means, sds, seed = 1234L)   # R
```
```python
fda.upd_sample_integrate(xs, means, sds, seed=1234)  # Python -- same number
```

A dev-only oracle gate (`tools/verify_oracles.py`) checks this against the real upstream C# using
a pinned copy of the HEC-FDA source; it currently reproduces all 18 Phase 0 fixture assertions
with 0 failures.

## Layout

| Path | Purpose |
|------|---------|
| `core/` | C++17 core library (headers, tests) |
| `fixtures/` | language-neutral oracle fixtures (JSON) validating both packages |
| `hecfdar/` | R package (cpp11) |
| `hecfdapy/` | Python package (scikit-build-core + pybind11) |
| `tools/` | build and validation scripts, incl. the dotnet oracle gate |
| `upstream/HEC-FDA/` | pinned dev-only git submodule of the upstream C#, used for provenance and the oracle gate only |

`hecfdar` and `hecfdapy` share the same code from `core/` and `fixtures/` using symlinks, resolved
at build time. See `.claude/CLAUDE.md` and `.claude/PLAN.md` for the port architecture and
development workflow.

## Build from source

```bash
# C++ core
cmake -S core -B core/build && cmake --build core/build && ctest --test-dir core/build

# R package
Rscript -e 'cpp11::cpp_register("hecfdar")'   # only after editing hecfdar/src/*.cpp
R CMD INSTALL hecfdar
Rscript -e 'testthat::test_local("hecfdar")'

# Python package
pip install ./hecfdapy
pytest hecfdapy/tests
```

Or via the Makefile: `make test-core`, `make test-r`, `make test-py`.

## License

The C++ core and `hecfdar` are released under the 0BSD license (see `hecfdar/LICENSE`), compatible
with upstream HEC-FDA's MIT license. `hecfdapy` will carry the same license once packaged for
release.
