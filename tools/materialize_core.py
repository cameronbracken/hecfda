#!/usr/bin/env python3
"""Replace hecfda_core / fixtures symlinks with real recursive copies.

R/Python vendor the shared C++ core and JSON fixtures via symlinks so a
single source of truth lives at the repo root. Symlinks don't reliably
survive `python -m build` sdists, git checkouts on Windows, or some CI
runners, so this script walks the known symlink paths and materializes
each one into a real directory tree. Safe to re-run: paths that are
already real directories (not symlinks) are left untouched.
"""
import os
import shutil

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SYMLINK_PATHS = [
    "hecfdar/src/hecfda_core",
    "hecfdapy/src/hecfda_core",
    "hecfdar/inst/fixtures",
    "hecfdapy/tests/fixtures",
]


def materialize(rel_path: str) -> None:
    path = os.path.join(REPO_ROOT, rel_path)
    if not os.path.islink(path):
        return
    target = os.path.realpath(path)
    os.unlink(path)
    shutil.copytree(target, path)
    print(f"materialized {rel_path} -> {target}")


def main() -> None:
    for rel_path in SYMLINK_PATHS:
        materialize(rel_path)


if __name__ == "__main__":
    main()
