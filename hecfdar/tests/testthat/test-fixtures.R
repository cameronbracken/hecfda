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

test_that("rng_digest fixture", {
  fx <- read_fx("sampling/rng_digest.json")
  for (c in fx$cases) for (a in c$assertions) {
    n <- as.integer(a$args[[1]])
    got <- sum(ns$hecfda_rng_sequence(as.integer(c$construct$seed), n))
    cmp(got, a$expected, a$tol, a$mode)
  }
})

# Generic factory-based distribution dispatch (Task A4): drives any `distribution`-kind fixture
# through hecfda_dist_eval(type, params, method, x). `fit_*` methods construct a distribution via
# IDistribution::fit(data) and are verified in C++ + the dotnet oracle gate only (per the task
# brief -- binding the polymorphic Fit() return type into R adds no coverage over those two), so
# they are skipped here.
run_distribution_fixture <- function(path) {
  fx <- read_fx(path)
  for (c in fx$cases) for (a in c$assertions) {
    if (startsWith(a$method, "fit_")) next
    x <- if (length(a$args) > 0) a$args[[1]] else 0
    got <- ns$hecfda_dist_eval(c$construct$type, as.double(unlist(c$construct$params)), a$method, x)
    cmp(got, a$expected, a$tol, a$mode)
  }
}

test_that("normal fixture", {
  run_distribution_fixture("distributions/normal.json")
})

test_that("uniform fixture", {
  run_distribution_fixture("distributions/uniform.json")
})

test_that("deterministic fixture", {
  run_distribution_fixture("distributions/deterministic.json")
})

test_that("paired_data fixture", {
  fx <- read_fx("paired_data/paired_data.json")
  for (c in fx$cases) for (a in c$assertions) {
    got <- ns$hecfda_paired_f(as.double(unlist(c$construct$xs)), as.double(unlist(c$construct$ys)), a$method, as.double(a$args[[1]]))
    cmp(got, a$expected, a$tol, a$mode)
  }
})

test_that("paired_data duplicate_values fixture", {
  fx <- read_fx("paired_data/duplicate_values.json")
  for (c in fx$cases) for (a in c$assertions) {
    got <- ns$hecfda_paired_f(as.double(unlist(c$construct$xs)), as.double(unlist(c$construct$ys)), a$method, as.double(a$args[[1]]))
    cmp(got, a$expected, a$tol, a$mode)
  }
})

test_that("uncertain_paired_data fixture", {
  fx <- read_fx("paired_data/uncertain_paired_data.json")
  for (c in fx$cases) for (a in c$assertions) {
    # ys migrated to {type:"Normal", params:[mean, sd, sampleSize]}; the binding still takes
    # parallel means/sds (Normal-only Phase-0 convenience path), so pull params[[1]]/params[[2]].
    means <- as.double(sapply(c$construct$ys, function(y) y$params[[1]]))
    sds <- as.double(sapply(c$construct$ys, function(y) y$params[[2]]))
    got <- ns$hecfda_upd_sample_integrate(as.double(unlist(c$construct$xs)), means, sds, c$seed)
    cmp(got, a$expected, a$tol, a$mode)
  }
})

# Phase 3 Task 7: representative structures subset (value_uncertainty, structure). The remaining
# structures targets (value_ratio_with_uncertainty, first_floor_elevation_uncertainty,
# occupancy_type, inventory) traverse the identical binding + compiled core and are validated in
# C++ (core/tests/test_fixtures.cpp) + the dotnet oracle gate only -- see .claude/CLAUDE.md's
# "R/Python distribution coverage scope" convention.
test_that("value_uncertainty fixture", {
  fx <- read_fx("structures/value_uncertainty.json")
  for (c in fx$cases) for (a in c$assertions) {
    got <- ns$hecfda_value_uncertainty(c$construct$dist, c$construct$std_or_min, c$construct$max,
                                        a$method, as.double(unlist(a$args)))
    cmp(got, a$expected, a$tol, a$mode)
  }
})

test_that("structure fixture", {
  fx <- read_fx("structures/structure.json")
  for (c in fx$cases) for (a in c$assertions) {
    oc <- c$construct$occupancy_type
    st <- c$construct$structure
    struct_params <- lapply(oc$struct_damages, function(d) as.double(unlist(d$params)))
    content_params <- lapply(oc$content_damages, function(d) as.double(unlist(d$params)))
    # C# defaults: ValueUncertainty max = 100; FirstFloorElevationUncertainty/
    # ValueRatioWithUncertainty max = double.MaxValue -- used when the fixture's "max" is absent.
    ffe_max <- if (is.null(oc$ffe$max)) .Machine$double.xmax else oc$ffe$max
    sv_max <- if (is.null(oc$structure_value$max)) 100 else oc$structure_value$max
    csvr_max <- if (is.null(oc$csvr$max)) .Machine$double.xmax else oc$csvr$max
    val_cont <- if (is.null(st$val_cont)) 0 else st$val_cont
    val_vehic <- if (is.null(st$val_vehic)) 0 else st$val_vehic
    val_other <- if (is.null(st$val_other)) 0 else st$val_other
    ground_elevation <- if (is.null(st$ground_elevation)) -999 else st$ground_elevation
    got <- ns$hecfda_structure(
      oc$name, oc$damage_category,
      as.double(unlist(oc$struct_depths)), sapply(oc$struct_damages, function(d) d$type), struct_params,
      as.double(unlist(oc$content_depths)), sapply(oc$content_damages, function(d) d$type), content_params,
      oc$ffe$dist, oc$ffe$std_or_min, ffe_max,
      oc$structure_value$dist, oc$structure_value$std_or_min, sv_max,
      oc$csvr$dist, oc$csvr$std_or_min, oc$csvr$central, csvr_max,
      as.integer(c$construct$sample[[1]]), c$construct$sample[[2]] != 0,
      st$fid, st$first_floor_elevation, st$val_struct, st$st_damcat, st$occtype,
      as.integer(st$impact_area_id), val_cont, val_vehic, val_other, ground_elevation,
      a$method, as.double(a$args[[1]])
    )
    # Matches test_fixtures.cpp's run_structure comparison exactly (not the generic cmp() helper):
    # rel divides by abs(expected) unless it is 0, in which case it divides by 1.0.
    rel_divisor <- if (abs(a$expected) > 0) abs(a$expected) else 1.0
    ok <- if (a$mode == "rel") {
      abs(got - a$expected) / rel_divisor <= a$tol
    } else {
      abs(got - a$expected) <= a$tol
    }
    expect_true(ok, info = paste("case:", c$name, "method:", a$method))
  }
})

# Phase 4 Task 9: representative subset (consequence_result, impact_area_stage_damage). The
# remaining Phase-4 targets (aggregated_consequences_binned, study_area_consequences_binned,
# inventory_compute_damages, hydraulic_profiles/correct_dry_structure_wses,
# stage_damage_geometry, scenario_stage_damage) traverse the identical binding + compiled core and
# are validated in C++ (core/tests/test_fixtures.cpp) + the dotnet oracle gate only -- see
# .claude/CLAUDE.md's "R/Python distribution coverage scope" convention.
test_that("consequence_result fixture", {
  fx <- read_fx("metrics/consequence_result.json")
  as_increments <- function(incs) lapply(incs, function(x) as.double(unlist(x)))
  for (c in fx$cases) for (a in c$assertions) {
    increments <- as_increments(c$increments)
    if (a$method == "equals") {
      cc <- c$compare_to
      got <- ns$hecfda_consequence_result(c$construct$damage_category, increments, a$method,
                                           cc$construct$damage_category, as_increments(cc$increments))
    } else {
      got <- ns$hecfda_consequence_result(c$construct$damage_category, increments, a$method, "", list())
    }
    cmp(got, a$expected, a$tol, a$mode)
  }
})

test_that("impact_area_stage_damage fixture", {
  fx <- read_fx("stage_damage/impact_area_stage_damage.json")
  for (c in fx$cases) for (a in c$assertions) {
    ctor <- c$construct
    got <- ns$hecfda_impact_area_stage_damage(
      as.integer(ctor$impact_area_id), ctor$damage_category, ctor$asset_category,
      ctor$hydraulic_stage1, ctor$hydraulic_stage2, ctor$use_reg_unreg, as.double(a$args[[1]])
    )
    cmp(got, a$expected, a$tol, a$mode)
  }
})

# Phase 5 Task 12: representative subset (system_performance_results,
# impact_area_scenario_simulation). The remaining Phase-5 targets (assurance_result_storage,
# performance_by_thresholds/threshold, categoried_paired_data/categoried_uncertain_paired_data,
# bootstrap_to_paired_data, frequency_stage_sample, default_threshold, and the seeded
# impact_area_scenario_simulation benchmarks) traverse the identical binding + compiled core and
# are validated in C++ (core/tests/test_fixtures.cpp) + the dotnet oracle gate only -- see
# .claude/CLAUDE.md's "R/Python distribution coverage scope" convention.
test_that("system_performance_results fixture", {
  fx <- read_fx("metrics/system_performance_results.json")
  # Only the "rng_conformance" case_kind -- the seeded DotNetRandom(1234) conformance pin -- is
  # bound here; "aep"/"levee" traverse the identical binding + compiled core (see note above).
  cs <- Filter(function(x) x$construct$case_kind == "rng_conformance", fx$cases)
  for (c in cs) for (a in c$assertions) {
    ctor <- c$construct
    got <- ns$hecfda_system_performance_results(
      as.integer(ctor$convergence$min_iterations), as.integer(ctor$convergence$max_iterations),
      ctor$standard_probability, as.integer(ctor$master_seed), ctor$threshold_value,
      as.integer(ctor$compute_chunks), a$method
    )
    cmp(got, a$expected, a$tol, a$mode)
  }
})

test_that("impact_area_scenario_simulation fixture", {
  fx <- read_fx("compute/impact_area_scenario_simulation_deterministic.json")
  # Only the "compute_ead" case -- the phase's headline deterministic EAD oracle (150000) -- is
  # bound here; the levee/total-risk/EALL/preview/AEP cases and the seeded benchmarks traverse the
  # identical binding + compiled core (see note above).
  cs <- Filter(function(x) x$name == "compute_ead", fx$cases)
  for (c in cs) for (a in c$assertions) {
    ctor <- c$construct
    sd <- ctor$stage_damage[[1]]
    got <- ns$hecfda_impact_area_scenario_simulation(
      as.integer(ctor$impact_area_id),
      ctor$flow_frequency$type, as.double(unlist(ctor$flow_frequency$params)),
      as.double(unlist(ctor$flow_stage$xs)),
      sapply(ctor$flow_stage$ys, function(y) y$type),
      lapply(ctor$flow_stage$ys, function(y) as.double(unlist(y$params))),
      as.double(unlist(sd$xs)),
      sapply(sd$ys, function(y) y$type),
      lapply(sd$ys, function(y) as.double(unlist(y$params))),
      sd$damage_category, sd$asset_category,
      as.integer(ctor$additional_threshold$threshold_id), ctor$additional_threshold$value,
      as.integer(a$args[[1]]), as.integer(a$args[[2]]), a$args[[3]] != 0
    )
    cmp(got, a$expected, a$tol, a$mode)
  }
})

# Phase 6 Task 12: representative subset (alternative's compute_eqad scalar dispatch, scenario's
# impact-area fan-out). The remaining Phase-6 targets (annualization_compute/AlternativeResults,
# AlternativeComparisonReport with/without benefits, the 5 ByQuantile/results metrics types, the
# un-severed Empirical/quantile chain) traverse the identical binding + compiled core and are
# validated in C++ (core/tests/test_fixtures.cpp) + the dotnet oracle gate only -- see
# .claude/CLAUDE.md's "R/Python distribution coverage scope" convention.
test_that("alternative compute_eqad fixture", {
  fx <- read_fx("alternatives/alternative.json")
  # Only the "compute_eqad" kind (the 8-row scalar EqAD oracle table, the phase's headline math) is
  # bound here; "annualization" (AlternativeResults-producing) traverses the identical binding +
  # compiled core (see note above).
  cs <- Filter(function(x) x$kind == "compute_eqad", fx$cases)
  for (c in cs) for (a in c$assertions) {
    got <- ns$hecfda_alternative_compute_eqad(
      as.double(a$args[[1]]), as.integer(a$args[[2]]), as.double(a$args[[3]]),
      as.integer(a$args[[4]]), as.integer(a$args[[5]]), as.double(a$args[[6]])
    )
    cmp(got, a$expected, a$tol, a$mode)
  }
})

test_that("scenario fixture", {
  fx <- read_fx("scenarios/scenario.json")
  for (c in fx$cases) for (a in c$assertions) {
    ias <- c$construct$impact_areas
    ia1 <- ias[[1]]
    sd <- ia1$stage_damage[[1]]
    got <- ns$hecfda_scenario(
      as.integer(sapply(ias, function(ia) ia$impact_area_id)),
      ia1$flow_frequency$type, as.double(unlist(ia1$flow_frequency$params)),
      as.double(unlist(ia1$flow_stage$xs)),
      sapply(ia1$flow_stage$ys, function(y) y$type),
      lapply(ia1$flow_stage$ys, function(y) as.double(unlist(y$params))),
      as.double(unlist(sd$xs)),
      sapply(sd$ys, function(y) y$type),
      lapply(sd$ys, function(y) as.double(unlist(y$params))),
      sd$damage_category, sd$asset_category,
      as.integer(ia1$additional_threshold$threshold_id), ia1$additional_threshold$value,
      as.integer(a$args[[1]]), as.integer(a$args[[2]]), a$args[[3]] != 0,
      as.integer(a$args[[4]])
    )
    cmp(got, a$expected, a$tol, a$mode)
  }
})
