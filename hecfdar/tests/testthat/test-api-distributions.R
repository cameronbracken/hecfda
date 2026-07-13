# API-layer tests: the numerical core is fixture-validated (test-fixtures.R); these tests check
# the public wrappers agree with the internal fixture-validated dispatch and with each other.
test_that("dist_ evaluators match the internal fixture-validated dispatch", {
  eval_internal = asNamespace("hecfdar")$hecfda_dist_eval
  params = c(0, 1, 1) # Normal: mean, sd, sample_size (fixtures/distributions/normal.json)
  expect_identical(dist_cdf("Normal", params, 0), 0.5)
  xs = c(-1.5, 0, 2.25)
  expect_identical(
    dist_pdf("Normal", params, xs),
    vapply(xs, \(x) eval_internal("Normal", params, "pdf", x), numeric(1))
  )
  ps = c(0.1, 0.5, 0.9)
  expect_identical(
    dist_quantile("Normal", params, ps),
    vapply(ps, \(p) eval_internal("Normal", params, "inverse_cdf", p), numeric(1))
  )
})

test_that("dist_sample is the seeded inverse-cdf of the rng stream", {
  u = rng_sequence(seed = 1234L, n = 5L)
  expect_identical(
    dist_sample("Normal", c(0, 1, 1), n = 5L, seed = 1234L),
    dist_quantile("Normal", c(0, 1, 1), u)
  )
})

test_that("rng_sequence reproduces the pinned .NET stream digest", {
  fx = jsonlite::fromJSON(
    system.file("fixtures", "sampling", "rng_digest.json", package = "hecfdar"),
    simplifyVector = FALSE
  )
  case = fx$cases[[1]]
  a = case$assertions[[1]]
  got = sum(rng_sequence(seed = as.integer(case$construct$seed), n = as.integer(a$args[[1]])))
  expect_equal(got, a$expected, tolerance = 1e-12)
})
