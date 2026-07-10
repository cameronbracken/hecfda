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
    got <- ns$hecfda_paired_f(as.double(unlist(c$construct$xs)), as.double(unlist(c$construct$ys)), a$method, as.double(a$args[[1]]))
    cmp(got, a$expected, a$tol, a$mode)
  }
})

test_that("uncertain_paired_data fixture", {
  fx <- read_fx("paired_data/uncertain_paired_data.json")
  for (c in fx$cases) for (a in c$assertions) {
    means <- as.double(sapply(c$construct$ys, `[[`, "mean")); sds <- as.double(sapply(c$construct$ys, `[[`, "sd"))
    got <- ns$hecfda_upd_sample_integrate(as.double(unlist(c$construct$xs)), means, sds, c$seed)
    cmp(got, a$expected, a$tol, a$mode)
  }
})
