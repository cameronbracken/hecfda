#' Seeded random sequence (.NET-compatible)
#'
#' Draws from the ported .NET `System.Random` (the legacy Knuth subtractive generator). The same
#' seed returns the identical stream in R, Python, and the upstream HEC-FDA C#.
#'
#' @param seed integer seed.
#' @param n number of draws.
#' @return numeric vector of `n` uniform draws on `[0, 1)`.
#' @export
#' @examples
#' rng_sequence(seed = 1234L, n = 5L)
rng_sequence = function(seed, n) {
  hecfda_rng_sequence(as.integer(seed), as.integer(n))
}
