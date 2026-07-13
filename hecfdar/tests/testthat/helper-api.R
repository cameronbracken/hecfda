# Shared helpers for the public-API tests: read canonical fixtures and map their `construct`
# blocks onto public-API argument shapes. Oracle values are never hardcoded here or in tests.
fx_read = function(...) {
  jsonlite::fromJSON(
    system.file("fixtures", ..., package = "hecfdar"),
    simplifyVector = FALSE
  )
}

fx_dist = function(d) list(dist = d$type, params = as.double(unlist(d$params)))

fx_curve = function(cv) {
  out = list(
    x = as.double(unlist(cv$xs)),
    dist = vapply(cv$ys, \(y) y$type, character(1)),
    params = lapply(cv$ys, \(y) as.double(unlist(y$params)))
  )
  if (!is.null(cv$damage_category)) {
    out$damage_category = cv$damage_category
    out$asset_category = cv$asset_category
  }
  out
}

fx_sim_args = function(construct) {
  list(
    impact_area_id = as.integer(construct$impact_area_id),
    flow_frequency = if (!is.null(construct$flow_frequency)) fx_dist(construct$flow_frequency),
    flow_stage = if (!is.null(construct$flow_stage)) fx_curve(construct$flow_stage),
    frequency_stage = if (!is.null(construct$frequency_stage)) {
      fs = construct$frequency_stage
      list(
        probabilities = as.double(unlist(fs$exceedance_probabilities)),
        stages = as.double(unlist(fs$values)),
        erl = as.double(fs$equivalent_record_length),
        damage_category = fs$damage_category,
        asset_category = fs$asset_category
      )
    },
    stage_damage = lapply(construct$stage_damage, fx_curve),
    threshold = if (!is.null(construct$additional_threshold)) {
      list(
        id = as.integer(construct$additional_threshold$threshold_id),
        value = as.double(construct$additional_threshold$value)
      )
    },
    levee = if (!is.null(construct$levee)) {
      c(fx_curve(construct$levee),
        list(top_of_levee_elevation = as.double(construct$levee$top_of_levee_elevation)))
    }
  )
}

expect_fx = function(got, assertion) {
  if (assertion$mode == "rel") {
    expect_equal(got, assertion$expected, tolerance = assertion$tol)
  } else {
    expect_true(abs(got - assertion$expected) <= assertion$tol,
                label = sprintf("got %.17g, expected %.17g +/- %g (abs)",
                                got, assertion$expected, assertion$tol))
  }
}
