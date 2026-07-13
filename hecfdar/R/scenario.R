#' Compute a scenario (impact-area fan-out)
#'
#' Runs one `ead_simulation`-shaped compute per impact area and folds the results into a single
#' scenario result, the input to [alternative_ead()]. Wraps the ported `Scenario` +
#' `ScenarioResults`.
#'
#' @param simulations list of simulation specs. Each spec is a named list of the
#'   [ead_simulation()] arguments (minus the iteration controls): `impact_area_id`,
#'   `stage_damage`, and either `flow_frequency` + `flow_stage` or `frequency_stage`, plus
#'   optional `threshold` and `levee`.
#' @inheritParams ead_simulation
#' @return A list: `summary` (data frame: impact_area_id, damage_category, asset_category,
#'   mean_ead), `total_ead` (all impact areas and categories), and `handle` (a classed external
#'   pointer to the scenario results, consumed by [alternative_ead()]). The handle is SINGLE-USE:
#'   annualization takes ownership of the underlying results.
#' @export
scenario_results = function(simulations, min_iterations = 100L, max_iterations = 100000L,
                            deterministic = FALSE) {
  specs = lapply(simulations, as_sim_spec)
  raw = hecfda_scenario_compute(specs, as.integer(min_iterations), as.integer(max_iterations),
                                isTRUE(deterministic))
  class(raw$handle) = c("hecfda_scenario_handle", "externalptr")
  list(
    summary = data.frame(
      impact_area_id = raw$impact_area_id,
      damage_category = raw$damage_category,
      asset_category = raw$asset_category,
      mean_ead = raw$mean_ead
    ),
    total_ead = raw$total_ead,
    handle = raw$handle
  )
}
