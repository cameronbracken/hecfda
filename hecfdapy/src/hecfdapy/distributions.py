"""Distribution evaluators over the shared C++ core.

Families are addressed by name with a parameter list in the core factory's order (the same
order ``fixtures/distributions/*.json`` use), e.g. ``Normal = [mean, sd, sample_size]``,
``Uniform = [min, max, sample_size]``, ``Triangular = [min, most_likely, max, sample_size]``.
"""

from ._core import dist_eval as _dist_eval
from ._core import dist_sample as _dist_sample


def _vectorize(dist, params, values, method):
    if hasattr(values, "__iter__"):
        return [_dist_eval(dist, list(params), method, float(v)) for v in values]
    return _dist_eval(dist, list(params), method, float(values))


def dist_pdf(dist, params, x):
    """Probability density of distribution `dist` at `x` (scalar or sequence).

    Parameters
    ----------
    dist : str
        Distribution family name, e.g. ``"Normal"``, ``"LogPearson3"``.
    params : sequence of float
        Parameters in core factory order.
    x : float or sequence of float
        Evaluation point(s).

    Returns
    -------
    float or list of float
    """
    return _vectorize(dist, params, x, "pdf")


def dist_cdf(dist, params, x):
    """Cumulative probability of distribution `dist` at `x` (scalar or sequence)."""
    return _vectorize(dist, params, x, "cdf")


def dist_quantile(dist, params, p):
    """Quantile (inverse CDF) of distribution `dist` at probability `p` (scalar or sequence)."""
    return _vectorize(dist, params, p, "inverse_cdf")


def dist_sample(dist, params, n, seed):
    """Draw `n` seeded samples from distribution `dist`.

    Sampling is ``inverse_cdf(u)`` over the seeded .NET-compatible generator stream
    (:func:`hecfdapy.rng_sequence`), the same chain the ported Monte Carlo computes use, so
    seeded draws match R and the upstream C#.
    """
    return _dist_sample(dist, list(params), int(n), int(seed))
