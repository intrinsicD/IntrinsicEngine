#!/usr/bin/env python3
"""Claim-grade and protected experiment custody using plain repository files."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmark"))

from workflow_evidence import (  # noqa: E402
    PROFILE_ORDER,
    SCHEMA_VERSION,
    _append_jsonl,
    _atomic_write,
    find_task,
    git,
    load_jsonl,
    load_task_metadata,
    relative_path,
    repo_root_from,
    resolve_repo_path,
    sha256_bytes,
    sha256_file,
    strict_json_load,
    strict_yaml_load,
    utc_now,
    yaml_dump,
)
from validate_benchmark_results import (  # noqa: E402
    ContractError as BenchmarkContractError,
    load_json as load_benchmark_json,
    load_manifests as load_benchmark_manifests,
    validate_result as validate_benchmark_result,
)

PROTOCOL_REQUIRED = {
    "schema_version",
    "task_id",
    "protocol_id",
    "profile",
    "state",
    "question",
    "hypothesis",
    "claim_boundary",
    "evidence_phase",
    "claim_eligible",
    "datasets",
    "disjoint_splits",
    "seeds",
    "input_policy",
    "comparators",
    "primary_metrics",
    "statistical_tests",
    "killing_gates",
    "screening_confirmation_policy",
    "resources",
    "command",
    "expected_artifacts",
    "blockers",
    "source",
    "config",
    "environment",
    "implementation",
}
RUN_REQUIRED = {
    "schema_version",
    "task_id",
    "run_id",
    "attempt_id",
    "status",
    "initialized_at",
    "protocol_path",
    "protocol_digest",
    "task_path",
    "task_sha256",
    "source",
    "config",
    "environment",
    "datasets",
    "implementation_digest",
    "cell_journal",
}
CELL_STATES = {"started", "completed", "failed", "missing"}
RUN_STATES = {"initialized", "completed", "failed", "abandoned"}
ATTEMPT_STATES = {"started", "failed", "completed"}
GATE_OPERATORS = {"<=", "<", ">=", ">", "==", "!="}
SUMMARY_STATISTICS = {"mean", "median", "min", "max", "count", "sum"}
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$")


class CustodyError(ValueError):
    pass


def _require_nonempty(data: dict[str, Any], key: str, errors: list[str]) -> None:
    value = data.get(key)
    if not isinstance(value, str) or not value.strip():
        errors.append(f"{key} must be a non-empty string")


def _require_list(data: dict[str, Any], key: str, errors: list[str]) -> list[Any]:
    value = data.get(key)
    if not isinstance(value, list):
        errors.append(f"{key} must be a list")
        return []
    return value


def _canonical_digest(data: dict[str, Any], omitted: set[str]) -> str:
    canonical = {key: value for key, value in data.items() if key not in omitted}
    payload = json.dumps(
        canonical,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
        default=str,
    ).encode("utf-8")
    return sha256_bytes(payload)


def _exclusive_write(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
    except FileExistsError as exc:
        raise CustodyError(f"refusing to overwrite consumed identity: {path}") from exc
    with os.fdopen(descriptor, "wb") as handle:
        handle.write(payload)
        handle.flush()
        os.fsync(handle.fileno())


def protocol_digest(protocol: dict[str, Any]) -> str:
    return _canonical_digest(protocol, {"protocol_digest", "frozen_at"})


def implementation_digest(protocol: dict[str, Any]) -> str:
    implementation = protocol.get("implementation")
    return sha256_bytes(
        json.dumps(
            implementation,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
        ).encode("utf-8")
    )


def _validate_sealed_file(
    repo_root: Path,
    entry: object,
    location: str,
    errors: list[str],
    path_key: str = "path",
) -> None:
    if not isinstance(entry, dict):
        errors.append(f"{location} must be a mapping")
        return
    try:
        path = resolve_repo_path(repo_root, entry.get(path_key))
    except ValueError as exc:
        errors.append(f"{location}: {exc}")
        return
    if not path.is_file():
        errors.append(f"{location}: sealed file is missing: {entry.get(path_key)}")
        return
    expected = entry.get("sha256")
    actual = sha256_file(path)
    if expected != actual:
        errors.append(
            f"{location}: hash mismatch for {entry.get(path_key)} "
            f"(expected {expected}, got {actual})"
        )


def validate_protocol_data(
    repo_root: Path,
    protocol: dict[str, Any],
    *,
    require_frozen: bool,
) -> list[str]:
    errors: list[str] = []
    missing = sorted(PROTOCOL_REQUIRED - protocol.keys())
    if missing:
        errors.append(f"missing protocol fields: {', '.join(missing)}")
    if protocol.get("schema_version") != SCHEMA_VERSION:
        errors.append("schema_version must equal 1")
    for key in (
        "task_id",
        "protocol_id",
        "question",
        "hypothesis",
        "claim_boundary",
        "input_policy",
        "screening_confirmation_policy",
    ):
        _require_nonempty(protocol, key, errors)
    if protocol.get("profile") not in {"claim-grade", "protected"}:
        errors.append("profile must be claim-grade or protected")
    state = protocol.get("state")
    if state not in {"ready", "frozen"}:
        errors.append("state must be ready or frozen")
    if require_frozen and state != "frozen":
        errors.append("protocol must be frozen")
    phase = protocol.get("evidence_phase")
    if phase not in {"scratch", "screening", "confirmation"}:
        errors.append("evidence_phase must be scratch, screening, or confirmation")
    if type(protocol.get("claim_eligible")) is not bool:
        errors.append("claim_eligible must be a boolean")
    if phase == "scratch" and protocol.get("claim_eligible") is True:
        errors.append("scratch evidence cannot be claim eligible")

    datasets = _require_list(protocol, "datasets", errors)
    if not datasets:
        errors.append("datasets must not be empty")
    dataset_ids: set[str] = set()
    split_names: set[str] = set()
    for index, entry in enumerate(datasets):
        if not isinstance(entry, dict):
            errors.append(f"datasets[{index}] must be a mapping")
            continue
        for key in ("id", "path", "sha256", "split"):
            if not isinstance(entry.get(key), str) or not entry[key].strip():
                errors.append(f"datasets[{index}].{key} must be non-empty")
        dataset_id = entry.get("id")
        if isinstance(dataset_id, str):
            if dataset_id in dataset_ids:
                errors.append(f"duplicate dataset id: {dataset_id}")
            dataset_ids.add(dataset_id)
        split = entry.get("split")
        if isinstance(split, str):
            split_names.add(split)
        _validate_sealed_file(repo_root, entry, f"datasets[{index}]", errors)

    splits = _require_list(protocol, "disjoint_splits", errors)
    if phase in {"screening", "confirmation"} and not splits:
        errors.append("official evidence requires declared disjoint splits")
    for index, entry in enumerate(splits):
        if not isinstance(entry, dict):
            errors.append(f"disjoint_splits[{index}] must be a mapping")
            continue
        for key in ("left", "right", "method"):
            if not isinstance(entry.get(key), str) or not entry[key].strip():
                errors.append(f"disjoint_splits[{index}].{key} must be non-empty")
        for key in ("left", "right"):
            if entry.get(key) not in split_names:
                errors.append(
                    f"disjoint_splits[{index}].{key} names unknown split "
                    f"{entry.get(key)!r}"
                )
        if entry.get("left") == entry.get("right"):
            errors.append(f"disjoint_splits[{index}] must name distinct splits")

    seeds = _require_list(protocol, "seeds", errors)
    if not seeds or any(type(seed) is not int for seed in seeds):
        errors.append("seeds must contain at least one integer")
    if len(seeds) != len(set(seeds)):
        errors.append("seeds must be unique")

    comparators = _require_list(protocol, "comparators", errors)
    if not comparators:
        errors.append("comparators must not be empty")
    for index, entry in enumerate(comparators):
        if not isinstance(entry, dict):
            errors.append(f"comparators[{index}] must be a mapping")
            continue
        for key in ("id", "budget"):
            if key not in entry:
                errors.append(f"comparators[{index}] missing {key}")

    metrics = _require_list(protocol, "primary_metrics", errors)
    metric_names: set[str] = set()
    if not metrics:
        errors.append("primary_metrics must not be empty")
    for index, entry in enumerate(metrics):
        if not isinstance(entry, dict):
            errors.append(f"primary_metrics[{index}] must be a mapping")
            continue
        for key in ("name", "direction", "unit", "statistical_unit"):
            if not isinstance(entry.get(key), str) or not entry[key].strip():
                errors.append(f"primary_metrics[{index}].{key} must be non-empty")
        if entry.get("direction") not in {"minimize", "maximize", "target"}:
            errors.append(f"primary_metrics[{index}].direction is invalid")
        name = entry.get("name")
        if isinstance(name, str):
            metric_names.add(name)

    tests = _require_list(protocol, "statistical_tests", errors)
    if not tests:
        errors.append("statistical_tests must not be empty")
    gates = _require_list(protocol, "killing_gates", errors)
    if not gates:
        errors.append("killing_gates must not be empty")
    for index, gate in enumerate(gates):
        if not isinstance(gate, dict):
            errors.append(f"killing_gates[{index}] must be a mapping")
            continue
        if gate.get("metric") not in metric_names:
            errors.append(f"killing_gates[{index}] names undeclared primary metric")
        if gate.get("operator") not in GATE_OPERATORS:
            errors.append(f"killing_gates[{index}] has invalid operator")
        if type(gate.get("threshold")) not in {int, float} or not math.isfinite(
            gate.get("threshold", math.nan)
        ):
            errors.append(f"killing_gates[{index}].threshold must be finite numeric")

    command = protocol.get("command")
    if (
        not isinstance(command, list)
        or not command
        or any(not isinstance(item, str) or not item for item in command)
    ):
        errors.append("command must be a non-empty argv string list")
    if not isinstance(protocol.get("resources"), dict):
        errors.append("resources must be a mapping")
    _require_list(protocol, "expected_artifacts", errors)
    _require_list(protocol, "blockers", errors)

    for key in ("source", "config", "environment"):
        if not isinstance(protocol.get(key), dict):
            errors.append(f"{key} must be a mapping")
    source = protocol.get("source", {})
    revision = source.get("revision") if isinstance(source, dict) else None
    if not isinstance(revision, str) or not revision:
        errors.append("source.revision must be non-empty")
    if type(source.get("clean")) is not bool:
        errors.append("source.clean must be a boolean")
    if protocol.get("claim_eligible"):
        if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision):
            errors.append(
                "claim-eligible source revision must be an exact 40-hex commit"
            )
        if source.get("clean") is not True:
            errors.append("claim-eligible source must be declared clean")
    for key in ("config", "environment"):
        entry = protocol.get(key)
        if isinstance(entry, dict):
            _validate_sealed_file(repo_root, entry, key, errors)

    implementation = _require_list(protocol, "implementation", errors)
    if not implementation:
        errors.append("implementation must not be empty")
    for index, entry in enumerate(implementation):
        _validate_sealed_file(repo_root, entry, f"implementation[{index}]", errors)

    if state == "frozen":
        expected = protocol.get("protocol_digest")
        if expected != protocol_digest(protocol):
            errors.append("frozen protocol_digest does not match protocol content")
        _require_nonempty(protocol, "frozen_at", errors)
    return errors


def freeze_protocol(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    path = resolve_repo_path(repo_root, args.protocol)
    protocol = strict_yaml_load(path)
    if protocol.get("state") == "frozen":
        raise CustodyError(
            "protocol is already frozen; create a new protocol/run identity"
        )
    protocol["state"] = "ready"
    errors = validate_protocol_data(repo_root, protocol, require_frozen=False)
    if errors:
        raise CustodyError("; ".join(errors))
    protocol["state"] = "frozen"
    protocol["frozen_at"] = utc_now()
    protocol["protocol_digest"] = protocol_digest(protocol)
    _atomic_write(path, yaml_dump(protocol).encode("utf-8"), replace=True)
    print(f"Frozen {relative_path(repo_root, path)} at {protocol['protocol_digest']}.")
    return 0


def _sealed_copy(entry: dict[str, Any]) -> dict[str, Any]:
    return {
        key: entry[key] for key in ("id", "path", "sha256", "split") if key in entry
    }


def _ensure_id(value: str, name: str) -> None:
    if not ID_RE.fullmatch(value):
        raise CustodyError(f"{name} has invalid characters: {value}")


def _repo_dirty(repo_root: Path) -> bool:
    return bool(git(repo_root, "status", "--porcelain"))


def init_run(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    protocol_path = resolve_repo_path(repo_root, args.protocol)
    protocol = strict_yaml_load(protocol_path)
    errors = validate_protocol_data(repo_root, protocol, require_frozen=True)
    if errors:
        raise CustodyError("; ".join(errors))
    _ensure_id(args.run_id, "run_id")
    _ensure_id(args.attempt_id, "attempt_id")

    task_id = protocol["task_id"]
    task_path = find_task(repo_root, task_id)
    _, task_metadata = load_task_metadata(task_path)
    expected_profile = task_metadata.get("workflow_profile")
    if expected_profile != protocol.get("profile"):
        raise CustodyError("protocol profile does not match task workflow profile")
    if PROFILE_ORDER.get(expected_profile, -1) < PROFILE_ORDER["claim-grade"]:
        raise CustodyError("task is not claim-grade or protected")

    source = protocol["source"]
    source_probe = subprocess.run(
        [
            "git",
            "-C",
            str(repo_root),
            "cat-file",
            "-e",
            f"{source.get('revision')}^{{commit}}",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if source_probe.returncode != 0:
        raise CustodyError("protocol source revision does not resolve to a commit")
    if protocol.get("claim_eligible") and _repo_dirty(repo_root):
        raise CustodyError(
            "official claim-grade initialization requires a clean worktree"
        )

    run_parent = resolve_repo_path(repo_root, args.run_parent)
    run_root = run_parent / args.run_id
    if run_root.exists():
        raise CustodyError(f"canonical run root already exists: {run_root}")
    if run_parent.exists():
        for candidate in run_parent.glob("*/run.yaml"):
            existing = strict_yaml_load(candidate)
            if existing.get("run_id") == args.run_id:
                raise CustodyError(f"duplicate run_id in {candidate}")
            if existing.get("attempt_id") == args.attempt_id:
                raise CustodyError(f"duplicate attempt_id in {candidate}")
    run_root.mkdir(parents=True, exist_ok=False)

    run = {
        "schema_version": SCHEMA_VERSION,
        "task_id": task_id,
        "run_id": args.run_id,
        "attempt_id": args.attempt_id,
        "status": "initialized",
        "initialized_at": utc_now(),
        "claim_eligible": protocol["claim_eligible"],
        "protocol_path": relative_path(repo_root, protocol_path),
        "protocol_digest": protocol["protocol_digest"],
        "task_path": relative_path(repo_root, task_path),
        "task_sha256": sha256_file(task_path),
        "source": dict(source),
        "config": dict(protocol["config"]),
        "environment": dict(protocol["environment"]),
        "datasets": [_sealed_copy(entry) for entry in protocol["datasets"]],
        "implementation_digest": implementation_digest(protocol),
        "command": list(protocol["command"]),
        "cell_journal": relative_path(repo_root, run_root / "cells.jsonl"),
        "bundle": relative_path(repo_root, run_root / "bundle.yaml"),
        "audit": relative_path(repo_root, run_root / "audit.json"),
    }
    _atomic_write(run_root / "run.yaml", yaml_dump(run).encode("utf-8"))
    print(f"Initialized non-overwriting run {relative_path(repo_root, run_root)}.")
    return 0


def _load_run(repo_root: Path, run_root_value: str) -> tuple[Path, dict[str, Any]]:
    run_root = resolve_repo_path(repo_root, run_root_value)
    run_path = run_root / "run.yaml"
    if not run_path.is_file():
        raise CustodyError(f"missing run.yaml: {run_path}")
    return run_root, strict_yaml_load(run_path)


def _validate_run_bindings(
    repo_root: Path, run_root: Path, run: dict[str, Any]
) -> list[str]:
    errors: list[str] = []
    missing = sorted(RUN_REQUIRED - run.keys())
    if missing:
        errors.append(f"run missing fields: {', '.join(missing)}")
    if run.get("schema_version") != SCHEMA_VERSION:
        errors.append("run schema_version must equal 1")
    if run.get("status") not in RUN_STATES:
        errors.append("run status is invalid")
    try:
        protocol_path = resolve_repo_path(repo_root, run.get("protocol_path"))
        protocol = strict_yaml_load(protocol_path)
        protocol_errors = validate_protocol_data(
            repo_root, protocol, require_frozen=True
        )
        errors.extend(f"protocol: {error}" for error in protocol_errors)
        if protocol.get("protocol_digest") != run.get("protocol_digest"):
            errors.append("run binds a stale/changed protocol digest")
        if implementation_digest(protocol) != run.get("implementation_digest"):
            errors.append("run binds a stale implementation digest")
        if run.get("source") != protocol.get("source"):
            errors.append("run source identity differs from frozen protocol")
    except (ValueError, CustodyError) as exc:
        errors.append(str(exc))
        protocol = {}
    try:
        task_path = find_task(repo_root, str(run.get("task_id")))
        if sha256_file(task_path) != run.get("task_sha256"):
            errors.append("task changed after official run initialization")
    except ValueError as exc:
        errors.append(str(exc))
    for key in ("config", "environment"):
        entry = run.get(key)
        _validate_sealed_file(repo_root, entry, f"run.{key}", errors)
    for index, entry in enumerate(run.get("datasets", [])):
        _validate_sealed_file(repo_root, entry, f"run.datasets[{index}]", errors)
    if run.get("claim_eligible"):
        source = run.get("source", {})
        if source.get("clean") is not True:
            errors.append("claim-eligible run source is not clean")
    try:
        journal_path = resolve_repo_path(repo_root, run.get("cell_journal"))
        if journal_path.parent != run_root:
            errors.append("cell journal must live in canonical run root")
        if journal_path.exists():
            _validate_cells(load_jsonl(journal_path), errors)
    except ValueError as exc:
        errors.append(str(exc))
    return errors


def _validate_cells(records: list[dict[str, Any]], errors: list[str]) -> None:
    last_state: dict[str, str] = {}
    for index, record in enumerate(records):
        key = record.get("cell_key")
        state = record.get("state")
        if not isinstance(key, str) or not ID_RE.fullmatch(key):
            errors.append(f"cell[{index}] has invalid cell_key")
            continue
        if state not in CELL_STATES:
            errors.append(f"cell[{index}] has invalid state")
            continue
        prior = last_state.get(key)
        if prior is None and state not in {"started", "missing"}:
            errors.append(f"cell {key} must begin as started or missing")
        elif prior is not None:
            if prior != "started":
                errors.append(f"cell {key} was reused after terminal state {prior}")
            elif state == "started":
                errors.append(f"cell {key} has duplicate started state")
        if state == "failed" and not record.get("error"):
            errors.append(f"failed cell {key} must retain its error")
        if state == "missing" and not record.get("reason"):
            errors.append(f"missing cell {key} must retain its reason")
        last_state[key] = state


def append_cell(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    run_root, run = _load_run(repo_root, args.run_root)
    errors = _validate_run_bindings(repo_root, run_root, run)
    if errors:
        raise CustodyError("; ".join(errors))
    _ensure_id(args.cell_key, "cell_key")
    if args.state not in CELL_STATES:
        raise CustodyError(f"invalid cell state: {args.state}")
    journal = run_root / "cells.jsonl"
    records = load_jsonl(journal) if journal.exists() else []
    states = [
        record["state"] for record in records if record.get("cell_key") == args.cell_key
    ]
    if not states and args.state not in {"started", "missing"}:
        raise CustodyError("new cell must begin as started or missing")
    if states:
        if states[-1] != "started":
            raise CustodyError(f"cell key is already terminal: {args.cell_key}")
        if args.state == "started":
            raise CustodyError(f"cell key is already started: {args.cell_key}")
    if args.state == "failed" and not args.error:
        raise CustodyError("failed cell requires --error")
    if args.state == "missing" and not args.reason:
        raise CustodyError("missing cell requires --reason")
    artifacts: list[dict[str, str]] = []
    for value in args.artifact:
        path = resolve_repo_path(repo_root, value)
        if not path.is_file():
            raise CustodyError(f"cell artifact is missing: {value}")
        artifacts.append({"path": value, "sha256": sha256_file(path)})
    record = {
        "schema_version": SCHEMA_VERSION,
        "recorded_at": utc_now(),
        "cell_key": args.cell_key,
        "state": args.state,
        "error": args.error,
        "reason": args.reason,
        "artifacts": artifacts,
    }
    _append_jsonl(journal, record)
    print(
        f"Appended {args.cell_key}:{args.state} to {relative_path(repo_root, journal)}."
    )
    return 0


def _load_raw_rows(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []

    def unique_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate key {key!r}")
            result[key] = value
        return result

    def reject_constant(value: str) -> None:
        raise ValueError(f"non-standard constant {value}")

    for index, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if not line.strip():
            continue
        try:
            row = json.loads(
                line,
                object_pairs_hook=unique_pairs,
                parse_constant=reject_constant,
            )
        except (json.JSONDecodeError, ValueError) as exc:
            raise CustodyError(f"{path}:{index}: invalid raw row ({exc})") from exc
        if not isinstance(row, dict):
            raise CustodyError(f"{path}:{index}: raw row must be an object")
        rows.append(row)
    if not rows:
        raise CustodyError(f"raw row file is empty: {path}")
    return rows


def _summary_value(values: list[float], statistic: str) -> float | int:
    if statistic == "mean":
        return statistics.fmean(values)
    if statistic == "median":
        return statistics.median(values)
    if statistic == "min":
        return min(values)
    if statistic == "max":
        return max(values)
    if statistic == "count":
        return len(values)
    if statistic == "sum":
        return math.fsum(values)
    raise CustodyError(f"unsupported summary statistic: {statistic}")


def _compare(value: float, operator: str, threshold: float) -> bool:
    return {
        "<=": value <= threshold,
        "<": value < threshold,
        ">=": value >= threshold,
        ">": value > threshold,
        "==": value == threshold,
        "!=": value != threshold,
    }[operator]


def _flatten_benchmark_metrics(
    value: object,
    prefix: str,
    row: dict[str, float | int],
) -> None:
    if type(value) in {int, float}:
        if not math.isfinite(value):
            raise CustodyError(f"benchmark metric {prefix} is not finite")
        row[prefix] = value
        return
    if isinstance(value, dict):
        for key, item in value.items():
            child = f"{prefix}.{key}" if prefix else str(key)
            _flatten_benchmark_metrics(item, child, row)
        return
    if isinstance(value, list):
        for index, item in enumerate(value):
            child = f"{prefix}[{index}]"
            _flatten_benchmark_metrics(item, child, row)
        return
    raise CustodyError(f"benchmark metric {prefix} is not numeric")


def _benchmark_bundle_input(
    repo_root: Path,
    result_value: str,
    manifests_value: str,
) -> tuple[dict[str, Any], dict[str, Any]]:
    result_path = resolve_repo_path(repo_root, result_value)
    manifests_root = resolve_repo_path(repo_root, manifests_value)
    if not result_path.is_file():
        raise CustodyError(f"benchmark result is missing: {result_value}")
    if not manifests_root.is_dir():
        raise CustodyError(f"benchmark manifest root is missing: {manifests_value}")
    try:
        manifests = load_benchmark_manifests(manifests_root)
        identity, errors = validate_benchmark_result(
            result_path,
            manifests,
            manifests_root,
        )
        result = load_benchmark_json(result_path)
    except BenchmarkContractError as exc:
        raise CustodyError(f"invalid benchmark result: {exc}") from exc
    if errors:
        raise CustodyError("invalid benchmark result: " + "; ".join(errors))
    if identity is None:
        raise CustodyError("benchmark result has no valid run/attempt identity")
    row: dict[str, Any] = {
        "benchmark_id": identity[0],
        "run_id": identity[1],
        "attempt_id": identity[2],
    }
    _flatten_benchmark_metrics(result["metrics"], "", row)
    binding = {
        "path": relative_path(repo_root, result_path),
        "sha256": sha256_file(result_path),
        "manifests_root": relative_path(repo_root, manifests_root),
        "benchmark_id": identity[0],
        "run_id": identity[1],
        "attempt_id": identity[2],
        "status": result["status"],
        "claim_eligible": result["claim_eligible"],
    }
    return binding, row


def create_bundle(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    run_root, run = _load_run(repo_root, args.run_root)
    errors = _validate_run_bindings(repo_root, run_root, run)
    if errors:
        raise CustodyError("; ".join(errors))
    benchmark_binding: dict[str, Any] | None = None
    if args.benchmark_result:
        benchmark_binding, benchmark_row = _benchmark_bundle_input(
            repo_root,
            args.benchmark_result,
            args.benchmark_manifests_root,
        )
        raw_path = run_root / "benchmark_raw_rows.jsonl"
        _atomic_write(
            raw_path,
            (
                json.dumps(
                    benchmark_row,
                    ensure_ascii=False,
                    separators=(",", ":"),
                    sort_keys=True,
                )
                + "\n"
            ).encode("utf-8"),
        )
        raw_value = relative_path(repo_root, raw_path)
    else:
        raw_path = resolve_repo_path(repo_root, args.raw_rows)
        raw_value = args.raw_rows
    rows = _load_raw_rows(raw_path)
    summaries: list[dict[str, Any]] = []
    for spec in args.summary:
        parts = spec.split(":")
        if len(parts) != 3:
            raise CustodyError("--summary expects NAME:COLUMN:STATISTIC")
        name, column, statistic = parts
        if statistic not in SUMMARY_STATISTICS:
            raise CustodyError(f"invalid summary statistic: {statistic}")
        values: list[float] = []
        for index, row in enumerate(rows):
            value = row.get(column)
            if type(value) not in {int, float} or not math.isfinite(value):
                raise CustodyError(
                    f"raw row {index} column {column} is not finite numeric"
                )
            values.append(float(value))
        summaries.append(
            {
                "name": name,
                "column": column,
                "statistic": statistic,
                "value": _summary_value(values, statistic),
            }
        )
    summary_by_name = {entry["name"]: entry["value"] for entry in summaries}
    protocol = strict_yaml_load(resolve_repo_path(repo_root, run["protocol_path"]))
    gates: list[dict[str, Any]] = []
    for gate in protocol["killing_gates"]:
        metric = gate["metric"]
        if metric not in summary_by_name:
            raise CustodyError(f"missing summary for killing gate metric: {metric}")
        passed = _compare(
            float(summary_by_name[metric]),
            gate["operator"],
            float(gate["threshold"]),
        )
        gates.append({**gate, "observed": summary_by_name[metric], "passed": passed})

    links: list[dict[str, str]] = []
    for value in args.link:
        path = resolve_repo_path(repo_root, value)
        if not path.exists():
            raise CustodyError(f"bundle link is missing: {value}")
        links.append(
            {"path": value, "sha256": sha256_file(path) if path.is_file() else ""}
        )
    previews: list[dict[str, str]] = []
    for value in args.preview:
        path = resolve_repo_path(repo_root, value)
        if not path.is_file():
            raise CustodyError(f"preview/readback is missing: {value}")
        previews.append({"path": value, "sha256": sha256_file(path)})
    if args.visual and not previews:
        raise CustodyError("visual bundle requires at least one preview/readback")

    smoke_receipts = list(args.smoke_receipt)
    if not smoke_receipts:
        raise CustodyError("bundle requires at least one replay/view smoke receipt")
    for value in smoke_receipts:
        path = resolve_repo_path(repo_root, value)
        if not path.is_file():
            raise CustodyError(f"smoke receipt is missing: {value}")

    bundle = {
        "schema_version": SCHEMA_VERSION,
        "task_id": run["task_id"],
        "run_id": run["run_id"],
        "attempt_id": run["attempt_id"],
        "created_at": utc_now(),
        "protocol_digest": run["protocol_digest"],
        "provenance": {
            "source": run["source"],
            "task_sha256": run["task_sha256"],
            "implementation_digest": run["implementation_digest"],
            "datasets": run["datasets"],
        },
        "resolved_config": run["config"],
        "environment": run["environment"],
        "raw_rows": {
            "path": raw_value,
            "sha256": sha256_file(raw_path),
            "row_count": len(rows),
        },
        "summaries": summaries,
        "gates": gates,
        "diagnostics": list(args.diagnostic),
        "links": links,
        "visual": bool(args.visual),
        "previews": previews,
        "replay_command": list(args.replay_command),
        "view_command": list(args.view_command),
        "smoke_receipts": smoke_receipts,
        "claim_authorized": False,
    }
    if benchmark_binding is not None:
        bundle["benchmark_result"] = benchmark_binding
    if not bundle["replay_command"] or not bundle["view_command"]:
        raise CustodyError("bundle requires exact replay and view commands")
    out = run_root / "bundle.yaml"
    _atomic_write(out, yaml_dump(bundle).encode("utf-8"))
    print(f"Created portable bundle {relative_path(repo_root, out)}.")
    return 0


def _validate_bundle(
    repo_root: Path,
    run_root: Path,
    run: dict[str, Any],
    bundle: dict[str, Any],
) -> tuple[list[str], list[dict[str, Any]], list[dict[str, Any]]]:
    errors: list[str] = []
    if bundle.get("schema_version") != SCHEMA_VERSION:
        errors.append("bundle schema_version must equal 1")
    for key in ("task_id", "run_id", "attempt_id", "protocol_digest"):
        if bundle.get(key) != run.get(key):
            errors.append(f"bundle {key} does not match run")
    try:
        raw = bundle.get("raw_rows", {})
        raw_path = resolve_repo_path(repo_root, raw.get("path"))
        if sha256_file(raw_path) != raw.get("sha256"):
            errors.append("raw row hash mismatch")
        rows = _load_raw_rows(raw_path)
        if len(rows) != raw.get("row_count"):
            errors.append("raw row count mismatch")
    except (ValueError, CustodyError, OSError) as exc:
        errors.append(str(exc))
        rows = []
    recomputed: list[dict[str, Any]] = []
    for index, summary in enumerate(bundle.get("summaries", [])):
        if not isinstance(summary, dict):
            errors.append(f"summaries[{index}] must be a mapping")
            continue
        column = summary.get("column")
        statistic = summary.get("statistic")
        try:
            values = []
            for row in rows:
                value = row.get(column)
                if type(value) not in {int, float} or not math.isfinite(value):
                    raise CustodyError(
                        f"column {column} contains non-finite/non-numeric data"
                    )
                values.append(float(value))
            actual = _summary_value(values, statistic)
            expected = summary.get("value")
            if type(expected) not in {int, float} or not math.isclose(
                float(expected), float(actual), rel_tol=1e-12, abs_tol=1e-12
            ):
                errors.append(
                    f"summary {summary.get('name')} is inconsistent with raw rows"
                )
            recomputed.append({**summary, "value": actual})
        except (CustodyError, TypeError) as exc:
            errors.append(f"summaries[{index}]: {exc}")
    summary_values = {entry.get("name"): entry.get("value") for entry in recomputed}
    recomputed_gates: list[dict[str, Any]] = []
    for index, gate in enumerate(bundle.get("gates", [])):
        if not isinstance(gate, dict):
            errors.append(f"gates[{index}] must be a mapping")
            continue
        try:
            observed = float(summary_values[gate["metric"]])
            passed = _compare(observed, gate["operator"], float(gate["threshold"]))
            if gate.get("passed") is not passed or not math.isclose(
                float(gate.get("observed")), observed, rel_tol=1e-12, abs_tol=1e-12
            ):
                errors.append(f"gate {gate.get('metric')} disposition is inconsistent")
            recomputed_gates.append({**gate, "observed": observed, "passed": passed})
        except (KeyError, TypeError, ValueError) as exc:
            errors.append(f"gates[{index}] cannot be recomputed: {exc}")
    for collection in ("links", "previews"):
        for index, entry in enumerate(bundle.get(collection, [])):
            try:
                path = resolve_repo_path(repo_root, entry.get("path"))
                if not path.exists():
                    raise CustodyError(f"missing path {entry.get('path')}")
                if path.is_file() and sha256_file(path) != entry.get("sha256"):
                    raise CustodyError(f"hash mismatch {entry.get('path')}")
            except (ValueError, CustodyError) as exc:
                errors.append(f"{collection}[{index}]: {exc}")
    if bundle.get("visual") is True and not bundle.get("previews"):
        errors.append("visual bundle is missing preview/readback")
    if not bundle.get("replay_command") or not bundle.get("view_command"):
        errors.append("bundle is missing replay/view command")
    for value in bundle.get("smoke_receipts", []):
        try:
            path = resolve_repo_path(repo_root, value)
            receipt = strict_json_load(path)
            if receipt.get("exit_code") != 0:
                errors.append(f"smoke receipt failed: {value}")
        except ValueError as exc:
            errors.append(str(exc))
    if not bundle.get("smoke_receipts"):
        errors.append("bundle is missing smoke receipts")
    benchmark = bundle.get("benchmark_result")
    if benchmark is not None:
        if not isinstance(benchmark, dict):
            errors.append("benchmark_result must be a mapping")
        else:
            try:
                binding, benchmark_row = _benchmark_bundle_input(
                    repo_root,
                    benchmark.get("path"),
                    benchmark.get("manifests_root"),
                )
                if binding != benchmark:
                    errors.append("benchmark result binding is stale or inconsistent")
                if rows != [benchmark_row]:
                    errors.append(
                        "raw rows do not exactly derive from benchmark metrics"
                    )
            except (CustodyError, ValueError) as exc:
                errors.append(f"benchmark_result: {exc}")
    return errors, recomputed, recomputed_gates


def audit_bundle(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    run_root, run = _load_run(repo_root, args.run_root)
    run_errors = _validate_run_bindings(repo_root, run_root, run)
    if run_errors:
        raise CustodyError("; ".join(run_errors))
    if args.auditor == args.driver:
        raise CustodyError("auditor label must differ from driver label")
    bundle_path = run_root / "bundle.yaml"
    bundle = strict_yaml_load(bundle_path)
    errors, summaries, gates = _validate_bundle(repo_root, run_root, run, bundle)
    disposition = "accepted" if not errors else "rejected"
    receipt = {
        "schema_version": SCHEMA_VERSION,
        "task_id": run["task_id"],
        "run_id": run["run_id"],
        "attempt_id": run["attempt_id"],
        "audited_at": utc_now(),
        "auditor_label": args.auditor,
        "driver_label": args.driver,
        "identity_assurance": "cooperative-label-only",
        "audited_revision": args.revision,
        "bundle_path": relative_path(repo_root, bundle_path),
        "bundle_sha256": sha256_file(bundle_path),
        "recomputed_summaries": summaries,
        "recomputed_gates": gates,
        "errors": errors,
        "disposition": disposition,
        "claim_authorized": False,
    }
    out = run_root / "audit.json"
    _atomic_write(
        out,
        (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    print(f"Audit {disposition}: {relative_path(repo_root, out)}.")
    return 0 if not errors else 1


def prospective_review(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    protocol_path = resolve_repo_path(repo_root, args.protocol)
    protocol = strict_yaml_load(protocol_path)
    errors = validate_protocol_data(repo_root, protocol, require_frozen=True)
    if errors:
        raise CustodyError("; ".join(errors))
    if protocol.get("profile") != "protected":
        raise CustodyError("prospective review is gated to protected profile")
    if args.reviewer == args.driver:
        raise CustodyError("prospective reviewer label must differ from driver")
    if args.protected_interactions != 0 or args.private_draws != 0:
        raise CustodyError(
            "prospective review must be result-free with zero protected access"
        )
    if args.review_kind == "independent" and not args.source_coverage:
        raise CustodyError("independent prospective review requires source coverage")
    record = {
        "schema_version": SCHEMA_VERSION,
        "task_id": protocol["task_id"],
        "recorded_at": utc_now(),
        "reviewer_label": args.reviewer,
        "driver_label": args.driver,
        "review_kind": args.review_kind,
        "identity_assurance": "cooperative-label-only",
        "protocol_digest": protocol["protocol_digest"],
        "implementation_digest": implementation_digest(protocol),
        "review_method": args.review_method,
        "review_boundary": args.review_boundary,
        "source_coverage": list(args.source_coverage),
        "protected_interactions": 0,
        "private_draws": 0,
        "disposition": args.disposition,
        "findings": list(args.finding),
        "authorizes_launch": False,
    }
    out = protocol_path.parent / "prospective_review.json"
    _atomic_write(
        out,
        (json.dumps(record, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    print(
        f"Recorded result-free prospective review at {relative_path(repo_root, out)}."
    )
    return 0


def authorize_protected(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    protocol_path = resolve_repo_path(repo_root, args.protocol)
    protocol = strict_yaml_load(protocol_path)
    errors = validate_protocol_data(repo_root, protocol, require_frozen=True)
    if errors:
        raise CustodyError("; ".join(errors))
    if protocol.get("profile") != "protected":
        raise CustodyError("launch authorization is gated to protected profile")
    review_path = protocol_path.parent / "prospective_review.json"
    review = strict_json_load(review_path)
    if review.get("review_kind") != "independent":
        raise CustodyError("machine rehearsal cannot authorize protected launch")
    if review.get("disposition") != "no-blocker":
        raise CustodyError("prospective review did not report no-blocker")
    if review.get("authorizes_launch") is not False:
        raise CustodyError("prospective review and launch authorization are conflated")
    if args.authorizer == review.get("reviewer_label"):
        raise CustodyError("reviewer cannot self-authorize the protected run")
    for key, expected in (
        ("protocol_digest", protocol["protocol_digest"]),
        ("implementation_digest", implementation_digest(protocol)),
    ):
        if review.get(key) != expected:
            raise CustodyError(f"prospective review has stale {key}")
    if review.get("protected_interactions") != 0 or review.get("private_draws") != 0:
        raise CustodyError("prospective review was not result-free")
    authorization = {
        "schema_version": SCHEMA_VERSION,
        "task_id": protocol["task_id"],
        "authorized_at": utc_now(),
        "authorizer_label": args.authorizer,
        "reviewer_label": review["reviewer_label"],
        "protocol_digest": protocol["protocol_digest"],
        "implementation_digest": implementation_digest(protocol),
        "review_sha256": sha256_file(review_path),
        "authorization_boundary": args.authorization_boundary,
        "launch_authorized": True,
    }
    out = protocol_path.parent / "authorization.json"
    _atomic_write(
        out,
        (json.dumps(authorization, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    print(
        f"Recorded separate protected authorization at {relative_path(repo_root, out)}."
    )
    return 0


def _validate_protected_authority(
    repo_root: Path, protocol_path: Path, protocol: dict[str, Any]
) -> list[str]:
    errors: list[str] = []
    review_path = protocol_path.parent / "prospective_review.json"
    authorization_path = protocol_path.parent / "authorization.json"
    try:
        review = strict_json_load(review_path)
        authorization = strict_json_load(authorization_path)
    except ValueError as exc:
        return [str(exc)]
    protocol_hash = protocol.get("protocol_digest")
    impl_hash = implementation_digest(protocol)
    if review.get("protocol_digest") != protocol_hash:
        errors.append("prospective review protocol digest is stale")
    if review.get("implementation_digest") != impl_hash:
        errors.append("prospective review implementation digest is stale")
    if review.get("review_kind") != "independent":
        errors.append("prospective review is not independent")
    if review.get("reviewer_label") == review.get("driver_label"):
        errors.append("prospective reviewer label equals driver label")
    for key in ("review_method", "review_boundary"):
        if not isinstance(review.get(key), str) or not review[key].strip():
            errors.append(f"prospective review is missing {key}")
    if (
        not isinstance(review.get("source_coverage"), list)
        or not review["source_coverage"]
    ):
        errors.append("prospective review is missing source coverage")
    if review.get("disposition") != "no-blocker":
        errors.append("prospective review did not reach no-blocker")
    if review.get("authorizes_launch") is not False:
        errors.append("prospective review conflates review with authorization")
    if review.get("protected_interactions") != 0 or review.get("private_draws") != 0:
        errors.append("prospective review accessed protected results")
    if authorization.get("protocol_digest") != protocol_hash:
        errors.append("authorization protocol digest is stale")
    if authorization.get("implementation_digest") != impl_hash:
        errors.append("authorization implementation digest is stale")
    if authorization.get("review_sha256") != sha256_file(review_path):
        errors.append("authorization binds a stale prospective review")
    if authorization.get("launch_authorized") is not True:
        errors.append("launch is not authorized")
    if authorization.get("authorizer_label") == review.get("reviewer_label"):
        errors.append("reviewer self-authorized launch")
    return errors


def consume_attempt(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    run_root, run = _load_run(repo_root, args.run_root)
    run_errors = _validate_run_bindings(repo_root, run_root, run)
    if run_errors:
        raise CustodyError("; ".join(run_errors))
    protocol_path = resolve_repo_path(repo_root, run["protocol_path"])
    protocol = strict_yaml_load(protocol_path)
    if protocol.get("profile") != "protected":
        raise CustodyError("one-shot attempt consumption is gated to protected profile")
    errors = validate_protocol_data(repo_root, protocol, require_frozen=True)
    errors.extend(_validate_protected_authority(repo_root, protocol_path, protocol))
    if errors:
        raise CustodyError("; ".join(errors))
    _ensure_id(args.attempt_id, "attempt_id")
    if args.attempt_id != run.get("attempt_id"):
        raise CustodyError("attempt identity does not match initialized run")
    attempt_path = run_root / "attempts" / f"{args.attempt_id}.json"
    if args.state == "started":
        if attempt_path.exists():
            raise CustodyError(
                "attempt identity is already consumed; retry is forbidden"
            )
        record = {
            "schema_version": SCHEMA_VERSION,
            "task_id": run["task_id"],
            "run_id": run["run_id"],
            "attempt_id": args.attempt_id,
            "state": "started",
            "history": [{"state": "started", "recorded_at": utc_now()}],
            "protocol_digest": protocol["protocol_digest"],
            "implementation_digest": implementation_digest(protocol),
        }
        _exclusive_write(
            attempt_path,
            (json.dumps(record, indent=2, sort_keys=True) + "\n").encode("utf-8"),
        )
    else:
        if not attempt_path.is_file():
            raise CustodyError("attempt must be atomically consumed as started first")
        record = strict_json_load(attempt_path)
        if record.get("state") != "started":
            raise CustodyError(
                f"attempt is already terminal ({record.get('state')}); retry is forbidden"
            )
        record["state"] = args.state
        record["history"].append(
            {
                "state": args.state,
                "recorded_at": utc_now(),
                "reason": args.reason,
            }
        )
        _atomic_write(
            attempt_path,
            (json.dumps(record, indent=2, sort_keys=True) + "\n").encode("utf-8"),
            replace=True,
        )
    print(f"Attempt {args.attempt_id} recorded as {args.state}; identity is consumed.")
    return 0


def validate_tree(args: argparse.Namespace) -> int:
    repo_root = repo_root_from(args.root)
    roots = sorted((repo_root / "tasks" / "evidence").glob("*/experiment"))
    errors: list[str] = []
    checked_protocols = 0
    checked_runs = 0
    for experiment in roots:
        for protocol_path in sorted(experiment.rglob("protocol.yaml")):
            checked_protocols += 1
            try:
                protocol = strict_yaml_load(protocol_path)
                protocol_errors = validate_protocol_data(
                    repo_root,
                    protocol,
                    require_frozen=protocol.get("state") == "frozen",
                )
                errors.extend(f"{protocol_path}: {error}" for error in protocol_errors)
                if protocol.get("profile") == "protected":
                    authority_files = (
                        protocol_path.parent / "prospective_review.json",
                        protocol_path.parent / "authorization.json",
                    )
                    if any(path.exists() for path in authority_files):
                        errors.extend(
                            f"{protocol_path}: {error}"
                            for error in _validate_protected_authority(
                                repo_root, protocol_path, protocol
                            )
                        )
            except ValueError as exc:
                errors.append(str(exc))
        for run_path in sorted(experiment.rglob("run.yaml")):
            checked_runs += 1
            try:
                run_root = run_path.parent
                run = strict_yaml_load(run_path)
                errors.extend(
                    f"{run_path}: {error}"
                    for error in _validate_run_bindings(repo_root, run_root, run)
                )
                bundle_path = run_root / "bundle.yaml"
                if bundle_path.exists():
                    bundle = strict_yaml_load(bundle_path)
                    bundle_errors, _, _ = _validate_bundle(
                        repo_root, run_root, run, bundle
                    )
                    errors.extend(f"{bundle_path}: {error}" for error in bundle_errors)
                    audit_path = run_root / "audit.json"
                    if not audit_path.is_file():
                        errors.append(
                            f"{run_path}: bundle is missing independent audit"
                        )
                    else:
                        audit = strict_json_load(audit_path)
                        if audit.get("bundle_sha256") != sha256_file(bundle_path):
                            errors.append(f"{audit_path}: audit binds stale bundle")
                        if audit.get("claim_authorized") is not False:
                            errors.append(
                                f"{audit_path}: audit must not silently authorize claim"
                            )
                for attempt_path in sorted((run_root / "attempts").glob("*.json")):
                    attempt = strict_json_load(attempt_path)
                    history = attempt.get("history")
                    if (
                        not isinstance(history, list)
                        or not history
                        or history[0].get("state") != "started"
                        or len(history) > 2
                    ):
                        errors.append(f"{attempt_path}: invalid one-shot history")
                    elif (
                        attempt.get("state") != history[-1].get("state")
                        or attempt.get("state") not in ATTEMPT_STATES
                        or (
                            len(history) == 2
                            and history[-1].get("state") not in {"failed", "completed"}
                        )
                    ):
                        errors.append(f"{attempt_path}: inconsistent one-shot state")
            except ValueError as exc:
                errors.append(str(exc))
    if errors:
        print("Experiment custody validation FAILED:")
        for error in errors:
            print(f" - {error}")
        return 1
    print(
        f"Experiment custody validation passed for {checked_protocols} protocol(s) "
        f"and {checked_runs} run(s)."
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command_name", required=True)

    validate = subs.add_parser("validate")
    validate.add_argument("--root", type=Path, default=Path("."))
    validate.set_defaults(handler=validate_tree)

    freeze = subs.add_parser("freeze-protocol")
    freeze.add_argument("--root", type=Path, default=Path("."))
    freeze.add_argument("--protocol", required=True)
    freeze.set_defaults(handler=freeze_protocol)

    init = subs.add_parser("init-run")
    init.add_argument("--root", type=Path, default=Path("."))
    init.add_argument("--protocol", required=True)
    init.add_argument("--run-parent", required=True)
    init.add_argument("--run-id", required=True)
    init.add_argument("--attempt-id", required=True)
    init.set_defaults(handler=init_run)

    cell = subs.add_parser("append-cell")
    cell.add_argument("--root", type=Path, default=Path("."))
    cell.add_argument("--run-root", required=True)
    cell.add_argument("--cell-key", required=True)
    cell.add_argument("--state", required=True, choices=sorted(CELL_STATES))
    cell.add_argument("--error")
    cell.add_argument("--reason")
    cell.add_argument("--artifact", action="append", default=[])
    cell.set_defaults(handler=append_cell)

    bundle = subs.add_parser("create-bundle")
    bundle.add_argument("--root", type=Path, default=Path("."))
    bundle.add_argument("--run-root", required=True)
    bundle_input = bundle.add_mutually_exclusive_group(required=True)
    bundle_input.add_argument("--raw-rows")
    bundle_input.add_argument("--benchmark-result")
    bundle.add_argument("--benchmark-manifests-root", default="benchmarks")
    bundle.add_argument("--summary", action="append", default=[], required=True)
    bundle.add_argument("--diagnostic", action="append", default=[])
    bundle.add_argument("--link", action="append", default=[])
    bundle.add_argument("--visual", action="store_true")
    bundle.add_argument("--preview", action="append", default=[])
    bundle.add_argument("--replay-command", action="append", default=[], required=True)
    bundle.add_argument("--view-command", action="append", default=[], required=True)
    bundle.add_argument("--smoke-receipt", action="append", default=[])
    bundle.set_defaults(handler=create_bundle)

    audit = subs.add_parser("audit-bundle")
    audit.add_argument("--root", type=Path, default=Path("."))
    audit.add_argument("--run-root", required=True)
    audit.add_argument("--driver", required=True)
    audit.add_argument("--auditor", required=True)
    audit.add_argument("--revision", required=True)
    audit.set_defaults(handler=audit_bundle)

    review = subs.add_parser("prospective-review")
    review.add_argument("--root", type=Path, default=Path("."))
    review.add_argument("--protocol", required=True)
    review.add_argument("--driver", required=True)
    review.add_argument("--reviewer", required=True)
    review.add_argument(
        "--review-kind",
        choices=("independent", "machine_rehearsal"),
        required=True,
    )
    review.add_argument("--review-method", required=True)
    review.add_argument("--review-boundary", required=True)
    review.add_argument("--source-coverage", action="append", default=[])
    review.add_argument("--protected-interactions", type=int, default=0)
    review.add_argument("--private-draws", type=int, default=0)
    review.add_argument(
        "--disposition",
        choices=("no-blocker", "refused", "revision-required"),
        required=True,
    )
    review.add_argument("--finding", action="append", default=[])
    review.set_defaults(handler=prospective_review)

    authorize = subs.add_parser("authorize-protected")
    authorize.add_argument("--root", type=Path, default=Path("."))
    authorize.add_argument("--protocol", required=True)
    authorize.add_argument("--authorizer", required=True)
    authorize.add_argument("--authorization-boundary", required=True)
    authorize.set_defaults(handler=authorize_protected)

    attempt = subs.add_parser("consume-attempt")
    attempt.add_argument("--root", type=Path, default=Path("."))
    attempt.add_argument("--run-root", required=True)
    attempt.add_argument("--attempt-id", required=True)
    attempt.add_argument("--state", choices=sorted(ATTEMPT_STATES), required=True)
    attempt.add_argument("--reason")
    attempt.set_defaults(handler=consume_attempt)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        return args.handler(args)
    except (CustodyError, ValueError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
