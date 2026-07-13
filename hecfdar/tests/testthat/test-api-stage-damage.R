tractable_inputs = function(damage_category, stage1, stage2) {
  depths = 0:10
  if (damage_category == "Residential") {
    struct_vals = seq(0, 100, by = 10)
    content_vals = c(0, 5, 15, 25, 35, 45, 55, 65, 75, 85, 95)
    csvr = 50
    structures = data.frame(
      fid = c("1", "2"), first_floor_elevation = c(14, 15), value_structure = c(100, 200),
      damage_category = "Residential", occupancy_type = "Residential", ground_elevation = 12
    )
  } else {
    struct_vals = c(0, 5, 15, 25, 35, 45, 55, 65, 75, 85, 95)
    content_vals = c(0, 0, seq(10, 90, by = 10))
    csvr = 120
    structures = data.frame(
      fid = c("3", "4"), first_floor_elevation = c(17, 18), value_structure = c(300, 400),
      damage_category = "Commercial", occupancy_type = "Commercial", ground_elevation = 12
    )
  }
  probabilities = c(.5, .2, .1, .04, .02, .01, .004, .002)
  wses = Reduce(\(prev, i) prev + 1, seq_len(7), init = c(stage1, stage2), accumulate = TRUE)
  list(
    structures = structures,
    occupancy_types = list(list(
      name = damage_category, damage_category = damage_category,
      structure_curve = list(x = depths, dist = "Deterministic",
                             params = lapply(struct_vals, \(v) v)),
      content_curve = list(x = depths, dist = "Deterministic",
                           params = lapply(content_vals, \(v) v)),
      content_to_structure_value_ratio = list(central = csvr)
    )),
    hydraulics = list(probabilities = probabilities, wses = wses),
    stage_frequency = list(probabilities = probabilities, stages = 12:19, erl = 50)
  )
}

test_that("stage_damage reproduces the tractable residential oracle", {
  fx = fx_read("stage_damage", "impact_area_stage_damage.json")
  case = Filter(\(c) c$name == "residential_structure_no_reg_unreg", fx$cases)[[1]]
  inp = tractable_inputs("Residential", case$construct$hydraulic_stage1,
                         case$construct$hydraulic_stage2)
  res = do.call(stage_damage, c(inp, list(
    stages = 12:19, impact_area_id = case$construct$impact_area_id
  )))
  target = res[res$damage_category == "Residential" & res$asset_category == "Structure", ]
  for (a in case$assertions) {
    expect_fx(target$damage[target$stage == a$args[[1]]], a)
  }
})

test_that("stage_damage reproduces the tractable commercial content oracle", {
  fx = fx_read("stage_damage", "impact_area_stage_damage.json")
  case = Filter(\(c) c$name == "commercial_content_no_reg_unreg", fx$cases)[[1]]
  inp = tractable_inputs("Commercial", case$construct$hydraulic_stage1,
                         case$construct$hydraulic_stage2)
  res = do.call(stage_damage, c(inp, list(
    stages = 12:19, impact_area_id = case$construct$impact_area_id
  )))
  target = res[res$damage_category == "Commercial" & res$asset_category == "Content", ]
  for (a in case$assertions) {
    expect_fx(target$damage[target$stage == a$args[[1]]], a)
  }
})
