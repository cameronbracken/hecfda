#' Stage-damage curves from a structure inventory
#'
#' The HEC-FDA stage-damage compute: a structure inventory, occupancy-type depth-damage
#' functions, per-frequency hydraulic stage profiles, and a graphical stage-frequency curve go
#' in; stage-damage curves (one per damage category and asset category) come out. Wraps the
#' ported `ImpactAreaStageDamage.Compute`, validated against the upstream
#' `TractableStageDamageTests`.
#'
#' The deterministic path (`deterministic = TRUE`, the default) collapses every uncertainty
#' distribution to its mean and matches the upstream oracle values. Occupancy types support
#' structure and content depth-percent-damage curves plus content-to-structure value ratio,
#' first-floor-elevation, and structure-value uncertainty.
#'
#' @param structures data frame: `fid` (character), `first_floor_elevation`, `value_structure`,
#'   `damage_category`, `occupancy_type`, `ground_elevation`, and optional `value_content`,
#'   `value_vehicle`, `value_other` (default 0).
#' @param occupancy_types list of occupancy-type specs; see Details.
#' @param hydraulics `list(probabilities, wses)`: `wses` is a list of numeric vectors, one water
#'   surface elevation profile per probability, each of length `nrow(structures)`.
#' @param stage_frequency `list(probabilities, stages, erl)`: a graphical stage-frequency curve
#'   with its equivalent record length.
#' @param stages numeric vector of stages at which to evaluate the resulting curves.
#' @param impact_area_id integer impact area label.
#' @param deterministic when `TRUE`, distributions collapse to their means (the oracle path).
#' @details An occupancy-type spec is `list(name, damage_category, structure_curve,
#'   content_curve, content_to_structure_value_ratio, first_floor_elevation, structure_value)`.
#'   Curves are `list(x = depths, dist, params)` ([ead_simulation()]'s curve spec).
#'   `content_to_structure_value_ratio` is `list(central = 50)` for a fixed percent or
#'   `list(dist, std_or_min, central, max)` with uncertainty; `first_floor_elevation` and
#'   `structure_value` are optional `list(dist, std_or_min, max)`.
#'
#'   Scope note: the deterministic path is the oracle-validated one (upstream
#'   `TractableStageDamageTests`); occupancy types support structure and content curves plus
#'   CSVR/FFE/structure-value uncertainty (the subset the existing bindings marshal); the
#'   flow-frequency + discharge-stage input path stays internal (C++-validated only).
#' @return data frame: `damage_category`, `asset_category`, `stage`, `damage`.
#' @export
stage_damage = function(structures, occupancy_types, hydraulics, stage_frequency, stages,
                        impact_area_id = 1L, deterministic = TRUE) {
  structures = as.data.frame(structures)
  occ_specs = lapply(occupancy_types, function(occ) {
    occ$structure_curve = normalize_curve(occ$structure_curve)
    occ$content_curve = normalize_curve(occ$content_curve)
    occ
  })
  raw = hecfda_stage_damage(
    as.list(structures), occ_specs,
    list(
      probabilities = as.double(hydraulics$probabilities),
      wses = lapply(hydraulics$wses, as.double)
    ),
    list(
      probabilities = as.double(stage_frequency$probabilities),
      stages = as.double(stage_frequency$stages),
      erl = as.double(stage_frequency$erl)
    ),
    as.double(stages), as.integer(impact_area_id), isTRUE(deterministic)
  )
  data.frame(
    damage_category = raw$damage_category,
    asset_category = raw$asset_category,
    stage = raw$stage,
    damage = raw$damage
  )
}
