test_that("eqad reproduces the 8-row ComputeEqad oracle table", {
  fx = fx_read("alternatives", "alternative.json")
  eqad_cases = Filter(\(c) identical(c$kind, "compute_eqad"), fx$cases)
  expect_length(eqad_cases, 8)
  for (case in eqad_cases) {
    a = case$assertions[[1]]
    got = eqad(
      base_value = a$args[[1]], base_year = a$args[[2]],
      future_value = a$args[[3]], future_year = a$args[[4]],
      period_of_analysis = a$args[[5]], discount_rate = a$args[[6]]
    )
    expect_fx(got, a)
  }
})

test_that("alternative_ead reproduces the AlternativeResults_Test annualization oracle", {
  fx = fx_read("alternatives", "end_to_end.json")
  case = Filter(\(c) c$name == "alternative_results_test_futyr2072_frac2", fx$cases)[[1]]
  base = scenario_results(list(fx_sim_args(case$base_construct)),
                          min_iterations = 1L, max_iterations = 1L, deterministic = TRUE)
  future = scenario_results(list(fx_sim_args(case$future_construct)),
                            min_iterations = 1L, max_iterations = 1L, deterministic = TRUE)
  alt = alternative_ead(
    base, future,
    base_year = case$base_year, future_year = case$future_year,
    period_of_analysis = case$period_of_analysis, discount_rate = case$discount_rate,
    alternative_id = case$alternative_id
  )
  by_method = \(m) Filter(\(a) a$method == m, case$assertions)[[1]]
  expect_fx(alt$mean_eqad, by_method("sample_mean_eqad"))
  expect_fx(alt$base_year_ead, by_method("sample_mean_base_year_ead"))
  expect_fx(alt$future_year_ead, by_method("sample_mean_future_year_ead"))
})

test_that("alternative_comparison reproduces the with/without benefits oracle", {
  fx = fx_read("alternatives", "end_to_end.json")
  case = Filter(\(c) c$name == "compute_eqad_futyr2072", fx$cases)[[1]]
  mk_scenario = \(construct) scenario_results(list(fx_sim_args(construct)),
                                              min_iterations = 1L, max_iterations = 1L,
                                              deterministic = TRUE)
  mk_alt = \(base_c, future_c, id) alternative_ead(
    mk_scenario(base_c), mk_scenario(future_c),
    base_year = case$base_year, future_year = case$future_year,
    period_of_analysis = case$period_of_analysis, discount_rate = case$discount_rate,
    alternative_id = id
  )
  without = mk_alt(case$without_base_construct, case$without_future_construct,
                   case$without_alternative_id)
  with_alt = mk_alt(case$with_base_construct, case$with_future_construct,
                    case$with_alternative_id)
  cmp = alternative_comparison(without, with_alt)
  by_method = \(m) Filter(\(a) a$method == m, case$assertions)[[1]]
  row = cmp$reduced[cmp$reduced$alternative_id == case$with_alternative_id, ]
  expect_fx(row$base_year_ead_reduced, by_method("sample_mean_base_year_ead_reduced"))
  expect_fx(row$future_year_ead_reduced, by_method("sample_mean_future_year_ead_reduced"))
  expect_fx(row$with_project_eqad, by_method("sample_mean_with_project_eqad"))
  expect_fx(cmp$without_base_year_ead, by_method("sample_mean_without_project_base_year_ead"))
  # eqad_reduced_exceeded_with_probability_q (a quantile accessor) is not exposed in 0.1.0.
})
