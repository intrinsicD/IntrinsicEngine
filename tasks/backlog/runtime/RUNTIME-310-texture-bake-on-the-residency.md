---
id: RUNTIME-310
theme: I
depends_on: [GRAPHICS-155]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: slice of RUNTIME-294 planned from ADR 0030 decisions 8-9 (2026-09-30); implementation owes the contract tests and the gpu;vulkan parity + IO smoke listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-310 — Property texture bake on the GPU property residency

## Goal
- RUNTIME-294 row "Texture bake": already fully GPU (no CPU stage).
- Input: the baked property's values from its canonical residency slot instead of a private host-visible upload; output: the texture (no ring; the texture itself is the preview).
- ADR 0030 decisions 8-9: the kernels record from `GpuPropertyView` inputs into ring outputs, never from CPU spans; `RequestedBackend` / `ActualBackend`, fallback reasons and the residency IO counters stay uniform. The CPU reference stays canonical; the GPU backend reports its identity and parity delta.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged: a typed property on the mesh's vertex or corner domain with resolved texcoords. |
| Compatible entity sources | Meshes with texcoords. |
| RuntimeModule | The existing property-texture bake operations. |
| Config/agent | Unchanged; IO counters in the result and agent output. |
| UI | Bake panel: IO counters. |
| Publication | A texture asset. GPU preview: yes (the texture); commit: none beyond the existing bake result. |
| End-to-end tests | Contract tests on the mock device; one gpu;vulkan IO smoke (identical texture bytes with and without the residency route). |

## Acceptance criteria
- [ ] gpu;vulkan IO smoke: the baked texture bytes are identical with and without the residency route (the bake reads the canonical slot; no readback, no CPU publication).
- [ ] IO counters: a second run on the same input revision uploads zero input bytes (the residency reports uploads, hits and any declared CPU-stage bytes in the result and the agent output).
- [ ] No Accept / Discard: the bake result is the texture as today (batch, agent and panel unchanged); the panel shows the IO counters.
- [ ] `method.engine-integration` publication row states "GPU preview: yes (the texture); commit: none" and the docs record the backend identity.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
