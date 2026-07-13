"""Fixture-construct -> public-API argument mapping shared by the API tests."""
import json, pathlib

FX = pathlib.Path(__file__).parent / "fixtures"


def fx_read(*parts):
    return json.loads(FX.joinpath(*parts).read_text())


def fx_dist(d):
    return {"dist": d["type"], "params": list(d["params"])}


def fx_curve(cv):
    out = {
        "x": list(cv["xs"]),
        "dist": [y["type"] for y in cv["ys"]],
        "params": [list(y["params"]) for y in cv["ys"]],
    }
    if "damage_category" in cv:
        out["damage_category"] = cv["damage_category"]
        out["asset_category"] = cv["asset_category"]
    return out


def fx_sim_args(construct):
    args = {
        "impact_area_id": construct["impact_area_id"],
        "stage_damage": [fx_curve(c) for c in construct["stage_damage"]],
    }
    if "flow_frequency" in construct:
        args["flow_frequency"] = fx_dist(construct["flow_frequency"])
        args["flow_stage"] = fx_curve(construct["flow_stage"])
    if "frequency_stage" in construct:
        fs = construct["frequency_stage"]
        args["frequency_stage"] = {
            "probabilities": list(fs["exceedance_probabilities"]),
            "stages": list(fs["values"]),
            "erl": fs["equivalent_record_length"],
            "damage_category": fs["damage_category"],
            "asset_category": fs["asset_category"],
        }
    if "additional_threshold" in construct:
        th = construct["additional_threshold"]
        args["threshold"] = {"id": th["threshold_id"], "value": th["value"]}
    if "levee" in construct:
        lv = construct["levee"]
        args["levee"] = dict(fx_curve(lv), top_of_levee_elevation=lv["top_of_levee_elevation"])
    return args


def assert_fx(got, assertion):
    exp, tol, mode = assertion["expected"], assertion["tol"], assertion["mode"]
    if mode == "rel":
        assert abs(got - exp) <= tol * (abs(exp) if exp else 1.0), (got, exp, tol, mode)
    else:
        assert abs(got - exp) <= tol, (got, exp, tol, mode)
