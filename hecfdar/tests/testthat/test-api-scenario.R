test_that("scenario_results reproduces the two-impact-area fan-out oracle", {
  fx = fx_read("scenarios", "scenario.json")
  case = fx$cases[[1]] # two_impact_area_fan_out
  sims = lapply(case$construct$impact_areas, fx_sim_args)
  res = scenario_results(sims, min_iterations = 1L, max_iterations = 1L, deterministic = TRUE)
  # assertion 1: impact area 1; assertion 2: impact area 2; assertion 3: wildcard (-999) total
  row1 = res$summary[res$summary$impact_area_id == 1, ]
  row2 = res$summary[res$summary$impact_area_id == 2, ]
  expect_fx(row1$mean_ead, case$assertions[[1]])
  expect_fx(row2$mean_ead, case$assertions[[2]])
  expect_fx(res$total_ead, case$assertions[[3]])
  expect_true(inherits(res$handle, "externalptr"))
})
