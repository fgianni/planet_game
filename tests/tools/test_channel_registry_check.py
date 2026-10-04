#!/usr/bin/env python3
"""Black-box acceptance tests for the semantic-channel registry checker."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def run(command: list[str], success: bool) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if (result.returncode == 0) != success:
        raise RuntimeError(f"unexpected checker result: {' '.join(command)}\n{result.stderr}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checker", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("planet_cli", type=Path)
    arguments = parser.parse_args()
    baseline = arguments.baseline.read_text(encoding="utf-8")
    dump = run([str(arguments.planet_cli), "channels", "dump"], True).stdout
    if dump != baseline:
        raise RuntimeError("planet_cli channel dump differs from the committed baseline")
    with tempfile.TemporaryDirectory(prefix="planetsim_channel_registry_") as directory:
        current = Path(directory) / "current.tsv"
        current.write_text(dump, encoding="utf-8")
        command = [sys.executable, str(arguments.checker), str(arguments.baseline), str(current)]
        run(command, True)
        rows = dump.splitlines()
        current.write_text("\n".join([rows[1], rows[0], *rows[2:]]) + "\n", encoding="utf-8")
        run(command, False)
    print("channel registry checker expectations passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
