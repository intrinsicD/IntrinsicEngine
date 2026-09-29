---
id: GRAPHICS-153
theme: I
depends_on: [GRAPHICS-150]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive performance work requested by the operator (2026-09-29); each slice owes bitwise/parity gpu;vulkan smokes, and the resolver slice a latency/IO comparison.
contract_schema: 1
contracts: [geometry.element-domain-sources, repo.source-documentation]
---
# GRAPHICS-153 — GPU methods reuse resident buffers; CPU<->GPU IO only at start and end

## Goal
- Operator (2026-09-29): GPU is not always faster than the CPU. Every GPU method must reuse
  buffers that already exist (an entity's resident vertex positions when `v:position` is the
  selected property), move data between CPU and GPU only when necessary (at most at the start
  and end of a method), and recycle every temporary buffer inside its loops (k-means, and
  every other method that loops on the GPU).
- Survey (2026-09-29, Explore): k-means and LOP already record all iterations into one
  submission with one readback (reference pattern), but re-upload positions the renderer
  holds. Violations:
  - WLOP projection: every iteration drops `Moving`, so `CreateWorkspace` rebuilds the CPU
    LBVH, a new `PointLbvhWorkspace` with its pipelines and uploads the points again; its
    query batches are recreated and self-queries re-uploaded.
  - Pipelines are created per workspace instance (per LBVH entry, per job for keypoints, FPS,
    property filter, CG, CPD).
  - SparseCG reads back all solutions every chunk; FPS reads back the whole result per chunk;
    CPD reads back the full E-step and uploads the moved source every iteration.
  - SparseCG `Begin` appends buffers on reuse; PropertyFilter is strictly one-shot; CPD
    `Ensure` recreates on any size change; paged radius queries reallocate the short last
    page; paged queries re-upload the index's own points as queries.
  - ICP uploads the transformed source every iteration instead of a matrix.
  - Nothing reads the renderer's resident positions (`GpuWorld` managed vertex pool, SoA
    float3 position stream per geometry, `TryGetGeometryResidencyView`); the entity ->
    geometry mapping and observed `v:position` revision sit in the Engine-private
    `RenderExtractionCache`.

## Slices
1. Loops recycle: WLOP keeps its moving-point index across iterations (points updated in place,
   rebuild into reserved storage) and its query batches; paged queries reuse the batch for a
   short last page and address self-queries through the index's positions.
2. One device-lifetime compute pipeline set per kernel kind, shared by all workspace instances.
3. Capacity-grown scratch: CPD `Ensure`, SparseCG `Begin`, PropertyFilter `Record` reuse
   their buffers; no allocation inside a loop.
4. Readback trimming: CG reads the current report per chunk and the solutions once; FPS reads
   only the new tail; ICP uploads a matrix and transforms on the GPU.
5. GPU property view resolver: `v:position` of a resident, current, row-aligned geometry
   resolves to the renderer's buffer (+offset, stride, count, revision); otherwise one cached
   upload keyed by entity/property/revision. Consumers: SpatialIndexCache GPU build,
   k-means, LOP, FPS, texture bake. Lifetime of pool blocks across immediate submits checked.
6. Measure: per-method upload/readback bytes and wall time before/after on the Vlasic meshes
   and the CPD scaling fixture; sealed evidence.

## Acceptance criteria
- [ ] No buffer or pipeline is created inside an iteration loop of any GPU method (audited list
      in this note, contract tests on reuse counters where the workspace exposes them).
- [ ] Per-iteration CPU<->GPU traffic is limited to what the algorithm's CPU half needs
      (CPD, ICP with a CPU solve) and documented per method; all others move data only at
      start and end.
- [ ] Selecting `v:position` of a resident, current entity uploads no positions (IO counter).
- [ ] GPU results unchanged: existing parity smokes pass bitwise where they were bitwise.
- [ ] Before/after IO and time table recorded.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 600
```
