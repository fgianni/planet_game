#!/usr/bin/env python3
"""Black-box acceptance tests for check_field_registry.py."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def run(command: list[str], expected_success: bool) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if (result.returncode == 0) != expected_success:
        print(f"unexpected exit status {result.returncode}: {' '.join(command)}", file=sys.stderr)
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        raise RuntimeError("registry checker result did not match expectation")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("checker", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("planet_cli", type=Path)
    arguments = parser.parse_args()

    baseline_text = arguments.baseline.read_text(encoding="utf-8")
    dump = run([str(arguments.planet_cli), "registry", "dump"], True).stdout
    if dump != baseline_text:
        raise RuntimeError("planet_cli registry dump differs from the committed baseline")

    with tempfile.TemporaryDirectory(prefix="planetsim_registry_check_") as directory:
        current_path = Path(directory) / "current.tsv"
        current_path.write_text(dump, encoding="utf-8")
        base_command = [sys.executable, str(arguments.checker), str(arguments.baseline)]
        run(base_command + [str(current_path)], True)

        lines = baseline_text.splitlines()
        changed_columns = lines[0].split("\t")
        changed_columns[2] = "fast"
        current_path.write_text(
            "\n".join(["\t".join(changed_columns), *lines[1:]]) + "\n",
            encoding="utf-8",
        )
        run(base_command + [str(current_path)], False)

        current_path.write_text("\n".join(lines[1:]) + "\n", encoding="utf-8")
        run(base_command + [str(current_path)], False)

        retired_id = lines[0].split("\t", 1)[0]
        current_path.write_text(
            "\n".join(lines[1:]) + f"\n# retired_field_id\t{retired_id}\n",
            encoding="utf-8",
        )
        run(base_command + [str(current_path)], True)

    print("field registry checker expectations passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
