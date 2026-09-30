---
id: RUNTIME-305
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
# RUNTIME-305 — ICP on the GPU property residency

## Goal
- RUNTIME-294 row "ICP".
- Input: source and target positions (and normals) from canonical residency slots through the LBVH; output: a pose, previewed through the existing moving-source transform preview; commit through the existing registration apply.
- CPU stage today: Kabsch per iteration. Port (medium): correspondence sums reduced on the device (fixed-order reduction); a matrix up and a convergence value back per iteration, both reported.
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: two vec3 position sets (optional target normals) on any point domains. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds. |
| RuntimeModule | `Extrinsic.Runtime.RegistrationOperations` (existing). |
| Config/agent | Unchanged backend enum; IO counters (per-iteration matrix and convergence bytes) in the result and agent output. |
| UI | Registration panel: Accept / Discard, observation state, IO counters. |
| Publication | A pose on the moving entity. GPU preview: yes (transform preview); commit via the existing registration apply (undoable). |
| End-to-end tests | Contract tests on the mock device; one gpu;vulkan parity + IO smoke. |

## Acceptance criteria
- [ ] gpu;vulkan parity smoke: the accepted pose equals the CPU reference within a stated, justified tolerance (rotation and translation), with the same iteration count and residual.
- [ ] IO counters: a second run on the same input revisions uploads zero input bytes; the per-iteration matrix upload and convergence readback bytes are reported in the result and the agent output.
- [ ] Panel Accept (apply) / Discard of the previewed pose through the existing registration apply; batch and agent commands apply automatically.
- [ ] `method.engine-integration` publication row states "GPU preview: yes (transform preview); commit via the existing registration apply" and the method docs record the backend identity and parity delta.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
