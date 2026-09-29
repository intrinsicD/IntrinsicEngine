---
id: RUNTIME-290
theme: I
depends_on: [GEOM-111]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Finished interactively; correctness is the default-gate fallback tests and the gpu;vulkan smoke (bitwise CPU parity, chunked prefixes, three identical editor runs); no performance claim is made.
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources]
---
# RUNTIME-290 — Shared Vulkan execution seam for Geometry.PointSampling

## Completion — 2026-09-29
Commit: the RUNTIME-290 commit on `claude/cpd-nystrom`. Operational: `Runtime.PointSamplingGpu`
(support check, chunked `PointSamplingGpuRun` with growing prefixes and a 64-sample CPU
recomputation before `gpu_vulkan_compute`) with the first device kernel, exact (weighted)
farthest point (`Graphics.FarthestPointSampling`, `point_sampling_farthest.comp`, bitwise equal to
the CPU order and clearances); the editor operation's `backend` field, panel combo and agent
fields (`requested_backend`, `backend`, `backend_diagnostic`). Deviations: buffers are per-run
host-visible allocations through the device (no BufferManager leases), readback goes through
`SpatialIndexCache::QueueGpuCompute` rather than a module participant, and the brute-force kernel
is O(N k) (METHOD-055's hole sieve replaces it). The RUNTIME-289 consumers run inside CPU solver
steps and keep the CPU; RUNTIME-289 owns that axis (a worker-side handoff like METHOD-056's
broker).

Reuse decisions: the framed GPU job is now one helper, `EditorFeatureDetail::MakeFramedGpuJobDesc`
(keypoints migrated; smoothing's CG chunk loop left as a candidate); compute workspaces create
their pipelines through `Graphics::CreateComputePipeline` (property filter, sparse CG, keypoints,
CPD E-step, LBVH, farthest point).

## Goal
- One runtime module `src/runtime/Modules/PointSampling/Runtime.PointSamplingGpu.{cppm,cpp}`
  owning upload (float positions, optional weights), persistent per-run buffers, the
  JobService GPU participant, chunked multi-dispatch rounds (no persistent kernels: GPU
  watchdog), `GpuTransfer` readback of order and clearances, the progressive `Extend`
  contract (published prefixes never change) and the parity check against the CPU order
  before reporting `gpu_vulkan_compute`. Method shaders plug in as pipeline sets (METHOD-014,
  055, 060, 061, 062). `Geometry.PointSampling` stays CPU-only; execution selection lives here.

## Acceptance criteria
- [x] Requested, actual and fallback backend reporting; CPU fallback tests in the default gate; stale-result rejection. (Stale framed jobs skip their chunks and deliver StaleEntity through the shared helper, exercised by the keypoint stale smoke.)
- [x] RUNTIME-289 (consumers) and RUNTIME-274 (panel) take their backend selector from this seam. (RUNTIME-274: done. RUNTIME-289: the consumers' samples are taken inside CPU solver steps; their backend axis stays open in RUNTIME-289.)

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'PointSampling' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'PointSampling' -L 'gpu|vulkan' --timeout 300
```
