import hecfdapy as fda
from api_helpers import fx_read, fx_sim_args, assert_fx


def test_scenario_results_two_impact_area_fan_out():
    fx = fx_read("scenarios", "scenario.json")
    case = fx["cases"][0]
    sims = [fx_sim_args(c) for c in case["construct"]["impact_areas"]]
    res = fda.scenario_results(sims, min_iterations=1, max_iterations=1, deterministic=True)
    by_id = {row["impact_area_id"]: row for row in res["summary"]}
    assert_fx(by_id[1]["mean_ead"], case["assertions"][0])
    assert_fx(by_id[2]["mean_ead"], case["assertions"][1])
    assert_fx(res["total_ead"], case["assertions"][2])
    assert res["handle"] is not None
