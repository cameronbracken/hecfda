import json, math, pathlib, sys
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
        # ys migrated to {type:"Normal", params:[mean, sd, sampleSize]}; the binding still takes
        # parallel means/sds (Normal-only Phase-0 convenience path), so pull params[0]/params[1].
        means = [y["params"][0] for y in c["construct"]["ys"]]
        sds = [y["params"][1] for y in c["construct"]["ys"]]
        for a in c["assertions"]:
            got = bf.upd_sample_integrate(c["construct"]["xs"], means, sds, c["seed"])
            _close(got, a["expected"], a["tol"], a["mode"])

# Phase 3 Task 7: representative structures subset (value_uncertainty, structure). The remaining
# structures targets (value_ratio_with_uncertainty, first_floor_elevation_uncertainty,
# occupancy_type, inventory) traverse the identical binding + compiled core and are validated in
# C++ (core/tests/test_fixtures.cpp) + the dotnet oracle gate only -- see .claude/CLAUDE.md's
# "R/Python distribution coverage scope" convention.
def test_value_uncertainty():
    fx = _read("structures/value_uncertainty.json")
    for c in fx["cases"]:
        for a in c["assertions"]:
            got = bf.value_uncertainty(c["construct"]["dist"], c["construct"]["std_or_min"],
                                        c["construct"]["max"], a["method"], a["args"])
            _close(got, a["expected"], a["tol"], a["mode"])

def test_structure():
    fx = _read("structures/structure.json")
    for c in fx["cases"]:
        oc = c["construct"]["occupancy_type"]
        st = c["construct"]["structure"]
        # C# defaults: ValueUncertainty max = 100; FirstFloorElevationUncertainty/
        # ValueRatioWithUncertainty max = double.MaxValue -- used when the fixture's "max" is absent.
        ffe_max = oc["ffe"].get("max", sys.float_info.max)
        sv_max = oc["structure_value"].get("max", 100)
        csvr_max = oc["csvr"].get("max", sys.float_info.max)
        for a in c["assertions"]:
            got = bf.structure(
                oc_name=oc["name"], oc_damage_category=oc["damage_category"],
                struct_depths=oc["struct_depths"],
                struct_types=[d["type"] for d in oc["struct_damages"]],
                struct_params=[d["params"] for d in oc["struct_damages"]],
                content_depths=oc["content_depths"],
                content_types=[d["type"] for d in oc["content_damages"]],
                content_params=[d["params"] for d in oc["content_damages"]],
                ffe_dist=oc["ffe"]["dist"], ffe_std_or_min=oc["ffe"]["std_or_min"], ffe_max=ffe_max,
                sv_dist=oc["structure_value"]["dist"], sv_std_or_min=oc["structure_value"]["std_or_min"],
                sv_max=sv_max,
                csvr_dist=oc["csvr"]["dist"], csvr_std_or_min=oc["csvr"]["std_or_min"],
                csvr_central=oc["csvr"]["central"], csvr_max=csvr_max,
                sample_iteration=c["construct"]["sample"][0],
                sample_compute_is_deterministic=c["construct"]["sample"][1] != 0,
                fid=st["fid"], first_floor_elevation=st["first_floor_elevation"], val_struct=st["val_struct"],
                st_damcat=st["st_damcat"], occtype=st["occtype"], impact_area_id=st["impact_area_id"],
                val_cont=st.get("val_cont", 0), val_vehic=st.get("val_vehic", 0),
                val_other=st.get("val_other", 0), ground_elevation=st.get("ground_elevation", -999),
                method=a["method"], wse=a["args"][0],
            )
            # Matches test_fixtures.cpp's run_structure comparison exactly (not the generic _close
            # helper): rel divides by abs(expected) unless it is 0, in which case it divides by 1.0.
            exp = a["expected"]
            if a["mode"] == "rel":
                divisor = abs(exp) if abs(exp) > 0 else 1.0
                assert abs(got - exp) / divisor <= a["tol"]
            else:
                assert abs(got - exp) <= a["tol"]

# Phase 4 Task 9: representative subset (consequence_result, impact_area_stage_damage). The
# remaining Phase-4 targets (aggregated_consequences_binned, study_area_consequences_binned,
# inventory_compute_damages, hydraulic_profiles/correct_dry_structure_wses,
# stage_damage_geometry, scenario_stage_damage) traverse the identical binding + compiled core and
# are validated in C++ (core/tests/test_fixtures.cpp) + the dotnet oracle gate only -- see
# .claude/CLAUDE.md's "R/Python distribution coverage scope" convention.
def test_consequence_result():
    fx = _read("metrics/consequence_result.json")
    for c in fx["cases"]:
        for a in c["assertions"]:
            if a["method"] == "equals":
                cc = c["compare_to"]
                got = bf.consequence_result(c["construct"]["damage_category"], c["increments"], a["method"],
                                             cc["construct"]["damage_category"], cc["increments"])
            else:
                got = bf.consequence_result(c["construct"]["damage_category"], c["increments"], a["method"],
                                             "", [])
            _close(got, a["expected"], a["tol"], a["mode"])

def test_impact_area_stage_damage():
    fx = _read("stage_damage/impact_area_stage_damage.json")
    for c in fx["cases"]:
        ctor = c["construct"]
        for a in c["assertions"]:
            got = bf.impact_area_stage_damage(
                ctor["impact_area_id"], ctor["damage_category"], ctor["asset_category"],
                ctor["hydraulic_stage1"], ctor["hydraulic_stage2"], ctor["use_reg_unreg"], a["args"][0],
            )
            _close(got, a["expected"], a["tol"], a["mode"])
