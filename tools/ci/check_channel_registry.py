#!/usr/bin/env python3
"""Check the append-only semantic-channel registry contract."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


def read(path: Path) -> list[tuple[str, ...]]:
    rows: list[tuple[str, ...]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line:
            continue
        columns = tuple(line.split("\t"))
        if len(columns) != 8 or not columns[0].isdigit() or int(columns[0]) <= 0:
            raise ValueError(f"{path}:{line_number}: expected eight columns beginning with a positive id")
        rows.append(columns)
    if not rows:
        raise ValueError(f"{path}: registry is empty")
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("current", type=Path)
    arguments = parser.parse_args()
    try:
        baseline = read(arguments.baseline)
        current = read(arguments.current)
    except (OSError, ValueError) as error:
        print(f"channel registry error: {error}", file=sys.stderr)
        return 1
    if current[: len(baseline)] != baseline:
        print("channel registry error: existing channel rows changed, moved, or disappeared", file=sys.stderr)
        return 1
    previous = 0
    for row in current:
        identifier = int(row[0])
        if identifier <= previous:
            print("channel registry error: ids must be strictly increasing", file=sys.stderr)
            return 1
        previous = identifier
    print(f"channel registry check passed: {len(current)} channels")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
