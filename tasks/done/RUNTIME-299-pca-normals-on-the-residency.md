---
id: RUNTIME-299
theme: I
depends_on: [RUNTIME-296, GRAPHICS-154]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: slice of RUNTIME-294 planned from ADR 0030 decisions 8-9 (2026-09-30); implementation owes the contract tests and the gpu;vulkan parity + IO smoke listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-299 — Point-set PCA normals on the GPU property residency

## Goal
- RUNTIME-294 row "Point-set PCA normals".
- Input: positions from the canonical residency slot through the LBVH; neighborhoods and PCA fitting remain on the device.
- Output: a vec3 normal ring; Accept through the normals transaction of RUNTIME-296.
- Device scope: unoriented PCA with a deterministic largest-magnitude-positive sign rule (axis ties x, y, z). MST is explicitly refused with a CPU-backend reason; no viewpoint mode exists.
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: at least three live finite vec3 samples on any element domain. |
| Compatible entity sources | Every canonical point domain (mesh vertex/edge/halfedge/face, graph node/edge/halfedge, point cloud). |
| RuntimeModule | `Extrinsic.Runtime.NormalOperations` (existing). |
| Config/agent | Unchanged backend enum (`vulkan_lbvh` becomes the residency path); IO counters in the result and agent output. |
| UI | Normal Estimation window: Accept / Discard, observation state, IO counters. |
| Publication | Same-domain vec3 output. GPU preview: no (vec3 rings are not observed); commit via the normals transaction. |
| End-to-end tests | Contract tests on the mock device; gpu;vulkan parity smoke on every domain against the CPU reference (unoriented); contracts cover explicit MST refusal. |

## Completion — 2026-09-30
Commit: see RETIREMENT-LOG (`claude/cpd-nystrom`). Point-set PCA normals run on the GPU
property residency (`vulkan_lbvh`):
- Neighbors come from the canonical positions through `lbvhQueryDouble` (double ranking).
- The covariance is accumulated in double. CPU and GPU share one portable double eigensolver
  (closed form, with a Jacobi fallback for repeated roots).
- A deterministic sign rule applies on both sides (largest-magnitude component positive;
  ties go to the lowest axis).
- The vec3 ring is accepted through `EditorNormalTransaction`.
- MST orientation is refused on the device with a CPU-backend reason, and there is no
  viewpoint mode. The old `vulkan_lbvh` CPU-fit path is removed.
- Radius rows stop at the first overflow and are heapsorted. Pages are completion-gated
  framed submissions of 64..4096 rows.

Engine fixes found along the way:
- A device loss on the RTX 3050: 1026 identical points under a radius query inserted every
  candidate in a single submission and hit the watchdog.
- A general shutdown use-after-free: `Engine::Shutdown` destroyed the device while the
  JobService still held job callbacks that owned GPU workspaces.
  - `JobService::CancelAndDrain` now cancels, joins, finalizes and releases every callback
    before module and device teardown.
  - Submissions stay rejected until `Engine::Initialize` calls `ResumeSubmissions`.

GEOM-114 was filed for the pre-existing absolute isotropic PCA cut-off.

Evidence:
- Contract and unit tests: `NormalTransaction.Pca*`, `PointNormalsWorkspace.*`,
  `RuntimeJobService.ShutdownJoinsWorkFinalizesAndReleasesEveryCallback`,
  `EngineShutdownReleasesPendingGpuCaptureBeforeDevice`, the sign-rule test and the BVH
  double tie tests.
- gpu;vulkan `RUNTIME299PointNormalsResidency`: measured delta 0 (components and angle) for
  kNN and radius, zero-upload second run, Discard. `PointLBVHGpuSmoke.NormalNeighborhoods`
  passes again, including the dense-overflow phase.
- Implemented by Codex 6 Astra (medium/high), reviewed by Claude Opus 5.5 in two rounds. The
  4096-point radius cap the implementer had added was removed; radius pages like kNN.
- CPU gate 5401/5401. GPU suite green except the environmental `VulkanShutdownLsanContract`
  (1 opt-in skip). Operational.

## Acceptance criteria
- [x] gpu;vulkan parity smoke: the accepted rows equal the CPU reference within a stated, justified tolerance.
- [x] IO counters: a second run on the same input revision uploads zero input bytes (the residency reports uploads, hits and any declared CPU-stage bytes in the result and the agent output).
- [x] Panel Accept / Discard (Accept disabled with its reason when stale); batch and agent commands accept automatically.
- [x] `method.engine-integration` publication row states "GPU preview: yes/no; commit via X" and the method docs record the backend identity and parity delta.
- [x] MST + `vulkan_lbvh` is explicitly refused with a CPU-backend reason; unoriented runs have no CPU fit/orientation stage or per-iteration input uploads. No viewpoint mode is exposed.

## Verification
```bash
cmake --build build/ci -j$(nproc)
cmake --build build/ci-vulkan -j$(nproc)
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
# On the RTX host only (not executed in this session):
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```

## Review follow-up
- Operator-directed scope: repair the RTX 3050 watchdog/device-loss path and shutdown lifetime, plus the independent review findings; leave changes uncommitted.
- Behavior change: default MST + `vulkan_lbvh` previously worked through CPU fitting/orientation. The resident implementation now refuses that combination explicitly; select a CPU backend for MST.
- Reuse: extend `lbvhQueryDouble` with overflow-only counting for normals; full-count scalar/outlier callers retain their behavior. Reuse `SpatialIndexCache::QueueGpuCompute` for completion-gated pages and `JobService` finalizers plus `Core::Tasks::Scheduler::WaitForAll` for shutdown. No new scheduling owner.
- Radius work: append until overflow, heapsort complete rows, 64 rows per submission, and a 4096-live-point admission limit with a CPU-backend reason. kNN pages clamp `GpuQueryBatchSize` to 64..4096. Intermediate pages never publish; see the bounded-work analysis in [normal estimation](../../docs/architecture/normal-estimation.md#resident-pca-vulkan_lbvh).
- Shutdown: cancel/join Work, finalize unpublished jobs once, and drop callbacks/results while module services and device remain alive.
- The isotropic scale defect is deliberately unchanged and tracked by [GEOM-114](../backlog/geometry/GEOM-114-scale-relative-pca-isotropic-cutoff.md).
- Review scope: layer imports, CMake ownership, API direction and renderer ownership pass inspection; no new render passes, recipe edges, exceptions or maturity closure. GPU revalidation remains pending on the RTX host; this session runs no GPU tests.
