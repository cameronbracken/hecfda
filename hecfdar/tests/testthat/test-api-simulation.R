test_that("ead_simulation reproduces the deterministic compute_ead oracle", {
  fx = fx_read("compute", "impact_area_scenario_simulation_deterministic.json")
  case = Filter(\(c) c$name == "compute_ead", fx$cases)[[1]]
  args = fx_sim_args(case$construct)
  res = do.call(ead_simulation, c(args, list(
    min_iterations = 1L, max_iterations = 1L, deterministic = TRUE
  )))
  expect_fx(res$ead$mean_ead[1], case$assertions[[1]])
  expect_identical(res$ead$damage_category[1], "residential")
  expect_identical(res$ead$asset_category[1], "Structure")
})

test_that("ead_simulation reproduces the levee deterministic oracle", {
  fx = fx_read("compute", "impact_area_scenario_simulation_deterministic.json")
  case = Filter(\(c) c$name == "compute_ead_with_levee_top10", fx$cases)[[1]]
  args = fx_sim_args(case$construct)
  res = do.call(ead_simulation, c(args, list(
    min_iterations = 1L, max_iterations = 1L, deterministic = TRUE
  )))
  expect_fx(res$ead$mean_ead[1], case$assertions[[1]])
})

test_that("ead_simulation reproduces the seeded 100-iteration Monte Carlo benchmark", {
  fx = fx_read("compute", "impact_area_scenario_simulation_seeded.json")
  case = Filter(\(c) c$name == "compute_ead_iterations_100", fx$cases)[[1]]
  args = fx_sim_args(case$construct)
  res = do.call(ead_simulation, c(args, list(
    min_iterations = 100L, max_iterations = 100L, deterministic = FALSE
  )))
  expect_fx(res$ead$mean_ead[1], case$assertions[[1]])
})

test_that("ead_simulation accepts an integer-valued frequency_stage spec", {
  # Regression guard: as_sim_spec() must coerce frequency_stage$stages/probabilities/erl to
  # double before handing them to cpp11, which errors opaquely on integer input otherwise.
  fx = fx_read("compute", "impact_area_scenario_simulation_seeded.json")
  case = Filter(\(c) c$name == "graphical_stage_frequency_seeded", fx$cases)[[1]]
  args = fx_sim_args(case$construct)
  args$frequency_stage$stages = as.integer(round(args$frequency_stage$stages))
  args$frequency_stage$erl = as.integer(args$frequency_stage$erl)
  res = do.call(ead_simulation, c(args, list(
    min_iterations = 1L, max_iterations = 1L, deterministic = TRUE
  )))
  expect_true(is.numeric(res$total_ead))
  expect_false(is.na(res$total_ead))
})

test_that("ead_simulation rejects both frequency input paths at once", {
  fx = fx_read("compute", "impact_area_scenario_simulation_deterministic.json")
  case = Filter(\(c) c$name == "compute_ead", fx$cases)[[1]]
  args = fx_sim_args(case$construct)
  args$frequency_stage = list(
    probabilities = c(0.5, 0.2), stages = c(1, 2), erl = 50,
    damage_category = "residential", asset_category = "Structure"
  )
  expect_error(
    do.call(ead_simulation, c(args, list(
      min_iterations = 1L, max_iterations = 1L, deterministic = TRUE
    ))),
    "not both"
  )
})
