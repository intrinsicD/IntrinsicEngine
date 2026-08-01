---
id: METHOD-019
theme: I
depends_on: [METHOD-016, METHOD-017, METHOD-018, RUNTIME-175, UI-035]
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner: "Codex-GeometryE2E"
branch: "feature/lop-consolidation-e2e"
worktree: "/tmp/intrinsic-geometry-e2e.GJlhXS"
claimed_at: "2026-08-01T23:17:30Z"
maturity_target: ParityProven
---
# METHOD-019 — LOP-family optimized CPU backend and comparison benchmark

## Status
- Completed on 2026-08-02 as a claim-grade negative confirmation. All four
  preregistered optimized candidates preserved exact reference parity and
  deterministic identity, but none met the frozen `<= 0.80` paired-median
  runtime-ratio gate. No `cpu_optimized` public backend, runtime config token,
  or UI choice was adopted.
- Implementation revision: `cfd0d9bdebe6c46969394d61593bf4305365ebec`;
  completion-evidence commit: pending.

## Goal
- Evaluate one concrete optimized CPU path for every LOP-family strategy and
  expose `cpu_optimized` only for the strategies that meet frozen
  reference-parity and useful-speedup gates. Add the paired comparison
  benchmark/baseline that supports those bounded speed/quality claims.

## Non-goals
- No algorithm/variant changes and no quality-behavior changes versus the reference backends — this is an acceleration slice; the reference stays the canonical truth.
- No GPU backend (owned by `METHOD-020`).
- No new public method parameters beyond the backend request; neighborhood
  and pruning policy stays implementation-internal, and the strategy axis is
  unchanged.

## Context
- Owner/layer: `src/geometry` (the `Geometry.PointCloud.Consolidation` module and the `Geometry.PointCloud.Kernels` seam). `geometry -> core` only.
- Backend policy (`docs/architecture/algorithm-variant-dispatch.md`,
  `docs/methods/backend-policy.md`): the reference is canonical truth. This
  task introduces explicit `cpu_reference`/`cpu_optimized` requests and every
  result reports actual identity; validation/benchmark results additionally
  report parity deltas against a same-input reference run.
- Concrete acceleration scope: spatially bounded `Geometry.KDTree`/existing
  grid neighborhoods for WLOP/LOP/EAR and component-neighborhood pruning for
  CLOP. `GEOM-060`/`GEOM-061` are not gates and are not opportunistically
  folded into this task; a later task may consume them only after this stable
  baseline shows a measured need.
- Benchmark policy (`docs/agent/benchmark-workflow.md`, `intrinsicengine-benchmark` skill): no speedup claim without a baseline comparison on a declared manifest.

## Control surfaces
- Config/UI/Agent: this task introduces the first concrete
  `cpu_reference`/`cpu_optimized` selector. Extend the validated
  `RUNTIME-175` config/typed-operation and `UI-035` presentation path only after the
  optimized implementation exists; do not expose a placeholder choice.

## Backends
- Backend axis: adds `cpu_optimized` to the family; `cpu_reference` stays the parity oracle. `gpu_vulkan_compute` deferred to `METHOD-020`.

## Slice plan
- **Slice A — parity contract.** Freeze canonical fixtures, output alignment,
  per-strategy quality/parity metrics, tolerances, fallback rules, benchmark
  params, warmup/sample order, and baseline identity before optimization.
- **Slice B — one concrete optimized path.** Add bounded spatial neighborhoods
  for WLOP/LOP first and prove reference parity/determinism.
- **Slice C — CLOP/EAR.** Extend the same concrete acceleration policy one
  strategy at a time; no optional fast seam is added mid-task.
- **Slice D — comparison evidence.** Run paired reference/optimized
  measurements, validate result JSON, and commit the baseline report before
  making any speed claim.

## Right-sizing
- The second CPU implementation justifies one explicit backend selector and
  shared result telemetry. Use plain strategy dispatch; do not add a backend
  interface, registry, service, or acceleration-plugin framework.
- `GEOM-060`/`061` adoption requires a separate measured follow-up rather than
  conditional branches in this task.

## Frozen parity and comparison protocol
- Preregistered at clean revision
  `6f2277b079044c96d6c38405941393f93861384b`, before optimized source changes.
- Frozen before optimized source changes in
  `methods/geometry/locally_optimal_projection/reports/METHOD-019-protocol.md`
  and executable manifest
  `benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml`.
- Output alignment is vector index with exact state/count/normal-presence;
  position and normal RMS/L-infinity tolerances are `1e-6`/`2e-6`.
- One warmup pair precedes five alternating measured pairs; each strategy uses
  the median `optimized_ns/reference_ns` ratio and passes useful acceleration
  only at `<= 0.80` with parity and no fallback.
- All four candidates are retained as positive or negative evidence. Only a
  passing strategy may enter ordinary runtime config/UI support; thresholds,
  fixtures, and candidate scope are not retuned after observing results.

## Required changes
- [x] Evaluate an optimized path for each strategy through the explicit
      validation/comparison seam using only the concrete spatial-neighborhood
      policy above; reuse shared kernels and duplicate no weight math. Add the
      public `cpu_optimized` selector only for a passing strategy; none passed.
- [x] The explicit validation/comparison path runs both implementations and
      reports implementation identity, exact state/shape parity, numeric
      deltas, and no-fallback diagnostics. The ordinary API remains
      CPU-reference-only because no candidate passed the adoption gate.
- [x] Determinism preserved: each optimized path is bitwise-deterministic for
      identical `(seed, input, params)` across runs and supported thread
      counts.
- [x] Update a package `method.yaml` to list `cpu_optimized` only if at least
      one strategy it contains passes both gates; all three packages reference
      the comparison benchmark under `benchmarks`. Record the exact
      per-strategy capability matrix in package docs and result diagnostics so
      a package-level backend list never implies unsupported pairs.
- [x] For passing strategies only, extend the delivered `RUNTIME-175`
      config/typed operation and `UI-035` panel with the `cpu_optimized` request and
      requested/actual/fallback diagnostics. Reject unsupported
      strategy/backend pairs during preview; if no strategy passes, leave
      both control surfaces CPU-reference-only.

## Tests
- [x] Extend `tests/unit/geometry/Test.PointCloudConsolidation.cpp` (`unit;geometry`) with parity tests: for each strategy the optimized output matches the reference within the documented tolerance on the standard fixtures.
- [x] Implementation telemetry distinguishes the reference and comparison-only
      optimized candidate. Since no strategy was adopted, there is no ordinary
      `cpu_optimized` request to substitute or fall back silently.
- [x] Existing runtime/config tests continue to round-trip the sole supported
      `cpu_reference` backend identically across Editor, AgentCli, and
      Programmatic sources; no unsupported optimized token is exposed.
- [x] Determinism: optimized output is bitwise-stable across two runs and the supported single-thread comparison budget.
- [x] Fail-closed parity: degenerate inputs return the same explicit failure states as the reference backend.
- [x] Freeze output correspondence/alignment and per-strategy parity tolerances
      before implementation; test optimized decline/fallback separately from
      numeric mismatch (a mismatch is a failure, never fallback).

## Docs
- [x] Add executable comparison manifest
      `benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml`
      (`benchmark_id: geometry.lop_family.comparison.smoke`) with a stable
      built-in family dataset, `intent: performance`, identical
      reference/optimized params, at least one warmup and five alternating
      paired measurements, a declared median statistic, and metrics
      `runtime_ms`/`quality_error_l2`.
- [x] Emit schema-valid results for both implementation identities and commit a
      baseline report naming commit, preset/build type, compiler, host, fixture,
      params, warmup/order, runtime statistic, and per-strategy parity deltas.
      Heavy/nightly corpus work is a separate benchmark task.
- [x] Freeze the useful-acceleration gate before implementation: on the stable
      smoke dataset each strategy's paired median optimized/reference runtime
      ratio must be `<= 0.80`, with no parity failure or fallback. If any
      strategy misses, do not expose `cpu_optimized` for that strategy; record
      the negative result and keep its reference path canonical.
- [x] Record the parity tolerance, concrete acceleration policy, crossover
      behavior, and measured baseline in each package README backend table and
      `reports/`.
- [x] Update the `Geometry.PointCloud.Consolidation` interface docs with the
      comparison-only candidate semantics and the absence of a public backend
      selector after the negative gate.
- [x] Regenerate `docs/api/generated/module_inventory.md` after the validation
      surface changed.

## Acceptance criteria
- [x] All four strategies are evaluated. Every exposed optimized strategy
      matches its reference within the frozen tolerance in the default CPU
      gate; a miss remains reference-only with a recorded negative result.
- [x] Comparison implementation identity is asserted by tests and results;
      requested/actual public-backend telemetry is intentionally absent because
      zero strategies were adopted.
- [x] The comparison manifest and emitted results validate, and the README
      records a reproducible reference-versus-optimized baseline; any speedup
      statement is limited to that declared dataset/host context.
- [x] Every exposed optimized strategy meets the frozen `<= 0.80` paired-median
      runtime ratio on the declared dataset with parity intact; misses remain
      unexposed and are documented as negative evidence.
- [x] If no strategy passes both gates, retire with the validated comparison
      report and no `cpu_optimized` public token or implementation scaffold.
- [x] Public API still exposes only `std`/`glm`/scalar types.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'Consolidation|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No quality/behavior divergence from the reference beyond the documented parity tolerance.
- No performance claim without the committed baseline comparison.
- No `std::rand` or global RNG state; no public Eigen types.

## Verification evidence

- The clean confirmation run at implementation revision `cfd0d9bd` validated
  exact parity, deterministic candidate identity, and no fallback for all four
  strategies. Paired-median candidate/reference ratios were LOP
  `0.961823997`, WLOP `0.966844794`, CLOP `0.998817796`, and EAR
  `1.037145476`; all exceeded the frozen `0.80` adoption threshold.
- The post-BUG-123 focused consolidation/KD-tree/kernel/benchmark selector
  passed 54/54. The normal supported CPU gate passed 4,812/4,812; isolated
  ASan and UBSan gates each passed 2,655/2,655, with the expected LSan-only
  process test skipped by capability policy. The formerly failing UBSan scene
  lifecycle contract also passed 100 consecutive repeats after BUG-123.
- Benchmark manifests/results, method manifests, strict layering and test
  layout, documentation links, generated module inventory, task policy/schema,
  ARA claims, and `git diff --check` are retained as command receipts. The
  original UBSan/ARA discovery failures remain optional diagnostic receipts;
  required post-repair/final receipts carry the disposition.
- Claim-grade experiment custody binds the preregistered protocol, clean source
  revision, raw strategy rows, schema-v2 benchmark result, aggregate decision,
  and independent audit under `tasks/evidence/METHOD-019/experiment/`.

## Maturity
- Target: `ParityProven` for the optimized CPU backend on the declared
  comparison dataset for every strategy that passes. If none passes, retire as
  a negative result without claiming this maturity. `cpu_reference` remains
  canonical truth; `gpu_vulkan_compute` is owned by `METHOD-020`.
- Completed as the specified negative-result stop state: no optimized strategy
  reached `ParityProven` because useful acceleration is part of that maturity
  gate. The comparison candidates remain validation-only evidence and the
  ordinary method/runtime/UI surface remains `cpu_reference`-only.
