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
