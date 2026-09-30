---
id: RUNTIME-302
theme: I
depends_on: [RUNTIME-296, RUNTIME-292]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: slice of RUNTIME-294 planned from ADR 0030 decisions 8-9 (2026-09-30); implementation owes the contract tests and the gpu;vulkan parity + IO smoke listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-302 — FPFH descriptors on the GPU property residency

## Goal
- RUNTIME-294 row "FPFH".
- Input: positions and normals from canonical residency slots through the LBVH; output: a descriptor ring (the bins as a typed multi-channel property) with the selected bin previewed as a scalar; Accept through a typed transaction.
- CPU stage today: SPFH / FPFH. Port (medium): two kernels (SPFH per point, weighted FPFH sum in fixed neighbor order).
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: vec3 positions and normals on any point domain. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | `Extrinsic.Runtime.PointAnalysisOperations` descriptor operations (existing). |
| Config/agent | Unchanged backend enum; IO counters in the result and agent output. |
| UI | Descriptor panel: Accept / Discard, observation state, IO counters. |
| Publication | Descriptor bins on the input domain, same cardinality. GPU preview: yes for the displayed bin (colormap scalar); commit via the typed property transaction. |
| End-to-end tests | Contract tests on the mock device; one gpu;vulkan parity + IO smoke. |

## Acceptance criteria
- [ ] gpu;vulkan parity smoke: the accepted rows equal the CPU reference within a stated, justified tolerance.
- [ ] IO counters: a second run on the same input revision uploads zero input bytes (the residency reports uploads, hits and any declared CPU-stage bytes in the result and the agent output).
- [ ] Panel Accept / Discard (Accept disabled with its reason when stale); batch and agent commands accept automatically.
- [ ] `method.engine-integration` publication row states "GPU preview: yes/no; commit via X" and the method docs record the backend identity and parity delta.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
