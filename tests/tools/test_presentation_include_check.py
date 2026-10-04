#!/usr/bin/env python3
"""Acceptance test that V2 rejects a deliberately forbidden include."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checker", type=Path)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="planetsim_presentation_include_") as directory:
        root = Path(directory)
        bad = root / "sim/presentation/bad.hpp"
        bad.parent.mkdir(parents=True)
        bad.write_text('#include "sim/planet/planet_state.hpp"\n', encoding="utf-8")
        result = subprocess.run([sys.executable, str(arguments.checker), str(root)], check=False)
        if result.returncode == 0:
            raise RuntimeError("presentation include checker accepted planet_state.hpp")
    print("presentation include checker rejects a forbidden include")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
