---
name: intrinsicengine-benchmark
description: Benchmark workflow and review for IntrinsicEngine. Defines the benchmark lifecycle (intent → manifest with stable `benchmark_id` → runner → result JSON with metrics+diagnostics → baseline comparison), manifest and runner requirements (deterministic invocation, PR-fast smoke vs heavy/nightly distinction, no external large datasets for smoke), the result-JSON reporting policy, and the benchmark review checklist (dataset declaration, warmup policy, schema validation, smoke threshold sanity, quality-metric inclusion). Use this skill whenever the user is adding or modifying anything under `benchmarks/` or `docs/benchmarking/`, writing a benchmark manifest, defining `benchmark_id`, wiring a benchmark runner, claiming a performance improvement, comparing against a baseline, reviewing a benchmark PR, or whenever the user mentions "perf", "benchmark", "regression", "smoke", "nightly", or "baseline" in a measurement context.
---

# IntrinsicEngine Benchmark Workflow

This skill governs benchmark work in IntrinsicEngine. Benchmarks live under
`benchmarks/` with documentation under `docs/benchmarking/`. The engine's
broader `methods/` layer also produces benchmarks, governed by
`intrinsicengine-method`.

The core invariant: **performance claims require baseline comparison**. A
benchmark without a baseline is a measurement, not a claim.

## Benchmark lifecycle

1. **Define benchmark intent.** One of: `smoke`, `correctness`, `performance`,
   `gpu`, `nightly`. The intent determines where the benchmark runs (PR-fast vs
   nightly), which dataset it can use, and what thresholds apply.

2. **Add or extend the benchmark manifest.** Each benchmark needs a stable
   `benchmark_id`, declared method, dataset, params, and metrics. Stability of
   `benchmark_id` is non-negotiable — it is the join key for historical
   comparison.

3. **Implement or wire the runner.** Deterministic invocation path. Distinguish
   PR-fast smoke from heavy/nightly workloads at the runner level — smoke must
   never require external large datasets.

4. **Emit result JSON with metrics + diagnostics.** Outputs are machine-readable
   and validate against the schema. Include quality/error metrics where
   relevant, not just runtime.

5. **Compare to baseline and document deltas.** Performance claims cite the
   baseline they're compared against, the measurement conditions, and the
   backend identity. Numerical-quality deltas are first-class.

For the full source workflow, read `references/benchmark-workflow.md`.

## Manifest requirements

- **Stable `benchmark_id`** — never changes once published; rename only via
  explicit migration.
- Declared **method**, **dataset**, **params**, and **metrics**.
- **Thresholds for smoke checks** when applicable.

Validate manifests:

```bash
python3 tools/benchmark/validate_benchmark_manifests.py
```

## Runner requirements

- **Deterministic invocation path** — same inputs produce the same outputs
  (modulo declared nondeterminism).
- **Distinguish PR-fast smoke from heavy/nightly workloads** at runner level.
- **Avoid requiring external large datasets for smoke checks** — smoke must
  run in the PR-fast window without large downloads.

## Reporting requirements

- JSON outputs are **machine-readable** and conform to the result schema.
- **Performance claims include baseline comparison** — no claim without a
  named baseline.
- **Numerical quality/error metrics** are included where relevant. A faster
  implementation that returns different numbers is not a perf win.

Validate result JSON:

```bash
python3 tools/benchmark/validate_benchmark_results.py
```

## Benchmark review checklist

Apply this checklist when reviewing benchmark changes — both the benchmark
definition and the change that motivates running it. Read
`references/benchmark-review-checklist.md` for the full version.

### 1. Benchmark definition quality

- Dataset is explicitly declared and versioned/named.
- Parameters are explicitly declared (including defaults).
- **Warmup policy is declared** (iterations or time). Cold-start measurements
  are different signals.
- Metrics are explicitly declared and meaningful for the method.
- Baseline is declared and comparable.

### 2. Output and validation quality

- Runner emits benchmark result JSON with the required schema fields.
- Result JSON validates with
  `tools/benchmark/validate_benchmark_results.py`.
- Benchmark manifest validates with
  `tools/benchmark/validate_benchmark_manifests.py`.

### 3. CI and execution policy

- PR smoke thresholds are reasonable and stable (no flaky-by-design thresholds).
- Heavy/slow benchmarks are excluded from PR-fast and reserved for nightly/deep runs.
- Benchmark category labels and workflow routing are correct.

### 4. Scientific / engineering signal quality

- **Quality / error metric is included when relevant — not runtime-only claims.**
- Reported comparisons identify backend and dataset context.
- Any performance claim references baseline comparison and measurement conditions.

### 5. Documentation and traceability

- Benchmark docs updated (`docs/benchmarking/*`) when policy/manifests change.
- PR includes benchmark decisions in the Benchmarking section.
- Temporary benchmark shims/exceptions tracked with removal task IDs.

## Anti-patterns

These come up enough in practice that they're worth calling out explicitly:

- **Runtime-only claims.** "30% faster" without quality/error metric is not a
  benchmark, it's a marketing slide.
- **Comparing optimized backends against each other** instead of against the
  reference — `intrinsicengine-method` rules apply: reference is canonical truth.
- **Renaming `benchmark_id`** during a refactor — breaks historical comparison.
  If a benchmark genuinely needs a new identity, retire the old ID explicitly
  and link the migration.
- **Smoke thresholds tight enough to be flaky** — defeats the purpose of smoke
  as a fast confidence signal.
- **Smoke that requires the big dataset** — moves the workload out of PR-fast
  into the territory where developers won't run it locally.

## Related repo documentation

- `benchmarks/README.md` — benchmark package structure and manifest expectations.
- `docs/benchmarking/index.md` — benchmarking documentation index.
- `docs/benchmarking/overview.md`
- `docs/benchmarking/dataset-policy.md`
- `docs/benchmarking/metrics.md`
- `docs/benchmarking/baselines.md`
- `docs/benchmarking/ci-policy.md`
- `docs/benchmarking/report-template.md`
- `docs/benchmarking/benchmark-manifest-schema.md`
- `docs/benchmarking/result-json-schema.md`

## References

- `references/benchmark-workflow.md` — lifecycle, manifest/runner/reporting
  requirements.
- `references/benchmark-review-checklist.md` — full review checklist; use this
  for benchmark-PR review.
