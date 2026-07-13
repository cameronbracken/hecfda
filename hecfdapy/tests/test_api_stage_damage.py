import hecfdapy as fda
from api_helpers import fx_read, assert_fx


def _case(fx, name):
    return next(c for c in fx["cases"] if c["name"] == name)


def tractable_inputs(damage_category, stage1, stage2):
    depths = list(range(11))
    if damage_category == "Residential":
        struct_vals = list(range(0, 101, 10))
        content_vals = [0, 5, 15, 25, 35, 45, 55, 65, 75, 85, 95]
        csvr = 50
        structures = {
            "fid": ["1", "2"],
            "first_floor_elevation": [14, 15],
            "value_structure": [100, 200],
            "damage_category": ["Residential", "Residential"],
            "occupancy_type": ["Residential", "Residential"],
            "ground_elevation": [12, 12],
        }
    else:
        struct_vals = [0, 5, 15, 25, 35, 45, 55, 65, 75, 85, 95]
        content_vals = [0, 0] + list(range(10, 91, 10))
        csvr = 120
        structures = {
            "fid": ["3", "4"],
            "first_floor_elevation": [17, 18],
            "value_structure": [300, 400],
            "damage_category": ["Commercial", "Commercial"],
            "occupancy_type": ["Commercial", "Commercial"],
            "ground_elevation": [12, 12],
        }
    probabilities = [.5, .2, .1, .04, .02, .01, .004, .002]
    wses = [[stage1, stage2]]
    for _ in range(7):
        prev = wses[-1]
        wses.append([prev[0] + 1, prev[1] + 1])
    return {
        "structures": structures,
        "occupancy_types": [{
            "name": damage_category,
            "damage_category": damage_category,
            "structure_curve": {"x": depths, "dist": "Deterministic",
                                "params": [[v] for v in struct_vals]},
            "content_curve": {"x": depths, "dist": "Deterministic",
                              "params": [[v] for v in content_vals]},
            "content_to_structure_value_ratio": {"central": csvr},
        }],
        "hydraulics": {"probabilities": probabilities, "wses": wses},
        "stage_frequency": {"probabilities": probabilities, "stages": list(range(12, 20)),
                            "erl": 50},
    }


def test_stage_damage_reproduces_tractable_residential_oracle():
    fx = fx_read("stage_damage", "impact_area_stage_damage.json")
    case = _case(fx, "residential_structure_no_reg_unreg")
    inp = tractable_inputs("Residential", case["construct"]["hydraulic_stage1"],
                           case["construct"]["hydraulic_stage2"])
    res = fda.stage_damage(**inp, stages=list(range(12, 20)),
                           impact_area_id=case["construct"]["impact_area_id"])
    target = {row["stage"]: row["damage"] for row in res
              if row["damage_category"] == "Residential" and row["asset_category"] == "Structure"}
    for a in case["assertions"]:
        assert_fx(target[a["args"][0]], a)


def test_stage_damage_reproduces_tractable_commercial_content_oracle():
    fx = fx_read("stage_damage", "impact_area_stage_damage.json")
    case = _case(fx, "commercial_content_no_reg_unreg")
    inp = tractable_inputs("Commercial", case["construct"]["hydraulic_stage1"],
                           case["construct"]["hydraulic_stage2"])
    res = fda.stage_damage(**inp, stages=list(range(12, 20)),
                           impact_area_id=case["construct"]["impact_area_id"])
    target = {row["stage"]: row["damage"] for row in res
              if row["damage_category"] == "Commercial" and row["asset_category"] == "Content"}
    for a in case["assertions"]:
        assert_fx(target[a["args"][0]], a)
