#!/usr/bin/env python3
"""Validate versioned benchmark results against their exact manifests."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path
from typing import Any, Iterable

RESULT_SCHEMA_VERSION = 2
REQUIRED_FIELDS_V2 = {
    "schema_version",
    "benchmark_id",
    "run_id",
    "attempt_id",
    "method",
    "backend",
    "dataset",
    "source",
    "claim_eligible",
    "manifest",
    "resolved_params",
    "config_digest",
    "warmup_policy",
    "metrics",
    "diagnostics",
    "thresholds",
    "threshold_disposition",
    "execution_status",
    "status",
}
ALLOWED_BACKENDS = {
    "cpu_reference",
    "cpu_optimized",
    "gpu_vulkan_compute",
    "gpu_vulkan_graphics",
    "cuda_optional",
    "external_baseline",
}
ALLOWED_STATUS = {"passed", "failed", "skipped", "error"}
ALLOWED_SOURCE_STATES = {
    "clean_commit",
    "dirty_worktree",
    "sealed_snapshot",
    "historical_aggregate",
    "unverified_commit",
}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
IDENTITY_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,159}$")


class ContractError(ValueError):
    pass


def _reject_duplicate(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise ContractError(f"duplicate JSON key {key!r}")
        value[key] = item
    return value


def _reject_constant(value: str) -> None:
    raise ContractError(f"non-standard JSON constant {value!r}")


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(
                handle,
                object_pairs_hook=_reject_duplicate,
                parse_constant=_reject_constant,
            )
    except (json.JSONDecodeError, OSError, ContractError) as exc:
        raise ContractError(f"invalid JSON ({exc})") from exc
    if not isinstance(data, dict):
        raise ContractError("result root must be a JSON object")
    return data


def _strict_yaml(path: Path) -> dict[str, Any]:
    try:
        import yaml
    except ImportError as exc:
        raise ContractError("pyyaml is required (pip install pyyaml)") from exc

    class UniqueKeyLoader(yaml.SafeLoader):
        pass

    def unique_mapping(loader, node, deep=False):
        mapping: dict[Any, Any] = {}
        for key_node, value_node in node.value:
            key = loader.construct_object(key_node, deep=deep)
            if key in mapping:
                raise yaml.constructor.ConstructorError(
                    "while constructing a mapping",
                    node.start_mark,
                    f"found duplicate key {key!r}",
                    key_node.start_mark,
                )
            mapping[key] = loader.construct_object(value_node, deep=deep)
        return mapping

    UniqueKeyLoader.add_constructor(
        yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, unique_mapping
    )
    try:
        value = yaml.load(path.read_text(encoding="utf-8"), Loader=UniqueKeyLoader)
    except (OSError, yaml.YAMLError) as exc:
        raise ContractError(f"invalid manifest YAML ({exc})") from exc
    if not isinstance(value, dict):
        raise ContractError("manifest root must be a mapping")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_digest(value: object) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
        ).encode("utf-8")
    ).hexdigest()


def load_manifests(root: Path) -> dict[str, tuple[Path, dict[str, Any]]]:
    manifests: dict[str, tuple[Path, dict[str, Any]]] = {}
    for path in sorted(root.rglob("*.yaml")):
        manifest = _strict_yaml(path)
        benchmark_id = manifest.get("benchmark_id")
        if not isinstance(benchmark_id, str) or not benchmark_id:
            continue
        if benchmark_id in manifests:
            raise ContractError(
                f"duplicate manifest benchmark_id {benchmark_id!r}: "
                f"{manifests[benchmark_id][0]} and {path}"
            )
        manifests[benchmark_id] = (path, manifest)
    return manifests


def manifest_display_path(path: Path, manifests_root: Path) -> str:
    try:
        return path.resolve().relative_to(manifests_root.resolve().parent).as_posix()
    except ValueError:
        return path.resolve().relative_to(manifests_root.resolve()).as_posix()


def derive_warmup_policy(params: dict[str, Any]) -> dict[str, Any]:
    return {
        key: value
        for key, value in params.items()
        if "warmup" in key.lower() or "measured" in key.lower()
    }


def threshold_metric_name(
    threshold_name: str, declared_metrics: Iterable[str]
) -> str | None:
    candidates = [metric for metric in declared_metrics if metric in threshold_name]
    if not candidates:
        return None
    return max(candidates, key=len)


def compute_threshold_disposition(
    metrics: dict[str, Any],
    thresholds: dict[str, Any],
    declared_metrics: Iterable[str],
) -> tuple[list[dict[str, Any]], list[str]]:
    disposition: list[dict[str, Any]] = []
    errors: list[str] = []
    for name, threshold in thresholds.items():
        metric = threshold_metric_name(name, declared_metrics)
        if metric is None:
            errors.append(f"threshold {name!r} cannot be mapped to a declared metric")
            continue
        if name.endswith("_max"):
            operator = "<="
        elif name.endswith("_min"):
            operator = ">="
        else:
            errors.append(f"threshold {name!r} must end in _max or _min")
            continue
        observed = metrics.get(metric)
        if type(threshold) not in {int, float} or not math.isfinite(threshold):
            errors.append(f"threshold {name!r} must be finite numeric")
            continue
        if type(observed) not in {int, float} or not math.isfinite(observed):
            errors.append(f"threshold metric {metric!r} must be finite numeric")
            continue
        passed = observed <= threshold if operator == "<=" else observed >= threshold
        disposition.append(
            {
                "threshold": name,
                "metric": metric,
                "operator": operator,
                "limit": threshold,
                "observed": observed,
                "passed": passed,
            }
        )
    return disposition, errors


def computed_status(execution_status: str, disposition: list[dict[str, Any]]) -> str:
    if execution_status in {"error", "skipped"}:
        return execution_status
    if execution_status == "failed":
        return "failed"
    if any(entry.get("passed") is not True for entry in disposition):
        return "failed"
    return "passed"


def _walk_metric_values(value: object, location: str, errors: list[str]) -> None:
    if type(value) in {int, float}:
        if not math.isfinite(value):
            errors.append(f"{location} must be finite")
        return
    if isinstance(value, bool):
        errors.append(f"{location} must be numeric, not boolean")
        return
    if isinstance(value, dict):
        if not value:
            errors.append(f"{location} nested metric object must not be empty")
        for key, item in value.items():
            if not isinstance(key, str) or not key:
                errors.append(f"{location} contains an invalid nested key")
            else:
                _walk_metric_values(item, f"{location}.{key}", errors)
        return
    if isinstance(value, list):
        if not value:
            errors.append(f"{location} nested metric list must not be empty")
        for index, item in enumerate(value):
            _walk_metric_values(item, f"{location}[{index}]", errors)
        return
    errors.append(f"{location} has unsupported type {type(value).__name__}")


def _is_nonempty_string(value: object) -> bool:
    return isinstance(value, str) and bool(value.strip())


def validate_result_data(
    data: dict[str, Any],
    path: Path,
    manifests: dict[str, tuple[Path, dict[str, Any]]],
    manifests_root: Path,
) -> tuple[tuple[str, str, str] | None, list[str]]:
    errors: list[str] = []
    missing = sorted(REQUIRED_FIELDS_V2 - data.keys())
    if missing:
        errors.append(f"missing required fields: {', '.join(missing)}")
    if data.get("schema_version") != RESULT_SCHEMA_VERSION:
        errors.append(
            f"schema_version must equal {RESULT_SCHEMA_VERSION}; legacy results "
            "must be sealed before strict validation"
        )

    for key in (
        "benchmark_id",
        "run_id",
        "attempt_id",
        "method",
        "backend",
        "dataset",
        "execution_status",
        "status",
    ):
        if key in data and not _is_nonempty_string(data[key]):
            errors.append(f"field {key!r} must be a non-empty string")
    for key in ("run_id", "attempt_id"):
        value = data.get(key)
        if isinstance(value, str) and not IDENTITY_RE.fullmatch(value):
            errors.append(f"field {key!r} has invalid identity syntax")

    backend = data.get("backend")
    if isinstance(backend, str) and backend not in ALLOWED_BACKENDS:
        errors.append(f"unsupported backend {backend!r}")
    for key in ("execution_status", "status"):
        value = data.get(key)
        if isinstance(value, str) and value not in ALLOWED_STATUS:
            errors.append(f"field {key!r} has unsupported value {value!r}")
    if type(data.get("claim_eligible")) is not bool:
        errors.append("claim_eligible must be a boolean")

    metrics = data.get("metrics")
    if not isinstance(metrics, dict) or not metrics:
        errors.append("metrics must be a non-empty object")
        metrics = {}
    else:
        for name, value in metrics.items():
            if not isinstance(name, str) or not name:
                errors.append("metrics contains an invalid name")
            else:
                _walk_metric_values(value, f"metrics.{name}", errors)
    if not isinstance(data.get("diagnostics"), dict):
        errors.append("diagnostics must be an object")

    benchmark_id = data.get("benchmark_id")
    manifest_record = (
        manifests.get(benchmark_id) if isinstance(benchmark_id, str) else None
    )
    if manifest_record is None:
        errors.append(f"no exact manifest found for benchmark_id {benchmark_id!r}")
    else:
        manifest_path, manifest = manifest_record
        for key in ("method", "dataset"):
            if data.get(key) != manifest.get(key):
                errors.append(
                    f"{key} mismatch: result {data.get(key)!r}, "
                    f"manifest {manifest.get(key)!r}"
                )
        declared_metrics = manifest.get("metrics")
        if not isinstance(declared_metrics, list):
            errors.append("manifest metrics must be a list")
            declared_metrics = []
        if set(metrics) != set(declared_metrics):
            errors.append(
                f"result metric set {sorted(metrics)} does not equal manifest "
                f"metric set {sorted(str(item) for item in declared_metrics)}"
            )
        params = manifest.get("params")
        if data.get("resolved_params") != params:
            errors.append("resolved_params do not exactly match manifest params")
        if data.get("config_digest") != canonical_digest(params):
            errors.append("config_digest does not bind resolved manifest params")
        expected_warmup = derive_warmup_policy(
            params if isinstance(params, dict) else {}
        )
        if data.get("warmup_policy") != expected_warmup:
            errors.append("warmup_policy does not match resolved manifest params")
        thresholds = manifest.get("thresholds")
        if data.get("thresholds") != thresholds:
            errors.append("thresholds do not exactly match manifest")
        binding = data.get("manifest")
        if not isinstance(binding, dict):
            errors.append("manifest binding must be an object")
        else:
            expected_path = manifest_display_path(manifest_path, manifests_root)
            if binding.get("path") != expected_path:
                errors.append(f"manifest path mismatch: expected {expected_path!r}")
            if binding.get("sha256") != sha256_file(manifest_path):
                errors.append("manifest sha256 mismatch")
        computed, threshold_errors = compute_threshold_disposition(
            metrics,
            thresholds if isinstance(thresholds, dict) else {},
            declared_metrics,
        )
        errors.extend(threshold_errors)
        if data.get("threshold_disposition") != computed:
            errors.append("threshold_disposition does not match recomputed gates")
        execution = data.get("execution_status")
        if isinstance(execution, str) and execution in ALLOWED_STATUS:
            expected_status = computed_status(execution, computed)
            if data.get("status") != expected_status:
                errors.append(
                    f"status mismatch: expected {expected_status!r} from execution "
                    "and threshold disposition"
                )

    source = data.get("source")
    if not isinstance(source, dict):
        errors.append("source must be an object")
        source = {}
    revision = source.get("revision")
    state = source.get("state")
    if not _is_nonempty_string(revision):
        errors.append("source.revision must be non-empty")
    if state not in ALLOWED_SOURCE_STATES:
        errors.append(f"source.state has unsupported value {state!r}")
    if data.get("commit") is not None and data.get("commit") != revision:
        errors.append("legacy commit alias must equal source.revision")
    if revision == "local-dev":
        if state != "dirty_worktree" or data.get("claim_eligible") is not False:
            errors.append(
                "local-dev source must be dirty_worktree and non-claim-eligible"
            )
    if state == "clean_commit" and (
        not isinstance(revision, str) or not COMMIT_RE.fullmatch(revision)
    ):
        errors.append("clean_commit source requires an exact 40-hex revision")
    if data.get("claim_eligible") is True:
        if state == "clean_commit":
            if not isinstance(revision, str) or not COMMIT_RE.fullmatch(revision):
                errors.append("claim-eligible clean source needs exact 40-hex commit")
        elif state == "sealed_snapshot":
            for key in ("snapshot_sha256", "diff_sha256"):
                if not isinstance(source.get(key), str) or not SHA256_RE.fullmatch(
                    source[key]
                ):
                    errors.append(
                        f"claim-eligible sealed snapshot requires source.{key}"
                    )
        else:
            errors.append(
                "claim eligibility requires clean_commit or an approved sealed_snapshot"
            )

    identity: tuple[str, str, str] | None = None
    if all(
        isinstance(data.get(key), str)
        for key in ("benchmark_id", "run_id", "attempt_id")
    ):
        identity = (data["benchmark_id"], data["run_id"], data["attempt_id"])
    return identity, [f"{path}: {error}" for error in errors]


def validate_result(
    path: Path,
    manifests: dict[str, tuple[Path, dict[str, Any]]],
    manifests_root: Path,
) -> tuple[tuple[str, str, str] | None, list[str]]:
    try:
        data = load_json(path)
    except ContractError as exc:
        return None, [f"{path}: {exc}"]
    return validate_result_data(data, path, manifests, manifests_root)


def discover_results(root: Path) -> list[Path]:
    return sorted(root.rglob("*.json"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("benchmarks"),
        help="Root directory containing canonical result JSON files",
    )
    parser.add_argument(
        "--manifests-root",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "benchmarks",
    )
    parser.add_argument(
        "--strict", action="store_true", help="Retained for CLI compatibility."
    )
    args = parser.parse_args()

    if not args.root.exists():
        print(f"BLOCKED: benchmark result root does not exist: {args.root}")
        print("Run the benchmark producer first, for example:")
        print("  cmake --build --preset ci --target IntrinsicBenchmarks")
        print("Then rerun benchmark result validation.")
        return 3
    if not args.manifests_root.exists():
        print(f"BLOCKED: benchmark manifest root does not exist: {args.manifests_root}")
        return 3
    try:
        manifests = load_manifests(args.manifests_root)
    except ContractError as exc:
        print(f"Benchmark result JSON validation FAILED:\n - {exc}")
        return 1
    results = discover_results(args.root)
    if not results:
        print(f"WARNING: no benchmark result JSON files found under {args.root}")
        return 0

    errors: list[str] = []
    identities: dict[tuple[str, str, str], Path] = {}
    stable_ids: set[str] = set()
    for path in results:
        identity, result_errors = validate_result(path, manifests, args.manifests_root)
        errors.extend(result_errors)
        if identity:
            stable_ids.add(identity[0])
            if identity in identities:
                errors.append(
                    f"duplicate run/attempt identity {identity!r} found in "
                    f"{identities[identity]} and {path}"
                )
            else:
                identities[identity] = path
    if errors:
        print("Benchmark result JSON validation FAILED:")
        for error in errors:
            print(f" - {error}")
        return 1
    print(
        f"Benchmark result JSON validation passed for {len(results)} file(s), "
        f"{len(stable_ids)} stable benchmark ID(s), and {len(identities)} "
        "append-only run/attempt identity tuple(s)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
