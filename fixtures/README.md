# Fixture schema

Each file: one JSON object.

```json
{
  "target": "dotnet_random",
  "kind": "rng | distribution | paired_data | mc_paired_data",
  "source_test": "upstream test path or 'planning-captured'",
  "cases": [
    {
      "name": "seed_1234",
      "construct": { "seed": 1234 },
      "assertions": [
        { "method": "next_double_sequence", "args": [10],
          "expected": [0.399, ...], "tol": 0.0, "mode": "vector" }
      ]
    }
  ]
}
```

Comparison `mode`: `abs | rel | exact | bool | vector | matrix`.
`exact` uses `tol == 0.0` (also matches NaN==NaN). Every `kind: mc_*` case MUST carry a seed.
Monte Carlo fixtures (kind `mc_*`) carry the seed in the case-level `seed` field; runners read the
seed from there, not from an assertion's `args`.
Method/target strings map to each language's API via a small per-runner dispatch table.
No oracle values live outside these files.

## `kind: distribution` (Task A4+)

`construct` is `{ "type": "<C# class name>", "params": [...] }`, dispatched through
`IDistributionFactory::create` (and the per-language equivalents). `params` order matches the
matching `IDistributionFactory.Factory*` C# signature (documented per-type in
`i_distribution_factory.hpp`), e.g. `Normal: [mean, stDev, sampleSize]`,
`Uniform: [min, max, sampleSize]`.

`method` values and how `args` is interpreted:
- `pdf | cdf | inverse_cdf`: `args[0]` is `x`/`p`.
- `has_errors`: `args` is `[]`; `mode: bool`. Calls `validate()` first.
- `error_level`: `args` is `[]`; expected value is the raw `ErrorLevel` byte (e.g. `Minor == 2`),
  not the enum name -- compared with `mode: exact`. Calls `validate()` first.
- `fit_<param>` (e.g. `fit_mean`, `fit_min`, `fit_sample_size`): `args` is the raw data array to
  fit against (not `x`). Constructs via `IDistribution::fit(data)`, then reads `<param>` off the
  fitted distribution. Verified in the C++ suite and the dotnet oracle gate; R/Python skip
  `fit_*` methods (binding the polymorphic `Fit()` return type adds no coverage beyond those
  two runners).
