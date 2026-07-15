---
id: GEOM-067
theme: B
depends_on:
  - REVIEW-003
maturity_target: CPUContracted
---
# GEOM-067 — Memory-aware BVH and merged-node evidence

## Goal

- Determine whether SAH construction and memory-priced merged-node packing
  improve exact CPU BVH queries over the current median-split binary layout on
  fixed coherent and incoherent workloads.

## Non-goals

- No replacement of `Geometry.BVH`'s default builder or public node layout in
  this task.
- No RHI, Vulkan ray-tracing, BLAS/TLAS, custom hardware traversal, shader, or
  renderer changes; archived `GRAPHICS-045` remains the hardware RT plan.
- No claim that a CPU packed layout predicts proprietary hardware RT behavior.
- No `IBvhBuilder`, backend registry, factory, or second spatial-query API.
- No performance claim from node count alone or without exact-query parity.

## Context

- Owner/layer: `geometry` for experimental CPU construction/packing records
  and exact traversal; `benchmarks/geometry` for evidence over public APIs.
- The current `Geometry.BVH` is a deterministic median-split binary tree with
  an array-of-`Node` layout. The Issue 445 paper proposes a memory-based SAH
  (MSAH) and merged wide blocks that price bytes/data movement rather than
  treating all node visits equally.
- The paper's reported rendering gains come from a cycle-level simulator and
  do not constitute IntrinsicEngine CPU/GPU evidence. This task tests the
  transferable layout/cost hypothesis only.
- Sources: [Graphics Programming Weekly Issue 445](https://www.jendrikillner.com/post/graphics-programming-weekly-issue-445/)
  and [Memory-Efficient BVHs with Merged Nodes project/paper](https://graphics.cs.utah.edu/research/projects/bvh-merged-nodes/).
- Cross-links: retired `GEOM-039` consumes `Geometry.BVH` for exact nearest
  faces; open `METHOD-003`, `METHOD-004`, `METHOD-005`, and `METHOD-007` rely
  on the existing public BVH contract and must not observe semantic drift.

## Right-sizing

- Element under evaluation: three concrete builders/layouts behind plain
  parameter/result records and free functions in one focused module.
- Simpler alternative: keep the current BVH as canonical and construct
  temporary median, SAH, and MSAH-packed representations only in the evidence
  runner; no general backend abstraction is justified.
- Blast radius: one geometry implementation surface, focused tests, benchmark
  manifest/runner, and evidence report. Existing consumers remain unchanged.
- Reintroduction trigger: a later default-layout task may open only if the
  preregistered parity and win thresholds pass on representative workloads.

## Required changes

- [ ] Define deterministic experimental median, binned-SAH, and MSAH builders
      over the same AABB inputs, plus a packed merged-node record with explicit
      byte size, alignment, child encoding, and leaf encoding.
- [ ] Keep `Geometry.BVH` as the baseline/default. Experimental construction
      and traversal must be directly comparable without changing existing
      callers or result ordering.
- [ ] Generate fixed coherent ray/AABB query streams, incoherent random and
      secondary-ray-like streams, overlap queries, coincident-centroid cases,
      and adversarial elongated/disconnected distributions from fixed seeds.
- [ ] Compare every result against brute force and the existing BVH using
      sorted element IDs. Record mismatches as correctness failures, not
      performance diagnostics.
- [ ] Instrument build time, traversal time, node/leaf tests, tree depth,
      logical bytes read, estimated cache lines touched, packed size, and
      temporary build memory. Declare cache-line and block-size assumptions in
      the manifest/result.
- [ ] Add stable workloads `geometry.bvh.memory_aware.smoke` and
      `geometry.bvh.memory_aware.nightly`, with checked-in/builtin smoke data,
      fixed seeds, warmup policy, backend identity, and machine-readable
      metrics/diagnostics.
- [ ] Preregister the promotion threshold before timing: exact parity is
      mandatory; a later default-change task requires at least a 10% measured
      median traversal-time improvement or a 15% estimated-cache-line
      improvement over the best baseline on two nontrivial incoherent fixtures.
      Regardless of which clause passes, measured median traversal time may
      regress by no more than 5% on every other required incoherent and coherent
      fixture, and the grouped 95% bootstrap upper confidence bound on relative
      regression may not exceed 10%. A byte/cache-line win that violates either
      measured-time cap is a rejection, not a promotion result.
- [ ] Record whether observed timing correlates more strongly with bytes/cache
      lines or conventional node visits, including negative results.

## Tests

- [ ] Unit tests prove deterministic construction, packed-record encode/decode,
      empty/invalid/non-finite failure states, coincident centroids, and exact
      result parity for every layout.
- [ ] Regression coverage runs existing `BVH`, mesh-closest-face, and boundary
      coincident-ray suites unchanged.
- [ ] Smoke thresholds are correctness and broad sanity bounds only; timing
      promotion evidence comes from repeated baseline-comparable runs and the
      nightly workload.
- [ ] Repeated timing tests use frozen warmups/sample counts and grouped
      bootstrap resampling, and assert that the per-fixture measured traversal
      regression caps are applied to incoherent as well as coherent streams.
- [ ] Manifests and result JSON validate and declare dataset, parameters,
      warmups, host/toolchain, baselines, and all quality/runtime metrics.

## Docs

- [ ] Document experimental layout semantics and limitations without changing
      the canonical `Geometry.BVH` contract.
- [ ] Add an evidence report under `docs/reports/` with per-workload tables,
      baseline conditions, measured-time confidence intervals, correlations,
      threshold verdict, and explicit simulator-vs-real-host limitations.
- [ ] Document both stable benchmark IDs and PR-fast versus nightly routing.

## Acceptance criteria

- [ ] Median, SAH, and MSAH-packed traversals return exactly the brute-force
      result sets on every fixture and fixed query stream.
- [ ] Machine-readable results contain build/traversal, memory-traffic, size,
      and correctness metrics for coherent and incoherent workloads.
- [ ] The report applies the preregistered threshold and records a positive or
      negative conclusion without changing the default BVH; estimated
      byte/cache-line improvement cannot override a measured traversal
      regression-cap failure on any required trace.
- [ ] No RHI/RT/renderer surface or existing BVH consumer changes.
- [ ] Default CPU and structural gates pass.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'BVH|MeshClosestFace' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- Making SAH/MSAH or the merged layout the default without a separate task and
  passing evidence threshold.
- Exposing a new builder interface/registry or duplicating spatial-query
  semantics.
- Treating estimated byte traffic as measured hardware traffic.
- RHI, Vulkan RT, BLAS/TLAS, shader, or renderer work.
- Performance claims without exact parity, baseline, and declared conditions.

## Maturity

- Target: `CPUContracted`; the intended endpoint is a validated evidence lane
  and adoption verdict. No `Operational` follow-up is owed.
