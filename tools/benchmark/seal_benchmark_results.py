#!/usr/bin/env python3
"""Seal raw benchmark output into the canonical result schema v2."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from validate_benchmark_results import (  # noqa: E402
    RESULT_SCHEMA_VERSION,
    canonical_digest,
    compute_threshold_disposition,
    computed_status,
    derive_warmup_policy,
    load_json,
    load_manifests,
    manifest_display_path,
    sha256_file,
    validate_result_data,
)


class SealError(ValueError):
    pass


def _timestamp_id() -> str:
    return datetime.now(UTC).strftime("%Y%m%dT%H%M%S%fZ") + f"-p{os.getpid()}"


def _source_state(revision: str, requested: str) -> str:
    if requested != "auto":
        return requested
    if revision == "local-dev":
        return "dirty_worktree"
    if re.fullmatch(r"[0-9a-f]{40}", revision):
        return "unverified_commit"
    if revision.startswith("historical"):
        return "historical_aggregate"
    return "dirty_worktree"


def canonicalize(
    raw: dict[str, Any],
    *,
    manifest_path: Path,
    manifest: dict[str, Any],
    manifests_root: Path,
    run_id: str,
    attempt_id: str,
    source_state: str,
    source_revision: str | None,
    claim_eligible: bool,
    snapshot_sha256: str | None,
    diff_sha256: str | None,
) -> dict[str, Any]:
    metrics = raw.get("metrics")
    if not isinstance(metrics, dict):
        raise SealError("raw result metrics must be an object")
    raw_status = raw.get("execution_status", raw.get("status"))
    if raw_status not in {"passed", "failed", "skipped", "error"}:
        raise SealError(f"raw result has invalid status {raw_status!r}")
    revision = (
        source_revision or raw.get("commit") or raw.get("source", {}).get("revision")
    )
    if not isinstance(revision, str) or not revision:
        raise SealError("source revision is missing")
    state = _source_state(revision, source_state)
    source: dict[str, Any] = {"revision": revision, "state": state}
    if snapshot_sha256:
        source["snapshot_sha256"] = snapshot_sha256
    if diff_sha256:
        source["diff_sha256"] = diff_sha256
    thresholds = manifest.get("thresholds")
    declared_metrics = manifest.get("metrics")
    if not isinstance(thresholds, dict) or not isinstance(declared_metrics, list):
        raise SealError("manifest thresholds/metrics are invalid")
    disposition, disposition_errors = compute_threshold_disposition(
        metrics, thresholds, declared_metrics
    )
    if disposition_errors:
        raise SealError("; ".join(disposition_errors))
    params = manifest.get("params")
    if not isinstance(params, dict):
        raise SealError("manifest params must be an object")
    canonical: dict[str, Any] = {
        "schema_version": RESULT_SCHEMA_VERSION,
        "benchmark_id": raw.get("benchmark_id"),
        "run_id": raw.get("run_id", run_id),
        "attempt_id": raw.get("attempt_id", attempt_id),
        "method": raw.get("method"),
        "backend": raw.get("backend"),
        "dataset": raw.get("dataset"),
        "commit": revision,
        "source": source,
        "claim_eligible": claim_eligible,
        "manifest": {
            "path": manifest_display_path(manifest_path, manifests_root),
            "sha256": sha256_file(manifest_path),
        },
        "resolved_params": params,
        "config_digest": canonical_digest(params),
        "warmup_policy": derive_warmup_policy(params),
        "metrics": metrics,
        "diagnostics": raw.get("diagnostics", {}),
        "thresholds": thresholds,
        "threshold_disposition": disposition,
        "execution_status": raw_status,
        "status": computed_status(raw_status, disposition),
    }
    for key in (
        "timestamp_utc",
        "runner",
        "host",
        "notes",
        "supersedes",
        "aggregation",
    ):
        if key in raw:
            canonical[key] = raw[key]
    return canonical


def _atomic_replace(path: Path, payload: bytes) -> None:
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    with temporary.open("xb") as handle:
        handle.write(payload)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument(
        "--manifests-root",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "benchmarks",
    )
    parser.add_argument("--run-id", default=None)
    parser.add_argument("--attempt-id", default="attempt-001")
    parser.add_argument(
        "--source-state",
        choices=(
            "auto",
            "clean_commit",
            "dirty_worktree",
            "sealed_snapshot",
            "historical_aggregate",
            "unverified_commit",
        ),
        default="auto",
    )
    parser.add_argument("--source-revision")
    parser.add_argument("--snapshot-sha256")
    parser.add_argument("--diff-sha256")
    parser.add_argument("--claim-eligible", action="store_true")
    parser.add_argument(
        "--replace",
        action="store_true",
        help="Replace raw files in place; otherwise canonical files are written under --output-root.",
    )
    parser.add_argument("--output-root", type=Path)
    args = parser.parse_args()

    if not args.root.exists():
        print(f"ERROR: result root does not exist: {args.root}", file=sys.stderr)
        return 2
    if not args.replace and args.output_root is None:
        print("ERROR: choose --replace or --output-root", file=sys.stderr)
        return 2
    try:
        manifests = load_manifests(args.manifests_root)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    files = sorted(args.root.rglob("*.json"))
    if not files:
        print(f"ERROR: no raw JSON files under {args.root}", file=sys.stderr)
        return 2
    run_id = args.run_id or f"run-{_timestamp_id()}"
    all_errors: list[str] = []
    sealed = 0
    for path in files:
        try:
            raw = load_json(path)
            if raw.get("schema_version") == RESULT_SCHEMA_VERSION:
                if args.replace:
                    raise SealError(f"refusing to reseal canonical result: {path}")
                canonical = raw
            else:
                benchmark_id = raw.get("benchmark_id")
                if benchmark_id not in manifests:
                    raise SealError(
                        f"{path}: no manifest for benchmark_id {benchmark_id!r}"
                    )
                manifest_path, manifest = manifests[benchmark_id]
                canonical = canonicalize(
                    raw,
                    manifest_path=manifest_path,
                    manifest=manifest,
                    manifests_root=args.manifests_root,
                    run_id=run_id,
                    attempt_id=args.attempt_id,
                    source_state=args.source_state,
                    source_revision=args.source_revision,
                    claim_eligible=args.claim_eligible,
                    snapshot_sha256=args.snapshot_sha256,
                    diff_sha256=args.diff_sha256,
                )
                _, validation_errors = validate_result_data(
                    canonical, path, manifests, args.manifests_root
                )
                if validation_errors:
                    raise SealError("; ".join(validation_errors))
            if args.replace:
                destination = path
            else:
                destination = args.output_root / path.relative_to(args.root)
                destination.parent.mkdir(parents=True, exist_ok=True)
                if destination.exists():
                    raise SealError(f"refusing to overwrite {destination}")
            _atomic_replace(
                destination,
                (json.dumps(canonical, indent=2, sort_keys=True) + "\n").encode(
                    "utf-8"
                ),
            )
            sealed += 1
        except (OSError, ValueError) as exc:
            all_errors.append(str(exc))
    if all_errors:
        print("Benchmark result sealing FAILED:")
        for error in all_errors:
            print(f" - {error}")
        return 1
    print(f"Sealed {sealed} benchmark result(s) as schema v{RESULT_SCHEMA_VERSION}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
