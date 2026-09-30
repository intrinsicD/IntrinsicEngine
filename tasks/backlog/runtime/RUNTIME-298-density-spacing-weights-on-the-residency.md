---
id: RUNTIME-298
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
# RUNTIME-298 — Kernel density, point spacing and density weights on the GPU property residency

## Goal
- RUNTIME-294 row "Kernel density, point spacing, density weights" (one task: the three share one neighbor kernel over the LBVH).
- Input: positions from the canonical residency slot through the LBVH.
- Output: a scalar ring per method (observed by the colormap); Accept through the scalar transaction.
- CPU stage today: reductions on downloaded neighborhoods. Port: one kernel over the LBVH neighbors with fixed-order sums.
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: a count-matched vec3 position property on any point domain. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | `Extrinsic.Runtime.PointFieldOperations` / `PointAnalysisOperations` (existing density, spacing and weight operations). |
| Config/agent | Unchanged backend enums; IO counters in the result and agent output. |
| UI | The three panels: Accept / Discard, observation state, IO counters. |
| Publication | One scalar on the input domain, same cardinality. GPU preview: yes (colormap scalar); commit via the scalar transaction. |
| End-to-end tests | Contract tests on the mock device; one gpu;vulkan parity + IO smoke covering the three methods. |

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
