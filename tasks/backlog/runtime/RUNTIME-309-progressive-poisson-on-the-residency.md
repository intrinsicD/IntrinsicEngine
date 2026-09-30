---
id: RUNTIME-309
theme: I
depends_on: [RUNTIME-293]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: slice of RUNTIME-294 planned from ADR 0030 decisions 8-9 (2026-09-30); implementation owes the contract tests and the gpu;vulkan parity + IO smoke listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-309 — Progressive Poisson sampling on the GPU property residency

## Goal
- RUNTIME-294 row "Progressive Poisson": already fully GPU (no CPU stage).
- Input: positions from the canonical residency slot instead of a private upload; the output is a cardinality change published atomically through the existing publication (no ring, no preview).
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: a count-matched vec3 position property on any point domain. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | `Extrinsic.Runtime.PointSamplingOperations` (existing). |
| Config/agent | Unchanged backend enum; IO counters in the result and agent output. |
| UI | Sampling panel: IO counters (no Accept / Discard: the subset publishes atomically on completion). |
| Publication | Cardinality change (selected subset). GPU preview: no; commit via the existing atomic publication. |
| End-to-end tests | Contract tests on the mock device; one gpu;vulkan parity + IO smoke. |

## Acceptance criteria
- [ ] gpu;vulkan parity smoke: the published subset (order / mask) equals the CPU reference for the same seed and parameters.
- [ ] IO counters: a second run on the same input revision uploads zero input bytes (the residency reports uploads, hits and any declared CPU-stage bytes in the result and the agent output).
- [ ] No Accept / Discard: the cardinality change publishes atomically on completion through the existing publication (batch, agent and panel unchanged); the panel shows the IO counters.
- [ ] `method.engine-integration` publication row states "GPU preview: no; commit via the existing atomic publication" and the method docs record the backend identity and parity delta.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
