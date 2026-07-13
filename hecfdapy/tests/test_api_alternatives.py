import hecfdapy as fda
from api_helpers import fx_read, fx_sim_args, assert_fx


def _by_method(case, m):
    return next(a for a in case["assertions"] if a["method"] == m)


def test_eqad_oracle_table():
    fx = fx_read("alternatives", "alternative.json")
    cases = [c for c in fx["cases"] if c.get("kind") == "compute_eqad"]
    assert len(cases) == 8
    for case in cases:
        a = case["assertions"][0]
        got = fda.eqad(*a["args"])
        assert_fx(got, a)


def _scenario(construct):
    return fda.scenario_results([fx_sim_args(construct)],
                                min_iterations=1, max_iterations=1, deterministic=True)


def test_alternative_ead_annualization_oracle():
    fx = fx_read("alternatives", "end_to_end.json")
    case = next(c for c in fx["cases"] if c["name"] == "alternative_results_test_futyr2072_frac2")
    alt = fda.alternative_ead(
        _scenario(case["base_construct"]), _scenario(case["future_construct"]),
        base_year=case["base_year"], future_year=case["future_year"],
        period_of_analysis=case["period_of_analysis"], discount_rate=case["discount_rate"],
        alternative_id=case["alternative_id"],
    )
    assert_fx(alt["mean_eqad"], _by_method(case, "sample_mean_eqad"))
    assert_fx(alt["base_year_ead"], _by_method(case, "sample_mean_base_year_ead"))
    assert_fx(alt["future_year_ead"], _by_method(case, "sample_mean_future_year_ead"))


def test_alternative_comparison_benefits_oracle():
    fx = fx_read("alternatives", "end_to_end.json")
    case = next(c for c in fx["cases"] if c["name"] == "compute_eqad_futyr2072")

    def mk_alt(base_c, future_c, alt_id):
        return fda.alternative_ead(
            _scenario(base_c), _scenario(future_c),
            base_year=case["base_year"], future_year=case["future_year"],
            period_of_analysis=case["period_of_analysis"],
            discount_rate=case["discount_rate"], alternative_id=alt_id,
        )

    without = mk_alt(case["without_base_construct"], case["without_future_construct"],
                     case["without_alternative_id"])
    with_alt = mk_alt(case["with_base_construct"], case["with_future_construct"],
                      case["with_alternative_id"])
    cmp = fda.alternative_comparison(without, with_alt)
    row = next(r for r in cmp["reduced"]
               if r["alternative_id"] == case["with_alternative_id"])
    assert_fx(row["base_year_ead_reduced"], _by_method(case, "sample_mean_base_year_ead_reduced"))
    assert_fx(row["future_year_ead_reduced"],
              _by_method(case, "sample_mean_future_year_ead_reduced"))
    assert_fx(row["with_project_eqad"], _by_method(case, "sample_mean_with_project_eqad"))
    assert_fx(cmp["without_base_year_ead"],
              _by_method(case, "sample_mean_without_project_base_year_ead"))
