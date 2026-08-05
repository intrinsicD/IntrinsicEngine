# Benchmark result JSON schema

Canonical benchmark results use `schema_version: 2`. Raw native producers may
emit the legacy minimum temporarily inside one invocation, but
`tools/benchmark/run_and_seal.py` seals them before validation or artifact
publication.

## Identity

- `benchmark_id` is the stable definition/history join key and must resolve to
  exactly one manifest.
- `run_id` identifies one initialized execution population.
- `attempt_id` identifies an attempt within that run.

The validator's uniqueness key is
`(benchmark_id, run_id, attempt_id)`. Repeating a stable benchmark ID with a
new run/attempt is valid. Reusing the tuple is invalid. A retry receives a new
attempt ID; failed attempts stay present. `supersedes` may point from a new
result to older identities, but never deletes them. Aggregates use a distinct
stable benchmark manifest and may list member identities in `aggregation`.

Disposable local CMake smoke output may reuse a fixed run ID because its build
directory is explicitly recreated and it is never claim evidence. Durable and
official output uses a fresh non-overwriting run root.

## Required fields

Every canonical object contains:

- `schema_version`: integer `2`.
- `benchmark_id`, `run_id`, `attempt_id`: identities described above.
- `method`, `backend`, `dataset`: execution binding; method and dataset must
  exactly match the manifest.
- `commit`: compatibility alias equal to `source.revision`.
- `source`: revision and source-state binding.
- `claim_eligible`: explicit boolean, false by default.
- `manifest`: repository-relative path and SHA-256 of the exact manifest.
- `resolved_params`: exact copy of manifest `params`.
- `config_digest`: SHA-256 of canonical JSON for `resolved_params`.
- `warmup_policy`: the manifest's resolved warmup/measured fields.
- `metrics`: exactly the top-level metric names declared by the manifest.
- `diagnostics`: non-metric runtime context.
- `thresholds`: exact copy of manifest thresholds.
- `threshold_disposition`: validator-recomputable observed/limit/operator/pass
  records.
- `execution_status`: raw producer state.
- `status`: state recomputed from execution plus thresholds.

Optional reporting fields include `timestamp_utc`, `runner`, `host`, `notes`,
`supersedes`, and `aggregation`.

## Source and claim eligibility

Allowed source states are:

- `clean_commit`: exact 40-lowercase-hex revision.
- `dirty_worktree`: includes `local-dev`; never claim eligible.
- `sealed_snapshot`: uncommitted source with both approved
  `snapshot_sha256` and `diff_sha256`.
- `historical_aggregate`: retained historical population; not a new claim run.
- `unverified_commit`: commit-shaped provenance without clean-state custody.

`claim_eligible: true` is valid only for an exact `clean_commit` or an approved
`sealed_snapshot` carrying both hashes. Merely adding a report, setting status
to passed, or using a commit-shaped string does not authorize a claim.
Claim-grade use also owes the frozen protocol, bundle, independent audit, and
ARA claim policy.

## Metrics and JSON strictness

JSON is RFC-strict: duplicate object keys, `NaN`, `Infinity`, and `-Infinity`
are rejected at load time. Every metric leaf, including nested dict/list
families, is a finite integer or float. Booleans are not numeric metrics.
Capability flags belong in `diagnostics`.

The result metric-name set must exactly equal the manifest declaration; an
undeclared extra metric is an error rather than silently ignored evidence.

## Thresholds and status

Manifest threshold names end in `_max` or `_min` and contain one declared
metric name, for example `smoke_runtime_ms_max` or
`throughput_items_per_sec_min`. The canonical sealer maps each threshold,
copies its limit, records the observed metric, and computes `passed`.

`status` is not trusted:

- `error` and `skipped` remain those execution states.
- raw `failed` remains failed.
- raw `passed` becomes failed if any declared threshold fails.
- only raw passed plus every gate passed becomes canonical passed.

The validator recomputes the complete disposition and status.

Native runners return zero only for raw `passed`. A skipped or failed attempt
still writes its raw diagnostic payload, but returns nonzero so generic command
receipts cannot mistake non-execution for success. GPU diagnostics distinguish
the requested backend from the actual backend; when no backend request was
accepted, `actual_backend` uses an explicit non-execution token and
`fallback_observed` remains unknown rather than manufacturing successful GPU
identity from zero counters.

`claim_eligible` describes source custody, not whether an attempt passed, so a
non-passed canonical result may be retained. Claim-grade positive bundle audit
separately requires both `execution_status` and recomputed `status` to be
`passed`.

## Commands

Seal raw producer output:

```bash
python3 tools/benchmark/seal_benchmark_results.py \
  --root build/ci/benchmark/IntrinsicBenchmarkSmoke \
  --manifests-root benchmarks \
  --run-id run-20260729-001 \
  --attempt-id attempt-001 \
  --replace
```

Run a native producer and seal it in one step:

```bash
python3 tools/benchmark/run_and_seal.py \
  --executable build/ci/bin/IntrinsicBenchmarkSmoke \
  --output build/ci/benchmark/IntrinsicBenchmarkSmoke/result.json \
  --manifests-root benchmarks \
  --run-id run-20260729-001
```

Validate:

```bash
python3 tools/benchmark/validate_benchmark_results.py \
  --root build/ci/benchmark \
  --manifests-root benchmarks \
  --strict
```

## Example

```json
{
  "schema_version": 2,
  "benchmark_id": "geometry.example.small",
  "run_id": "run-20260729-001",
  "attempt_id": "attempt-001",
  "method": "geometry.example",
  "backend": "cpu_reference",
  "dataset": "builtin.triangle_mesh.small",
  "commit": "0000000000000000000000000000000000000000",
  "source": {
    "revision": "0000000000000000000000000000000000000000",
    "state": "unverified_commit"
  },
  "claim_eligible": false,
  "manifest": {
    "path": "benchmarks/datasets/manifests/geometry_example_small.yaml",
    "sha256": "3b3facd2b2d355f92e2959c779834896c051e3ffbc4372a6c6bb77e9981eaff2"
  },
  "resolved_params": {
    "intent": "smoke",
    "warmup_iterations": 1,
    "measured_iterations": 8
  },
  "config_digest": "143720a539c0474f31b2af8f749bebb94e8d65a0784684d18d771afd1def2536",
  "warmup_policy": {
    "warmup_iterations": 1,
    "measured_iterations": 8
  },
  "metrics": {
    "runtime_ms": 1.8,
    "memory_peak_bytes": 28672,
    "quality_error_l2": 0.0
  },
  "diagnostics": {
    "runner": "IntrinsicBenchmarkSmoke"
  },
  "thresholds": {
    "smoke_runtime_ms_max": 200,
    "quality_error_l2_max": 0.0
  },
  "threshold_disposition": [
    {
      "threshold": "smoke_runtime_ms_max",
      "metric": "runtime_ms",
      "operator": "<=",
      "limit": 200,
      "observed": 1.8,
      "passed": true
    },
    {
      "threshold": "quality_error_l2_max",
      "metric": "quality_error_l2",
      "operator": "<=",
      "limit": 0.0,
      "observed": 0.0,
      "passed": true
    }
  ],
  "execution_status": "passed",
  "status": "passed"
}
```
