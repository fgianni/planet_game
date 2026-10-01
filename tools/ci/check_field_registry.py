#!/usr/bin/env python3
"""Check that stable field-registry IDs remain append-only."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


RETIRED_PREFIX = "# retired_field_id\t"
ATTRIBUTE_NAMES = ("name", "partition", "layout", "dtype", "layers", "units")


def parse_id(text: str, path: Path, line_number: int) -> int:
    try:
        value = int(text, 0)
    except ValueError as error:
        raise ValueError(f"{path}:{line_number}: invalid field ID {text!r}") from error
    if value < 0 or value > 0xFFFF_FFFF:
        raise ValueError(f"{path}:{line_number}: field ID is outside uint32: {text!r}")
    return value


def read_registry(path: Path) -> tuple[dict[int, tuple[str, ...]], set[int]]:
    fields: dict[int, tuple[str, ...]] = {}
    retired: set[int] = set()
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw_line:
            continue
        if raw_line.startswith(RETIRED_PREFIX):
            retired_id = parse_id(raw_line[len(RETIRED_PREFIX) :], path, line_number)
            if retired_id in retired:
                raise ValueError(f"{path}:{line_number}: duplicate retired field ID {retired_id}")
            retired.add(retired_id)
            continue
        if raw_line.startswith("#"):
            continue
        columns = raw_line.split("\t")
        if len(columns) != 7:
            raise ValueError(
                f"{path}:{line_number}: expected 7 tab-separated columns, got {len(columns)}"
            )
        field_id = parse_id(columns[0], path, line_number)
        if field_id in fields:
            raise ValueError(f"{path}:{line_number}: duplicate field ID {field_id}")
        # "scenario": a layer count set per scenario (ADR-0010 §4.1).
        if columns[5] != "scenario":
            try:
                layers = int(columns[5], 10)
            except ValueError as error:
                raise ValueError(
                    f"{path}:{line_number}: invalid layer count {columns[5]!r}"
                ) from error
            if layers <= 0:
                raise ValueError(f"{path}:{line_number}: layer count must be positive")
        fields[field_id] = tuple(columns[1:])
    return fields, retired


def check_registry(baseline_path: Path, current_path: Path) -> int:
    baseline, baseline_retired = read_registry(baseline_path)
    current, current_retired = read_registry(current_path)
    retired = baseline_retired | current_retired
    errors: list[str] = []

    # A retired ID stays retired forever and is never registered again.
    for field_id in sorted(baseline_retired - current_retired):
        errors.append(f"retired field ID {field_id} is no longer declared retired")
    for field_id in sorted(retired & current.keys()):
        errors.append(f"field ID {field_id} is both registered and retired")

    for field_id, baseline_attributes in sorted(baseline.items()):
        current_attributes = current.get(field_id)
        if current_attributes is None:
            if field_id in retired:
                print(f"retired field ID {field_id}: {baseline_attributes[0]}")
            else:
                errors.append(f"baseline field ID {field_id} is missing and not retired")
            continue
        if current_attributes != baseline_attributes:
            differences = [
                f"{name}: {old!r} -> {new!r}"
                for name, old, new in zip(
                    ATTRIBUTE_NAMES, baseline_attributes, current_attributes, strict=True
                )
                if old != new
            ]
            errors.append(f"field ID {field_id} changed ({'; '.join(differences)})")

    # New fields must be added to the baseline in the commit that registers them.
    for field_id in sorted(current.keys() - baseline.keys()):
        errors.append(
            f"field ID {field_id} ({current[field_id][0]}) is not in the baseline; "
            f"add its line to {baseline_path}"
        )

    if errors:
        for error in errors:
            print(f"field registry error: {error}", file=sys.stderr)
        return 1
    print(f"field registry check passed: {len(current)} current, {len(retired)} retired")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("current", type=Path)
    arguments = parser.parse_args()
    try:
        return check_registry(arguments.baseline, arguments.current)
    except (OSError, ValueError) as error:
        print(f"field registry error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
