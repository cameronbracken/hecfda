# Internal: normalize a user curve spec. `dist` recycles a single name; `params` accepts a list
# (one numeric vector per point), a single vector (recycled), or an n x k matrix (one row per
# point).
normalize_curve = function(curve) {
  n = length(curve$x)
  curve$x = as.double(curve$x)
  curve$dist = rep_len(as.character(curve$dist), n)
  p = curve$params
  if (is.matrix(p)) {
    p = lapply(seq_len(nrow(p)), \(i) as.double(p[i, ]))
  } else if (!is.list(p)) {
    p = rep(list(as.double(p)), n)
  } else {
    p = lapply(p, as.double)
  }
  stopifnot(length(p) == n)
  curve$params = p
  curve
}

`%||%` = function(a, b) if (is.null(a)) b else a

# Internal: assemble one `hecfda_*_compute` simulation spec from a `sim` list shaped like
# `ead_simulation`'s arguments (minus the iteration controls: `impact_area_id`, `flow_frequency`,
# `flow_stage`, `frequency_stage`, `stage_damage`, `threshold`, `levee`). Shared by `ead_simulation`
# (Task 2) and `scenario_results` (Task 3) so the spec-assembly logic exists in exactly one place.
as_sim_spec = function(sim) {
  if (is.null(sim$flow_frequency) && is.null(sim$frequency_stage)) {
    stop("each simulation needs flow_frequency + flow_stage or frequency_stage")
  }
  list(
    impact_area_id = as.integer(sim$impact_area_id %||% 1L),
    flow_frequency = if (!is.null(sim$flow_frequency)) {
      list(dist = sim$flow_frequency$dist, params = as.double(sim$flow_frequency$params))
    },
    flow_stage = if (!is.null(sim$flow_stage)) normalize_curve(sim$flow_stage),
    frequency_stage = sim$frequency_stage,
    stage_damage = lapply(sim$stage_damage, normalize_curve),
    threshold = sim$threshold,
    levee = if (!is.null(sim$levee)) {
      c(normalize_curve(sim$levee[setdiff(names(sim$levee), "top_of_levee_elevation")]),
        list(top_of_levee_elevation = as.double(sim$levee$top_of_levee_elevation)))
    }
  )
}

#' Seeded Monte Carlo expected annual damage (EAD) simulation
#'
#' The HEC-FDA impact-area EAD compute: assemble a frequency function, a stage transform, and one
#' or more stage-damage curves with uncertainty, then integrate damage against frequency over a
#' seeded Monte Carlo (or a single deterministic pass). Wraps the ported
#' `ImpactAreaScenarioSimulation`. Random seeds are fixed per-curve constants ported from the C#
#' (frequency 1234, flow-stage 3456, stage-damage 6789, ...), so results are reproducible and
#' identical across R, Python, and the upstream HEC-FDA.
#'
#' Supply either `flow_frequency` + `flow_stage`, or `frequency_stage` (a graphical stage
#' frequency), not both.
#'
#' @param stage_damage list of stage-damage curve specs. A curve spec is
#'   `list(x, dist, params, damage_category, asset_category)`: `x` the stages, `dist` distribution
#'   family name(s) (recycled), `params` a list of parameter vectors (or a matrix, one row per
#'   point).
#' @param impact_area_id integer impact area label.
#' @param flow_frequency dist spec `list(dist, params)` for the analytical flow frequency.
#' @param flow_stage curve spec transforming flow to stage.
#' @param frequency_stage graphical stage-frequency spec
#'   `list(probabilities, stages, erl, damage_category, asset_category)`.
#' @param threshold optional `list(id, value)` performance threshold (exterior stage).
#' @param levee optional levee fragility curve spec plus `top_of_levee_elevation`.
#' @param min_iterations,max_iterations Monte Carlo convergence bounds. Use `1, 1` with
#'   `deterministic = TRUE` for a single mean-value pass.
#' @param deterministic when `TRUE`, every distribution collapses to its mean (no sampling).
#' @return A list: `ead` (data frame: damage_category, asset_category, mean_ead), `total_ead`
#'   (all categories), and `mean_aep` (default threshold annual exceedance probability).
#' @export
#' @examples
#' res = ead_simulation(
#'   stage_damage = list(list(
#'     x = c(0, 15, 30), dist = "Uniform",
#'     params = list(c(0, 0, 10), c(0, 600000, 10), c(0, 600000, 10)),
#'     damage_category = "residential", asset_category = "Structure"
#'   )),
#'   flow_frequency = list(dist = "Uniform", params = c(0, 100000, 1000)),
#'   flow_stage = list(
#'     x = c(0, 100000), dist = "Uniform",
#'     params = list(c(0, 0, 10), c(0, 30, 10))
#'   ),
#'   min_iterations = 1L, max_iterations = 1L, deterministic = TRUE
#' )
#' res$total_ead # 150000 (the deterministic HEC-FDA oracle)
ead_simulation = function(stage_damage, impact_area_id = 1L, flow_frequency = NULL,
                          flow_stage = NULL, frequency_stage = NULL, threshold = NULL,
                          levee = NULL, min_iterations = 100L, max_iterations = 100000L,
                          deterministic = FALSE) {
  spec = as_sim_spec(list(
    impact_area_id = impact_area_id, flow_frequency = flow_frequency, flow_stage = flow_stage,
    frequency_stage = frequency_stage, stage_damage = stage_damage, threshold = threshold,
    levee = levee
  ))
  raw = hecfda_ead_simulation(spec, as.integer(min_iterations), as.integer(max_iterations),
                              isTRUE(deterministic))
  list(
    ead = data.frame(
      damage_category = raw$damage_category,
      asset_category = raw$asset_category,
      mean_ead = raw$mean_ead
    ),
    total_ead = raw$total_ead,
    mean_aep = raw$mean_aep
  )
}
