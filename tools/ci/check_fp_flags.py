#!/usr/bin/env python3
"""Enforce the ADR-0002 floating-point policy on a configured build tree.

Every PlanetSim translation unit must be compiled with -ffp-contract=off and
without value-changing optimizations such as -ffast-math. The check reads the
compile_commands.json that CMake exports, so flags injected through
CMAKE_CXX_FLAGS, toolchain files or presets are caught as well.
"""

import json
import shlex
import sys
from pathlib import Path

REQUIRED = "-ffp-contract=off"
FORBIDDEN_PREFIXES = (
    "-ffast-math",
    "-Ofast",
    "-funsafe-math-optimizations",
    "-fassociative-math",
    "-freciprocal-math",
    "-ffinite-math-only",
    "-fno-signed-zeros",
    "-fno-trapping-math",
    "-ffp-contract=fast",
    "-ffp-contract=on",
    "-ffp-model=fast",
    "-ffp-model=aggressive",
)


def arguments(entry: dict) -> list[str]:
    if "arguments" in entry:
        return entry["arguments"]
    return shlex.split(entry["command"])


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} BUILD_DIR", file=sys.stderr)
        return 2

    database = Path(sys.argv[1]) / "compile_commands.json"
    entries = json.loads(database.read_text())
    if not entries:
        print(f"{database}: no compile commands", file=sys.stderr)
        return 1

    failures = []
    for entry in entries:
        args = arguments(entry)
        source = entry["file"]
        # The last -ffp-contract flag wins, so a later override must fail.
        contract = [arg for arg in args if arg.startswith("-ffp-contract=")]
        if not contract or contract[-1] != REQUIRED:
            failures.append(f"{source}: missing effective {REQUIRED}")
        for arg in args:
            if arg.startswith(FORBIDDEN_PREFIXES):
                failures.append(f"{source}: forbidden flag {arg}")

    for failure in failures:
        print(failure, file=sys.stderr)
    print(f"checked {len(entries)} translation units, {len(failures)} violations")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
