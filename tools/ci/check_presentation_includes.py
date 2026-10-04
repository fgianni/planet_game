#!/usr/bin/env python3
"""Reject presentation headers that reach authoritative state or solvers."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_root", type=Path)
    arguments = parser.parse_args()
    root = arguments.source_root.resolve()
    pending = list((root / "sim" / "presentation").glob("*.*pp"))
    visited: set[Path] = set()
    violations: list[str] = []
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        visited.add(path)
        for include in INCLUDE.findall(path.read_text(encoding="utf-8")):
            # The permitted cover-function header owns its physical constants;
            # constants are not a surface solver dependency.
            cover_constant = (path == root / "sim/planet/surface/cover_fractions.hpp" and
                              include == "sim/planet/surface/cryosphere_constants.hpp")
            if include == "sim/planet/planet_state.hpp" or include.startswith("sim/planet/dynamics/") or \
               include.startswith("sim/planet/atmosphere/") or \
               (include.startswith("sim/planet/surface/") and
                include != "sim/planet/surface/cover_fractions.hpp" and not cover_constant):
                violations.append(f"{path.relative_to(root)} reaches forbidden {include}")
            candidate = root / include
            if candidate.exists() and candidate not in visited:
                pending.append(candidate)
    if violations:
        print("\n".join(violations), file=sys.stderr)
        return 1
    print(f"presentation include check passed: {len(visited)} project files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
