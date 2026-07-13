#' Evaluate a ported HEC-FDA distribution
#'
#' Density, CDF, quantile, and seeded sampling for the 13 distribution families ported from
#' `HEC.FDA.Statistics`. Families are addressed by name with a numeric parameter vector in the
#' core factory's order (the same order `fixtures/distributions/*.json` use), e.g.
#' `Normal = c(mean, sd, sample_size)`, `Uniform = c(min, max, sample_size)`,
#' `Triangular = c(min, most_likely, max, sample_size)`.
#'
#' Sampling draws `inverse_cdf(u)` over the seeded generator stream ([rng_sequence()]), the same
#' chain the ported Monte Carlo computes use, so seeded draws match Python and the upstream C#.
#'
#' @param dist distribution family name, e.g. `"Normal"`, `"LogPearson3"`, `"Triangular"`.
#' @param params numeric parameter vector in factory order.
#' @param x,p numeric vector of evaluation points / probabilities.
#' @param n number of draws.
#' @param seed integer seed.
#' @return numeric vector.
#' @export
#' @examples
#' dist_cdf("Normal", c(0, 1, 1), 0) # 0.5
#' dist_sample("Normal", c(0, 1, 1), n = 3L, seed = 1234L)
dist_pdf = function(dist, params, x) {
  vapply(x, \(xi) hecfda_dist_eval(dist, as.double(params), "pdf", xi), numeric(1))
}

#' @rdname dist_pdf
#' @export
dist_cdf = function(dist, params, x) {
  vapply(x, \(xi) hecfda_dist_eval(dist, as.double(params), "cdf", xi), numeric(1))
}

#' @rdname dist_pdf
#' @export
dist_quantile = function(dist, params, p) {
  vapply(p, \(pi) hecfda_dist_eval(dist, as.double(params), "inverse_cdf", pi), numeric(1))
}

#' @rdname dist_pdf
#' @export
dist_sample = function(dist, params, n, seed) {
  hecfda_dist_sample(dist, as.double(params), as.integer(n), as.integer(seed))
}
