# hecfda

hecfda is an (unofficial) C++ port of the numerical core of USACE-HEC
[HEC-FDA](https://github.com/HydrologicEngineeringCenter/HEC-FDA), the Hydrologic Engineering
Center's flood damage and risk assessment tool. One shared C++17 core is bound into an R package
(`hecfdar`) and a Python package (`hecfdapy`), so the same seed produces the same numbers in R,
Python, and the upstream C#.

## What's ported

The full numerical pipeline: distributions, paired-data curves, structure inventories,
stage-damage compute, the seeded Monte Carlo expected annual damage (EAD) simulation, scenarios
across impact areas, alternatives, equivalent annual damage (EqAD) annualization, and
with/without-project benefits. See the [porting status page](https://cameronbracken.github.io/hecfda/status.html)
for the area-by-area detail.

Deliberately not ported: GIS and terrain reading, RAS-grid hydraulics ingest, SQLite/DBF
persistence, life-loss modeling, and the WPF/MVVM application layer. Users supply
already-extracted inputs, stage-frequency curves, hydraulic stage profiles, structure inventory
tables, as plain arrays or data frames; hecfda has no file I/O of its own.

## Documentation

- [Documentation site](https://cameronbracken.github.io/hecfda/) with worked examples in both
  languages, covering distributions, stage-damage, EAD simulation, and the alternatives capstone
- [Python API reference](https://cameronbracken.github.io/hecfda/reference/)
- [R API reference](https://cameronbracken.github.io/hecfda/r/)
- [Porting status](https://cameronbracken.github.io/hecfda/status.html)

## Development status

Version 0.1.0. All HEC-FDA numerical-core features are ported and validated against the real C#:
the dev-only oracle gate reproduces 820 assertions with 0 failures. Neither package is on CRAN or
PyPI yet.

## Install

Compiling from source requires a C++17 compiler (Xcode command-line tools on macOS, gcc/clang on
Linux, Rtools on Windows for R).

R:

```r
# install.packages("pak")
pak::pak("cameronbracken/hecfda/hecfdar")
```

Python (3.10+):

```bash
pip install "git+https://github.com/cameronbracken/hecfda.git#subdirectory=hecfdapy"
```

## Quick start

Both packages run the same seeded Monte Carlo engine, so the deterministic HEC-FDA oracle value
150000 comes back identically in R and Python.

Python:

```python
import hecfdapy as fda

res = fda.ead_simulation(
    stage_damage=[
        {
            "x": [0, 15, 30],
            "dist": "Uniform",
            "params": [[0, 0, 10], [0, 600000, 10], [0, 600000, 10]],
            "damage_category": "residential",
            "asset_category": "Structure",
        }
    ],
    flow_frequency={"dist": "Uniform", "params": [0, 100000, 1000]},
    flow_stage={
        "x": [0, 100000],
        "dist": "Uniform",
        "params": [[0, 0, 10], [0, 30, 10]],
    },
    min_iterations=1,
    max_iterations=1,
    deterministic=True,
)
res["total_ead"]  # 150000
```

R:

```r
library(hecfdar)

res = ead_simulation(
  stage_damage = list(list(
    x = c(0, 15, 30), dist = "Uniform",
    params = list(c(0, 0, 10), c(0, 600000, 10), c(0, 600000, 10)),
    damage_category = "residential", asset_category = "Structure"
  )),
  flow_frequency = list(dist = "Uniform", params = c(0, 100000, 1000)),
  flow_stage = list(
    x = c(0, 100000), dist = "Uniform",
    params = list(c(0, 0, 10), c(0, 30, 10))
  ),
  min_iterations = 1L, max_iterations = 1L, deterministic = TRUE
)
res$total_ead  # 150000
```

The two packages also agree on a plain random draw:

```r
rng_sequence(seed = 1234L, n = 5L)   # R
```
```python
fda.rng_sequence(seed=1234, n=5)  # Python -- same numbers
```

## Reproducibility

Both packages port .NET's seeded `System.Random` algorithm exactly (the legacy Knuth subtractive
generator), and the Monte Carlo EAD simulation reuses seven fixed per-curve seed constants carried
over from the C# source. Together these give bit-for-bit reproducibility across languages and
against the original: at exactly 100 iterations, the seeded EAD benchmark returns
`121194.5159789352` in C++, R, Python, and the real HEC-FDA C#.

The dev-only oracle gate (`tools/verify_oracles.py`) replays every fixture against the real
`HEC.FDA.Statistics`/`HEC.FDA.Model` source and currently reproduces all 820 assertions with 0
failures. It needs a local `dotnet` install and the pinned upstream submodule, so it does not run
in CI; the fixture-based test suites that do run in CI (`make test-core`, `make test-r`,
`make test-py`) check the same values without needing dotnet.

## Layout

| Path | Purpose |
|------|---------|
| `core/` | C++17 core library (headers, tests) |
| `fixtures/` | language-neutral oracle fixtures (JSON) validating both packages |
| `hecfdar/` | R package (cpp11) |
| `hecfdapy/` | Python package (scikit-build-core + pybind11) |
| `site/` | Quarto documentation site (worked examples, API reference, porting status) |
| `tools/` | build and validation scripts, incl. the dotnet oracle gate |
| `upstream/HEC-FDA/` | pinned dev-only git submodule of the upstream C#, used for provenance and the oracle gate only |

`hecfdar` and `hecfdapy` share the same code from `core/` and `fixtures/` using symlinks, resolved
at build time. See `.claude/CLAUDE.md` and `.claude/PLAN.md` for the port architecture and
development history.

## Build from source / development

Feel free to use your own tools but a [pixi](https://pixi.prefix.dev/latest/) 
environment is included which can conveniently install the system dependencies required 
to build everything from source:
```bash
# build and then run tests, these call build targets in the Makefile
pixi run test-core
pixi run test-r
pixi run test-py
```

If you just want to build and install the packages to use them (no tests):

```bash
pixi run install-r     # or: make install-r (needs cpp11 in your R library)
pixi run install-py    # or: make install-py
pixi run install       # both
```

`install-r` installs into your default R library; `install-py` installs into pixi's python
(use `make install-py PYTHON=...` to target another one).

Or you can try running the build directly:
```bash
# (optional) install system deps and run a special shell with the proper paths
pixi install
pixi shell

# C++ core
cmake -S core -B core/build && cmake --build core/build && ctest --test-dir core/build --output-on-failure

# R package (regenerate registration only after editing hecfdar/src/*.cpp)
Rscript -e 'cpp11::cpp_register("hecfdar")'
R CMD INSTALL --preclean hecfdar
Rscript -e 'testthat::test_local("hecfdar")'

# Python package
pip install ./hecfdapy
pytest hecfdapy/tests
```

Python comes with the pixi environment but R itself is not managed by pixi; 
[rig](https://github.com/r-lib/rig) is recommended for installing and pinning R versions:

    rig add 4.6

R development packages (cpp11, testthat, roxygen2, pkgdown, and friends) are managed with
[rv](https://github.com/a2-ai/rv): `rv sync` restores `rv.lock` into `rv/library/`, which
`.Rprofile` activates for any R session started at the repo root. The R-touching pixi tasks
(`test-r`, `build-r`, `install-r`, `docs`) run `rv sync` automatically first. `rproject.toml`
pins `r_version = "4.6"` and is the package manifest; it and `rv.lock` are committed, while
`rv/library/` is the machine-local install and stays out of git. Where rv is not installed
(for example CI, which installs from `hecfdar/DESCRIPTION`), everything degrades to a
warning and the default library paths apply.

## Why?

USACE-HEC builds and maintains flood-damage and risk-assessment tools used across the profession,
but HEC-FDA itself is a Windows desktop application built on a C# and MVVM stack, not something a
researcher can call from a script. Porting the numerical core to a small, dependency-free C++17
library, and binding it into R and Python, makes the same computations available inside ordinary
data-analysis workflows: a notebook, an R script, a batch job on a cluster. Seeded reproducibility
across languages and against the original C# means results can be checked, shared, and rerun by
someone using a different toolchain and still get the same numbers.

## AI use and credit

Anthropic's Claude was used to facilitate the porting process, Fable and Opus 4.8 for planning, 
Sonnet 5 and Haiku 4.5 for implementation. 

All credit for the design and implementation of the original tool goes to the
[USACE Hydrologic Engineering Center](https://github.com/HydrologicEngineeringCenter) and the
contributors to [HEC-FDA](https://github.com/HydrologicEngineeringCenter/HEC-FDA).

# Other porting projects
- [corehydro](https://github.com/cameronbracken/corehydro) is a C++ port of USACE-RMC libraries. 

## License

The C++ core and both packages are released under the
[MIT](LICENSE) as is HEC-FDA.
