import json, math, pathlib
import hecfdapy as bf

FX = pathlib.Path(__file__).parent / "fixtures"

def _read(p): return json.loads((FX / p).read_text())

def _close(got, exp, tol, mode):
    if mode == "vector":
        assert len(got) == len(exp)
        for g, e in zip(got, exp): assert abs(g - e) <= max(tol, 1e-15)
    elif mode == "rel":
        t = 1e-15 if tol == 0 else tol
        assert abs(got - exp) <= t * (abs(exp) if exp else 1.0)
    else:  # abs / exact
        assert abs(got - exp) <= tol

def test_dotnet_random():
    fx = _read("sampling/dotnet_random.json")
    for c in fx["cases"]:
        for a in c["assertions"]:
            got = bf.rng_sequence(c["construct"]["seed"], a["args"][0])
            _close(got, a["expected"], a["tol"], a["mode"])

def test_normal():
    fx = _read("distributions/normal.json")
    for c in fx["cases"]:
        for a in c["assertions"]:
            got = bf.normal_eval(c["construct"]["mean"], c["construct"]["sd"], a["method"], a["args"][0])
            _close(got, a["expected"], a["tol"], a["mode"])

def test_paired_data():
    fx = _read("paired_data/paired_data.json")
    for c in fx["cases"]:
        for a in c["assertions"]:
            got = bf.paired_f(c["construct"]["xs"], c["construct"]["ys"], a["method"], a["args"][0])
            _close(got, a["expected"], a["tol"], a["mode"])

def test_uncertain_paired_data():
    fx = _read("paired_data/uncertain_paired_data.json")
    for c in fx["cases"]:
        means = [y["mean"] for y in c["construct"]["ys"]]
        sds = [y["sd"] for y in c["construct"]["ys"]]
        for a in c["assertions"]:
            got = bf.upd_sample_integrate(c["construct"]["xs"], means, sds, c["seed"])
            _close(got, a["expected"], a["tol"], a["mode"])
