---
id: GRAPHICS-151
theme: I
depends_on: [GRAPHICS-148]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note split from GRAPHICS-148 (2026-09-29); implementation owes its own tests and gpu;vulkan smoke.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-151 — Sorted segment heads and fixed-order segment reduction

## Goal
- On radix-sorted records (GRAPHICS-148), mark segment heads (`key[i] != key[i-1]`) and
  compact them with the existing stream compaction into segment offsets, so sorted keys
  replace GPU hash tables (METHOD-014's racy hash, METHOD-059, METHOD-062).
- Reduce values per sorted segment in fixed order from those offsets; keep
  `parallel_segmented_float_reduce.comp` (one workgroup scans every element per segment)
  for small inputs.
- Reuse candidate found during GRAPHICS-148: `lop_grid_count.comp` / `lop_grid_scatter.comp`
  bucket points into cells with atomic cursors (order within a cell depends on scheduling);
  evaluate replacing them with a radix sort by cell key plus segment heads.

- First production consumer: the k-means GPU update. `assets/shaders/kmeans_update.comp` runs one
  64-lane workgroup per centroid that scans every point and tests `labels[row]==c`, so the
  update is O(k*N), and `kmeans_reset.comp` reduces over N in a single workgroup (host side
  `Runtime.ClusteringGpuState.cpp`, `Runtime.ClusteringGpuBackend.cpp`). The shared
  `parallel_segmented_float_reduce.comp` has the same O(segments*N) shape and today no
  production consumer (tests only; `RecordGpuSegmentedFloatReduction` in
  `Graphics.ComputeParallelPrimitives.cppm`). Rebuild the reduction as radix sort by label ->
  segment heads/offsets -> fixed-order per-segment sums, with a dvec3 variant, and adopt it in
  k-means. Keep the fixed-order, no-floating-atomics determinism the current kernel documents
  and declare any parity delta against the CPU reference. Observed by the 2026-10-01 audit; the
  O(k*N) cost is inferred from the kernel, not measured, so record a k-means timing before and
  after rather than claiming a speedup.
- Note: `RecordGpuRadixSort` is used only by `Graphics.PointLBVH.cpp`, `RecordGpuPrefixScan` only by
  `PointCloudConsolidationGpu.cpp`; nine renderer files import the module only for
  `CreateComputePipeline`. Consider a shared `include/workgroup_reduce.glsl` (fixed-order tree
  sum and index-tie-break argmax) for the seven duplicated workgroup reductions
  (`kmeans_update`, `kmeans_reset`, `point_sampling_farthest`, `sparse_cg`, `lbvh_bounds`,
  `point_keypoints`, `parallel_segmented_float_reduce`) only if this work touches them.

## Acceptance criteria
- [ ] CPU oracle and plan tests in the default gate; heads and offsets equal the CPU oracle on
      empty, single, all-equal and random keys (one and two key words).
- [ ] gpu;vulkan smoke: three runs identical; reduction in fixed order bitwise repeatable.
- [ ] LOP grid bucketing decision recorded (replace, or keep with the measured reason).
- [ ] k-means GPU update uses the sorted-segment reduction (no per-centroid scan of all points), matches the CPU reference within a declared delta, and before/after timings are recorded without a speedup claim beyond them.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitives' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'ComputeParallelPrimitives' -L 'gpu|vulkan' --timeout 300
```
