#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# /// script
# dependencies = ["jsonschema>=4.21"]
# ///
import argparse
import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator


def load_json(path: Path) -> object:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def format_path(path) -> str:
    if not path:
        return "$"
    out = "$"
    for item in path:
        if isinstance(item, int):
            out += f"[{item}]"
        else:
            out += f".{item}"
    return out


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate an edge-healthd snapshot against the JSON schema."
    )
    parser.add_argument("snapshot", help="Path to snapshot JSON file.")
    parser.add_argument(
        "--schema",
        default="schemas/edge.health.state.v1.0.json",
        help="Path to schema JSON file.",
    )
    args = parser.parse_args()

    schema_path = Path(args.schema)
    snapshot_path = Path(args.snapshot)

    if not schema_path.exists():
        print(f"Schema file not found: {schema_path}", file=sys.stderr)
        return 2
    if not snapshot_path.exists():
        print(f"Snapshot file not found: {snapshot_path}", file=sys.stderr)
        return 2

    try:
        schema = load_json(schema_path)
        instance = load_json(snapshot_path)
        validator = Draft202012Validator(schema)
    except Exception as exc:
        print(f"Failed to load or parse JSON: {exc}", file=sys.stderr)
        return 2

    errors = sorted(validator.iter_errors(instance), key=lambda err: list(err.path))
    if errors:
        print(f"Schema validation failed for {snapshot_path}:", file=sys.stderr)
        for error in errors:
            path = format_path(error.absolute_path)
            print(f"- {path}: {error.message}", file=sys.stderr)
        return 1

    print(f"Schema validation OK: {snapshot_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
