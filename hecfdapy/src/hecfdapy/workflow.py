"""The HEC-FDA workflow API: stage-damage, EAD simulation, scenarios, alternatives, benefits."""

from . import _core


def _normalize_curve(curve):
    n = len(curve["x"])
    dist = curve["dist"]
    if isinstance(dist, str):
        dist = [dist] * n
    params = curve["params"]
    if params and not isinstance(params[0], (list, tuple)):
        params = [list(params)] * n
    out = dict(curve, x=[float(v) for v in curve["x"]], dist=list(dist),
               params=[[float(v) for v in p] for p in params])
    return out


def _as_sim_spec(sim):
    """Assemble one ``_core.ead_simulation``/``_core.scenario_compute`` spec dict from a `sim`
    dict shaped like ``ead_simulation``'s arguments (minus the iteration controls):
    ``impact_area_id``, ``flow_frequency``, ``flow_stage``, ``frequency_stage``, ``stage_damage``,
    ``threshold``, ``levee``. Shared by ``ead_simulation`` (Task 2) and ``scenario_results``
    (Task 3) so the spec-assembly logic exists in exactly one place.
    """
    flow_frequency = sim.get("flow_frequency")
    frequency_stage = sim.get("frequency_stage")
    if flow_frequency is None and frequency_stage is None:
        raise ValueError("each simulation needs flow_frequency + flow_stage or frequency_stage")
    spec = {
        "impact_area_id": int(sim.get("impact_area_id", 1)),
        "stage_damage": [_normalize_curve(c) for c in sim["stage_damage"]],
    }
    if flow_frequency is not None:
        spec["flow_frequency"] = {"dist": flow_frequency["dist"],
                                  "params": [float(v) for v in flow_frequency["params"]]}
        spec["flow_stage"] = _normalize_curve(sim["flow_stage"])
    if frequency_stage is not None:
        spec["frequency_stage"] = frequency_stage
    threshold = sim.get("threshold")
    if threshold is not None:
        spec["threshold"] = {"id": int(threshold["id"]), "value": float(threshold["value"])}
    levee = sim.get("levee")
    if levee is not None:
        lv = _normalize_curve({k: v for k, v in levee.items() if k != "top_of_levee_elevation"})
        lv["top_of_levee_elevation"] = float(levee["top_of_levee_elevation"])
        spec["levee"] = lv
    return spec


def ead_simulation(stage_damage, impact_area_id=1, flow_frequency=None, flow_stage=None,
                   frequency_stage=None, threshold=None, levee=None, min_iterations=100,
                   max_iterations=100000, deterministic=False):
    """Seeded Monte Carlo expected annual damage (EAD) simulation.

    The HEC-FDA impact-area EAD compute over the ported ``ImpactAreaScenarioSimulation``.
    Random seeds are fixed per-curve constants ported from the C#, so results are reproducible
    and identical across R, Python, and the upstream HEC-FDA. Supply either ``flow_frequency`` +
    ``flow_stage``, or ``frequency_stage``.

    Parameters
    ----------
    stage_damage : list of dict
        Stage-damage curve specs: ``{"x", "dist", "params", "damage_category",
        "asset_category"}``.
    impact_area_id : int
    flow_frequency : dict, optional
        ``{"dist", "params"}`` analytical flow frequency.
    flow_stage : dict, optional
        Curve spec transforming flow to stage.
    frequency_stage : dict, optional
        Graphical stage frequency: ``{"probabilities", "stages", "erl", "damage_category",
        "asset_category"}``.
    threshold : dict, optional
        ``{"id", "value"}`` exterior-stage performance threshold.
    levee : dict, optional
        Levee fragility curve spec plus ``top_of_levee_elevation``.
    min_iterations, max_iterations : int
        Monte Carlo convergence bounds; use ``1, 1`` with ``deterministic=True`` for a single
        mean-value pass.
    deterministic : bool
        When True, every distribution collapses to its mean (no sampling).

    Returns
    -------
    dict
        ``{"ead": [{"damage_category", "asset_category", "mean_ead"}, ...], "total_ead",
        "mean_aep"}``
    """
    spec = _as_sim_spec({
        "impact_area_id": impact_area_id, "flow_frequency": flow_frequency,
        "flow_stage": flow_stage, "frequency_stage": frequency_stage,
        "stage_damage": stage_damage, "threshold": threshold, "levee": levee,
    })
    return _core.ead_simulation(spec, int(min_iterations), int(max_iterations),
                                bool(deterministic))


def scenario_results(simulations, min_iterations=100, max_iterations=100000,
                     deterministic=False):
    """Compute a scenario (impact-area fan-out); returns a summary and a single-use handle.

    See :func:`ead_simulation` for the per-simulation spec fields. The returned ``handle`` feeds
    :func:`alternative_ead`; annualization takes ownership of it (single use).
    """
    specs = [_as_sim_spec(sim) for sim in simulations]
    return _core.scenario_compute(specs, int(min_iterations), int(max_iterations),
                                  bool(deterministic))


def eqad(base_value, base_year, future_value, future_year, period_of_analysis, discount_rate):
    """Equivalent annual damage (EqAD) annualization (interpolate, present-value, PVIFA).

    The HEC-FDA annualization math alone: linearly interpolate EAD between the base and most
    likely future year, hold it flat to the end of the period of analysis, discount each year to
    present value, and divide by the annuity factor (PVIFA). Wraps the ported
    ``Alternative::ComputeEqad``.
    """
    return _core.alternative_compute_eqad(
        float(base_value), int(base_year), float(future_value), int(future_year),
        int(period_of_analysis), float(discount_rate))


def _scenario_handle(x):
    return x["handle"] if isinstance(x, dict) else x


def alternative_ead(base, future=None, *, base_year, future_year, period_of_analysis,
                    discount_rate, alternative_id=1):
    """Annualize a base/future scenario pair into EqAD alternative results.

    Builds equivalent annual damage (EqAD) results from a base-year and a future-year scenario
    compute. Wraps the ported ``Alternative::AnnualizationCompute``. ``future=None`` reuses the
    base scenario for both years (the single-scenario case). Consumes the scenario handles
    (single use): annualization takes ownership of the underlying results, so a handle cannot be
    used twice (recompute the scenario if needed).

    Returns
    -------
    dict
        ``{"mean_eqad", "base_year_ead", "future_year_ead", "handle"}`` -- ``handle`` is consumed
        by :func:`alternative_comparison` (single-use).
    """
    return _core.annualization(
        _scenario_handle(base), None if future is None else _scenario_handle(future),
        float(discount_rate), int(period_of_analysis), int(alternative_id),
        int(base_year), int(future_year))


def stage_damage(structures, occupancy_types, hydraulics, stage_frequency, stages,
                 impact_area_id=1, deterministic=True):
    """Stage-damage curves from a structure inventory (ported ImpactAreaStageDamage).

    See the R documentation for the field-by-field spec; shapes are identical with dicts in
    place of named lists and a dict of column lists in place of the data frame.

    Scope note: the deterministic path is the oracle-validated one (upstream
    ``TractableStageDamageTests``); occupancy types support structure and content curves plus
    CSVR/FFE/structure-value uncertainty (the subset the existing bindings marshal); the
    flow-frequency + discharge-stage input path stays internal (C++-validated only).

    Returns
    -------
    list of dict
        One row per (damage_category, asset_category, stage): ``{"damage_category",
        "asset_category", "stage", "damage"}``.
    """
    occ_specs = []
    for occ in occupancy_types:
        occ = dict(occ)
        occ["structure_curve"] = _normalize_curve(occ["structure_curve"])
        occ["content_curve"] = _normalize_curve(occ["content_curve"])
        occ_specs.append(occ)
    return _core.stage_damage(
        {k: list(v) for k, v in structures.items()},
        occ_specs,
        {"probabilities": [float(p) for p in hydraulics["probabilities"]],
         "wses": [[float(w) for w in profile] for profile in hydraulics["wses"]]},
        {"probabilities": [float(p) for p in stage_frequency["probabilities"]],
         "stages": [float(s) for s in stage_frequency["stages"]],
         "erl": float(stage_frequency["erl"])},
        [float(s) for s in stages], int(impact_area_id), bool(deterministic))


def alternative_comparison(without, with_):
    """With/without-project damage-reduction benefits.

    Subtracts each with-project alternative's damage distributions from the without-project
    alternative's (empirical-distribution subtraction) and reports the mean reduced EqAD and
    reduced base/future-year EAD per alternative. Wraps the ported
    ``AlternativeComparisonReport``. Consumes the alternative handles (single use).

    Parameters
    ----------
    without : dict
        An :func:`alternative_ead` result (or its ``"handle"``): the without-project condition.
    with_ : dict or list of dict
        One :func:`alternative_ead` result or a list of them: the with-project alternatives.

    Returns
    -------
    dict
        ``{"reduced": [{"alternative_id", "eqad_reduced", "base_year_ead_reduced",
        "future_year_ead_reduced", "with_project_eqad"}, ...], "without_base_year_ead",
        "without_future_year_ead"}``
    """
    if isinstance(with_, dict):
        with_ = [with_]
    return _core.alt_comparison(_scenario_handle(without),
                                [_scenario_handle(w) for w in with_])
