#' Equivalent annual damage (EqAD) annualization
#'
#' The HEC-FDA annualization math alone: linearly interpolate EAD between the base and most
#' likely future year, hold it flat to the end of the period of analysis, discount each year to
#' present value, and divide by the annuity factor (PVIFA). Wraps the ported
#' `Alternative::ComputeEqad`.
#'
#' @param base_value,future_value EAD at the base / most likely future year.
#' @param base_year,future_year analysis years (`future_year >= base_year`).
#' @param period_of_analysis length of the analysis period in years.
#' @param discount_rate annual discount rate (e.g. `0.07`).
#' @return The equivalent annual damage (scalar).
#' @export
#' @examples
#' eqad(35000, 2023, 50000, 2072, 50, 0.07)
eqad = function(base_value, base_year, future_value, future_year, period_of_analysis,
                discount_rate) {
  hecfda_alternative_compute_eqad(
    as.double(base_value), as.integer(base_year), as.double(future_value),
    as.integer(future_year), as.integer(period_of_analysis), as.double(discount_rate)
  )
}

as_scenario_handle = function(x) {
  if (inherits(x, "externalptr")) return(x)
  if (is.list(x) && inherits(x$handle, "externalptr")) return(x$handle)
  stop("expected a scenario_results() result or its $handle")
}

#' Annualize a base/future scenario pair into an alternative
#'
#' Builds equivalent annual damage (EqAD) results from a base-year and a future-year scenario
#' compute. Wraps the ported `Alternative::AnnualizationCompute`. The scenario handles are
#' consumed: annualization takes ownership of the underlying results, so a handle cannot be used
#' twice (recompute the scenario if needed).
#'
#' @param base,future results from [scenario_results()] (or their `$handle`). `future = NULL`
#'   reuses the base scenario for both years (the single-scenario case).
#' @param base_year,future_year analysis years.
#' @param period_of_analysis length of the analysis period in years.
#' @param discount_rate annual discount rate.
#' @param alternative_id integer label carried into [alternative_comparison()].
#' @return A list: `mean_eqad`, `base_year_ead`, `future_year_ead`, and `handle` (consumed by
#'   [alternative_comparison()]; single-use).
#' @export
alternative_ead = function(base, future = NULL, base_year, future_year, period_of_analysis,
                           discount_rate, alternative_id = 1L) {
  raw = hecfda_annualization(
    as_scenario_handle(base),
    if (!is.null(future)) as_scenario_handle(future),
    as.double(discount_rate), as.integer(period_of_analysis), as.integer(alternative_id),
    as.integer(base_year), as.integer(future_year)
  )
  raw
}

as_alternative_handle = function(x) {
  if (inherits(x, "externalptr")) return(x)
  if (is.list(x) && inherits(x$handle, "externalptr")) return(x$handle)
  stop("expected an alternative_ead() result or its $handle")
}

#' With/without-project damage-reduction benefits
#'
#' Subtracts each with-project alternative's damage distributions from the without-project
#' alternative's (empirical-distribution subtraction) and reports the mean reduced EqAD and
#' reduced base/future-year EAD per alternative. Wraps the ported
#' `AlternativeComparisonReport`. Consumes the alternative handles (single use).
#'
#' @param without an [alternative_ead()] result (or its `$handle`): the without-project
#'   condition.
#' @param with one [alternative_ead()] result or a list of them: the with-project alternatives.
#' @return A list: `reduced` (data frame: alternative_id, eqad_reduced, base_year_ead_reduced,
#'   future_year_ead_reduced, with_project_eqad), `without_base_year_ead`,
#'   `without_future_year_ead`.
#' @export
alternative_comparison = function(without, with) {
  if (is.list(with) && !is.null(with$handle)) with = list(with)
  raw = hecfda_alt_comparison(
    as_alternative_handle(without),
    lapply(with, as_alternative_handle)
  )
  list(
    reduced = data.frame(
      alternative_id = raw$alternative_id,
      eqad_reduced = raw$eqad_reduced,
      base_year_ead_reduced = raw$base_year_ead_reduced,
      future_year_ead_reduced = raw$future_year_ead_reduced,
      with_project_eqad = raw$with_project_eqad
    ),
    without_base_year_ead = raw$without_base_year_ead,
    without_future_year_ead = raw$without_future_year_ead
  )
}
