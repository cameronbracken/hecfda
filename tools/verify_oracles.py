#!/usr/bin/env python3
"""Dev-only oracle gate: reproduce every fixture value against the REAL HEC-FDA C#.

Builds+runs tools/oracle_emitter (which reconstructs each fixture case with the real
Statistics + subset-compiled Model paired-data types) and asserts every fixture `expected`
reproduces the emitted value within its tol/mode. Needs `dotnet` + the pinned upstream submodule.
Not wired into CI. Runs the net9 emitter on the net10 runtime via DOTNET_ROLL_FORWARD=Major.
"""
import json, math, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES = os.path.join(ROOT, "fixtures")


def close(got, exp, tol, mode):
    if mode == "vector":
        if len(got) != len(exp):
            return False
        return all(abs(a - b) <= max(tol, 1e-15) for a, b in zip(got, exp))
    if mode == "rel":
        denom = abs(exp) if exp else 1.0
        return abs(got - exp) / denom <= (tol if tol else 1e-15)
    # abs / exact
    if isinstance(got, list):
        got = got[0]
    return abs(got - exp) <= tol


def load_expected():
    """(file, case, assertion_index) -> (expected, tol, mode)."""
    table = {}
    for dirpath, _, files in os.walk(FIXTURES):
        for fn in files:
            if not fn.endswith(".json"):
                continue
            path = os.path.join(dirpath, fn)
            fx = json.load(open(path))
            if "target" not in fx:
                continue
            rel = os.path.relpath(path, FIXTURES).replace("\\", "/")
            for ci, c in enumerate(fx["cases"]):
                name = c.get("name", str(ci))
                for ai, a in enumerate(c["assertions"]):
                    table[(rel, name, ai)] = (a["expected"], a.get("tol", 0.0), a.get("mode", "abs"))
    return table


def main():
    env = dict(os.environ, DOTNET_ROLL_FORWARD="Major", HECFDA_FIXTURES=FIXTURES)
    proc = subprocess.run(
        ["dotnet", "run", "--project", os.path.join(ROOT, "tools", "oracle_emitter"), "-c", "Release"],
        capture_output=True, text=True, env=env,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + "\n" + proc.stderr + "\n")
        sys.exit("oracle emitter failed to run")
    emitted = json.loads(proc.stdout.strip().splitlines()[-1])
    expected = load_expected()

    reproduced = failed = 0
    for e in emitted:
        key = (e["file"], e["case"], e["assertion"])
        if key not in expected:
            print(f"WARN no fixture assertion for {key}")
            continue
        exp, tol, mode = expected[key]
        if close(e["value"], exp, tol, mode):
            reproduced += 1
        else:
            failed += 1
            print(f"FAIL {key} method={e['method']} emitted={e['value']} expected={exp} mode={mode} tol={tol}")

    print(f"\n{reproduced} reproduced, {failed} failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
