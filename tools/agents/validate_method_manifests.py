#!/usr/bin/env python3
"""Validate method manifest files under methods/**/method.yaml."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    import yaml
except ImportError as exc:  # pragma: no cover - dependency issue path
    print("ERROR: PyYAML is required. Install with `python3 -m pip install pyyaml`.", file=sys.stderr)
    raise SystemExit(2) from exc

REQUIRED_TOP_LEVEL_FIELDS = {
    "id",
    "name",
    "domain",
    "status",
    "paper",
    "inputs",
    "outputs",
    "backends",
    "metrics",
    "correctness_tests",
    "benchmarks",
    "known_limitations",
}

ALLOWED_STATUS = {
    "proposed",
    "reference",
    "optimized",
    "gpu",
    "validated",
    "deprecated",
}

ALLOWED_BACKENDS = {
    "cpu_reference",
    "cpu_optimized",
    "gpu_vulkan_compute",
    "gpu_vulkan_graphics",
    "cuda_optional",
    "external_baseline",
}

ALLOWED_ID_PREFIXES = ("geometry.", "rendering.", "physics.")
PLACEHOLDER_PREFIXES = ("TODO:", "TBD", "PLACEHOLDER:")


def _is_placeholder(value: str) -> bool:
    v = value.strip()
    return any(v.startswith(prefix) for prefix in PLACEHOLDER_PREFIXES)


def _load_yaml(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8") as f:
            data = yaml.safe_load(f)
    except yaml.YAMLError as exc:
        raise ValueError(f"invalid YAML ({exc})") from exc

    if not isinstance(data, dict):
        raise ValueError("manifest root must be a YAML mapping/object")
    return data


def _require_list_of_strings(data: dict, key: str, errors: list[str]) -> list[str]:
    value = data.get(key)
    if not isinstance(value, list) or not value:
        errors.append(f"field '{key}' must be a non-empty list")
        return []
    bad = [v for v in value if not isinstance(v, str) or not v.strip()]
    if bad:
        errors.append(f"field '{key}' entries must be non-empty strings")
        return []
    return value


def _validate_paths(entries: list[str], manifest_dir: Path, field: str, errors: list[str]) -> None:
    for entry in entries:
        if _is_placeholder(entry):
            continue
        candidate = (manifest_dir / entry).resolve()
        if not candidate.exists():
            errors.append(
                f"field '{field}' entry '{entry}' does not exist under {manifest_dir} "
                "(or mark as TODO:/TBD/PLACEHOLDER:)"
            )


def validate_manifest(path: Path, strict: bool = False) -> list[str]:
    errors: list[str] = []

    try:
        data = _load_yaml(path)
    except ValueError as exc:
        return [f"{path}: {exc}"]

    missing = sorted(REQUIRED_TOP_LEVEL_FIELDS.difference(data.keys()))
    if missing:
        errors.append(f"missing required fields: {', '.join(missing)}")

    manifest_id = data.get("id")
    if not isinstance(manifest_id, str) or not manifest_id:
        errors.append("field 'id' must be a non-empty string")
    elif strict and not manifest_id.startswith(ALLOWED_ID_PREFIXES):
        allowed = ", ".join(ALLOWED_ID_PREFIXES)
        errors.append(f"field 'id' must start with one of: {allowed}")

    status = data.get("status")
    if status not in ALLOWED_STATUS:
        errors.append(f"field 'status' must be one of: {', '.join(sorted(ALLOWED_STATUS))}")

    backends = _require_list_of_strings(data, "backends", errors)
    for backend in backends:
        if backend not in ALLOWED_BACKENDS:
            errors.append(
                f"field 'backends' contains '{backend}', must be one of: "
                f"{', '.join(sorted(ALLOWED_BACKENDS))}"
            )

    paper = data.get("paper")
    if not isinstance(paper, dict):
        errors.append("field 'paper' must be a mapping/object")
    else:
        for key in ("title", "authors", "year", "doi", "url"):
            if key not in paper:
                errors.append(f"field 'paper.{key}' is required")

    tests = _require_list_of_strings(data, "correctness_tests", errors)
    benches = _require_list_of_strings(data, "benchmarks", errors)
    _validate_paths(tests, path.parent, "correctness_tests", errors)
    _validate_paths(benches, path.parent, "benchmarks", errors)

    return [f"{path}: {e}" for e in errors]


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate IntrinsicEngine method manifests.")
    parser.add_argument("--root", type=Path, default=Path("methods"), help="Methods root directory")
    parser.add_argument("--strict", action="store_true", help="Enable strict validation rules")
    args = parser.parse_args()

    root = args.root
    if not root.exists():
        print(f"ERROR: root path does not exist: {root}", file=sys.stderr)
        return 2

    manifests = sorted(root.rglob("method.yaml"))
    if not manifests:
        print(f"WARNING: no method manifests found under {root}")
        return 0

    all_errors: list[str] = []
    for manifest in manifests:
        all_errors.extend(validate_manifest(manifest, strict=args.strict))

    if all_errors:
        print("Method manifest validation FAILED:")
        for err in all_errors:
            print(f" - {err}")
        return 1

    print(f"Method manifest validation passed for {len(manifests)} file(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
