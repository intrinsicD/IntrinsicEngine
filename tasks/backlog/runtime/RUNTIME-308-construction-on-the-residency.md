---
id: RUNTIME-308
theme: I
depends_on: [RUNTIME-292, GRAPHICS-154]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: slice of RUNTIME-294 planned from ADR 0030 decisions 8-9 (2026-09-30); implementation owes the contract tests and the gpu;vulkan parity + IO smoke listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-308 — Surface construction (Hoppe) on the GPU property residency

## Goal
- RUNTIME-294 row "Construction (Hoppe)".
- Input: positions and normals from canonical residency slots through the LBVH; output: a mesh published atomically (no ring; a topology change).
- CPU stage today: the distance field and marching cubes. Port (large): device distance field and marching cubes with variable-size output (compaction, dynamic sizes); the readback is the terminal mesh only.
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: vec3 positions and normals on any point domain. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | `Extrinsic.Runtime.PointConstructionOperations` (existing). |
| Config/agent | Unchanged backend enum; IO counters in the result and agent output. |
| UI | Construction panel: IO counters (no Accept / Discard: the mesh publishes atomically on completion). |
| Publication | A new mesh entity / topology replacement. GPU preview: no; commit via the existing atomic mesh publication. |
| End-to-end tests | Contract tests on the mock device; one gpu;vulkan parity + IO smoke. |

## Acceptance criteria
- [ ] gpu;vulkan parity smoke: the published mesh equals the CPU reference (vertex positions within a stated tolerance, identical face count and connectivity for the same grid).
- [ ] IO counters: a second run on the same input revision uploads zero input bytes (the residency reports uploads, hits and any declared CPU-stage bytes in the result and the agent output).
- [ ] No Accept / Discard: the mesh publishes atomically on completion through the existing publication (batch, agent and panel unchanged; undoable as today); the panel shows the IO counters.
- [ ] `method.engine-integration` publication row states "GPU preview: no; commit via the existing atomic mesh publication" and the method docs record the backend identity and parity delta.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
