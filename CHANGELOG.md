# Changelog

All notable changes to hecfda (the shared C++ core, the `hecfdar` R package, and the
`hecfdapy` Python package) are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/). The three components are versioned together.

## [Unreleased]

## [0.1.1] - 2026-07-14

### Changed

- **Relicensed from 0BSD to MIT**, matching upstream HEC-FDA. `hecfdar` uses the CRAN
  `MIT + file LICENSE` form; the root and `hecfdapy` LICENSE files carry the full MIT text.
- `ead_simulation()` now rejects supplying both frequency input paths (`flow_frequency` +
  `flow_stage` and `frequency_stage`) with a clear error in both packages; the behavior was
  previously undefined.

### Added

- **rv-managed R development library**: `rproject.toml` + `rv.lock` pin the dev packages,
  `.Rprofile` activates `rv/library/`, and the R-touching pixi tasks (`test-r`, `build-r`,
  `docs`) run `rv sync` first. R itself is installed with rig (see the README).
- **Install-only targets** for package users: `make install-r` / `install-py` / `install`
  and the matching pixi tasks build and install without running tests.

### Fixed

- Examples index described example 03 as spanning impact areas (that is example 04).
- The docs deploy workflow now also triggers on `fixtures/**` and `tools/**` changes.

## [0.1.0] - 2026-07-13

First tagged release. Everything below is new.

### Added

- **Shared C++17 core**: a faithful port of the numerical core of USACE-HEC HEC-FDA
  (`HEC.FDA.Statistics` and the pure-numeric parts of `HEC.FDA.Model`): 13 distribution
  families, special functions, paired-data curve algebra with the .NET `Array.BinarySearch`
  semantics, structure inventories and occupancy-type uncertainty, the stage-damage compute,
  the seeded Monte Carlo EAD simulation with performance metrics (AEP, assurance, levee
  fragility), scenarios, EqAD annualization, and with/without-project benefits.
- **R package `hecfdar`** and **Python package `hecfdapy`**, thin bindings over the same
  compiled core. A bit-exact port of .NET's seeded `System.Random` means seeded results are
  identical across R, Python, and the upstream C# (the 100-iteration EAD benchmark
  121194.5159789352 reproduces bit-for-bit).
- **Public workflow API** (same surface in both packages): `stage_damage()`,
  `ead_simulation()`, `scenario_results()`, `alternative_ead()` / `eqad()`,
  `alternative_comparison()`, plus distribution evaluators (`dist_pdf`/`dist_cdf`/
  `dist_quantile`/`dist_sample`) and `rng_sequence()`.
- **Validation**: a language-neutral oracle fixture suite consumed by C++, R, and Python
  test runners, plus a dev-only gate replaying all 820 fixture assertions against the real
  HEC-FDA C# source (820 reproduced, 0 failed).
- **Documentation site** with five worked examples in both languages, a porting status page,
  and full API references (quartodoc for Python, pkgdown for R).
- **Pixi development environment** with tasks mapping to the Makefile targets.

[Unreleased]: https://github.com/cameronbracken/hecfda/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/cameronbracken/hecfda/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/cameronbracken/hecfda/releases/tag/v0.1.0
