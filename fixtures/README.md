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
