import json, pathlib
import hecfdapy as fda

FX = pathlib.Path(__file__).parent / "fixtures"


def test_dist_evaluators_match_internal_dispatch():
    params = [0.0, 1.0, 1]  # Normal: mean, sd, sample_size
    assert fda.dist_cdf("Normal", params, 0.0) == 0.5
    xs = [-1.5, 0.0, 2.25]
    assert fda.dist_pdf("Normal", params, xs) == [
        fda.dist_eval("Normal", params, "pdf", x) for x in xs
    ]
    ps = [0.1, 0.5, 0.9]
    assert fda.dist_quantile("Normal", params, ps) == [
        fda.dist_eval("Normal", params, "inverse_cdf", p) for p in ps
    ]


def test_dist_sample_is_seeded_inverse_cdf_of_rng_stream():
    u = fda.rng_sequence(seed=1234, n=5)
    assert fda.dist_sample("Normal", [0.0, 1.0, 1], n=5, seed=1234) == fda.dist_quantile(
        "Normal", [0.0, 1.0, 1], u
    )


def test_rng_sequence_reproduces_pinned_digest():
    fx = json.loads((FX / "sampling" / "rng_digest.json").read_text())
    case = fx["cases"][0]
    a = case["assertions"][0]
    got = sum(fda.rng_sequence(seed=int(case["construct"]["seed"]), n=int(a["args"][0])))
    assert abs(got - a["expected"]) <= 1e-12 * abs(a["expected"])
