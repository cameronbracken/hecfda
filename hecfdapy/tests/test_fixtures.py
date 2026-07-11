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

def test_rng_digest():
    fx = _read("sampling/rng_digest.json")
    for c in fx["cases"]:
        for a in c["assertions"]:
            n = int(a["args"][0])
            got = sum(bf.rng_sequence(int(c["construct"]["seed"]), n))
            _close(got, a["expected"], a["tol"], a["mode"])

# Generic factory-based distribution dispatch (Task A4): drives any `distribution`-kind fixture
# through bf.dist_eval(type, params, method, x). `fit_*` methods construct a distribution via
# IDistribution::fit(data) and are verified in C++ + the dotnet oracle gate only (per the task
# brief -- binding the polymorphic Fit() return type into Python adds no coverage over those
# two), so they are skipped here.
def _run_distribution_fixture(path):
    fx = _read(path)
    for c in fx["cases"]:
        for a in c["assertions"]:
            if a["method"].startswith("fit_"):
                continue
            x = a["args"][0] if a["args"] else 0.0
            got = bf.dist_eval(c["construct"]["type"], c["construct"]["params"], a["method"], x)
            _close(got, a["expected"], a["tol"], a["mode"])

def test_normal():
    _run_distribution_fixture("distributions/normal.json")

def test_uniform():
    _run_distribution_fixture("distributions/uniform.json")

def test_deterministic():
    _run_distribution_fixture("distributions/deterministic.json")

def test_paired_data():
    fx = _read("paired_data/paired_data.json")
    for c in fx["cases"]:
        for a in c["assertions"]:
            got = bf.paired_f(c["construct"]["xs"], c["construct"]["ys"], a["method"], a["args"][0])
            _close(got, a["expected"], a["tol"], a["mode"])

def test_paired_data_duplicate_values():
    fx = _read("paired_data/duplicate_values.json")
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
