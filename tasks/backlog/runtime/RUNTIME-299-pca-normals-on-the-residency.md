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
- Input: positions from the canonical residency slot through the LBVH (today `vulkan_lbvh` downloads neighborhoods and fits on the CPU).
- Output: a vec3 normal ring; Accept through the normals transaction of RUNTIME-296.
- CPU stage today: covariance and orientation. Port: unoriented and viewpoint orientation on the device (closed-form symmetric 3x3 eigenvectors); MST orientation stays a declared CPU stage (the readback it needs is reported; parallel Boruvka later).
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
| End-to-end tests | Contract tests on the mock device; gpu;vulkan parity smoke on every domain against the CPU reference (unoriented, viewpoint, MST). |

## Acceptance criteria
- [ ] gpu;vulkan parity smoke: the accepted rows equal the CPU reference within a stated, justified tolerance.
- [ ] IO counters: a second run on the same input revision uploads zero input bytes (the residency reports uploads, hits and any declared CPU-stage bytes in the result and the agent output).
- [ ] Panel Accept / Discard (Accept disabled with its reason when stale); batch and agent commands accept automatically.
- [ ] `method.engine-integration` publication row states "GPU preview: yes/no; commit via X" and the method docs record the backend identity and parity delta.
- [ ] MST orientation reports its CPU-stage bytes; unoriented and viewpoint runs upload nothing per iteration.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
