import hecfdapy as fda
from api_helpers import fx_read, fx_sim_args, assert_fx


def _case(fx, name):
    return next(c for c in fx["cases"] if c["name"] == name)


def test_ead_simulation_deterministic_oracle():
    fx = fx_read("compute", "impact_area_scenario_simulation_deterministic.json")
    case = _case(fx, "compute_ead")
    res = fda.ead_simulation(**fx_sim_args(case["construct"]),
                             min_iterations=1, max_iterations=1, deterministic=True)
    assert_fx(res["ead"][0]["mean_ead"], case["assertions"][0])
    assert res["ead"][0]["damage_category"] == "residential"


def test_ead_simulation_levee_oracle():
    fx = fx_read("compute", "impact_area_scenario_simulation_deterministic.json")
    case = _case(fx, "compute_ead_with_levee_top10")
    res = fda.ead_simulation(**fx_sim_args(case["construct"]),
                             min_iterations=1, max_iterations=1, deterministic=True)
    assert_fx(res["ead"][0]["mean_ead"], case["assertions"][0])


def test_ead_simulation_seeded_benchmark():
    fx = fx_read("compute", "impact_area_scenario_simulation_seeded.json")
    case = _case(fx, "compute_ead_iterations_100")
    res = fda.ead_simulation(**fx_sim_args(case["construct"]),
                             min_iterations=100, max_iterations=100, deterministic=False)
    assert_fx(res["ead"][0]["mean_ead"], case["assertions"][0])


def test_ead_simulation_rejects_both_frequency_paths():
    import pytest

    fx = fx_read("compute", "impact_area_scenario_simulation_deterministic.json")
    case = _case(fx, "compute_ead")
    args = fx_sim_args(case["construct"])
    args["frequency_stage"] = {
        "probabilities": [0.5, 0.2], "stages": [1, 2], "erl": 50,
        "damage_category": "residential", "asset_category": "Structure",
    }
    with pytest.raises(ValueError, match="not both"):
        fda.ead_simulation(**args, min_iterations=1, max_iterations=1, deterministic=True)
