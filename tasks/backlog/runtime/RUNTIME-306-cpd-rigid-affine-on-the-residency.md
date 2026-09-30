---
id: RUNTIME-306
theme: I
depends_on: [RUNTIME-293, GRAPHICS-156]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: slice of RUNTIME-294 planned from ADR 0030 decisions 8-9 (2026-09-30); implementation owes the contract tests and the gpu;vulkan parity + IO smoke listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-306 — CPD rigid / affine on the GPU property residency

## Goal
- RUNTIME-294 row "CPD rigid/affine".
- Input: the target as float-float once from the canonical slot, the moving source from its slot; output: the moving-source position ring previewed through the `GpuWorld` position preview; Accept through the positions run API.
- CPU stage today: the M-step sums. Port (medium): M-step sums on the device (fixed-order reductions); the tiny SVD on the CPU or a one-thread kernel, its bytes reported.
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: two vec3 position sets on any point domains. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | `Extrinsic.Runtime.RegistrationOperations` CPD (existing). |
| Config/agent | Unchanged backend enum; IO counters in the result and agent output. |
| UI | CPD panel: Accept / Discard, observation state, IO counters. |
| Publication | Moving-source positions, same cardinality. GPU preview: yes (positions); commit via the positions run API. |
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
