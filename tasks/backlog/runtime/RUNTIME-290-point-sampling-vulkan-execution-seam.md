---
id: RUNTIME-290
theme: I
depends_on: [GEOM-111]
maturity_target: CPUContracted
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note planned with Fable 5.1 and Codex (2026-09-28); implementation owes its own tests, gpu;vulkan smokes and sealed evidence.
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources]
---
# RUNTIME-290 — Shared Vulkan execution seam for Geometry.PointSampling

## Goal
- One runtime module `src/runtime/Modules/PointSampling/Runtime.PointSamplingGpu.{cppm,cpp}`
  owning upload (float positions, optional weights), persistent per-run buffers, the
  JobService GPU participant, chunked multi-dispatch rounds (no persistent kernels: GPU
  watchdog), `GpuTransfer` readback of order and clearances, the progressive `Extend`
  contract (published prefixes never change) and the parity check against the CPU order
  before reporting `gpu_vulkan_compute`. Method shaders plug in as pipeline sets (METHOD-014,
  055, 060, 061, 062). `Geometry.PointSampling` stays CPU-only; execution selection lives here.

## Acceptance criteria
- [ ] Requested, actual and fallback backend reporting; CPU fallback tests in the default gate; stale-result rejection.
- [ ] RUNTIME-289 (consumers) and RUNTIME-274 (panel) take their backend selector from this seam.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'PointSampling' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'PointSampling' -L 'gpu|vulkan' --timeout 300
```
