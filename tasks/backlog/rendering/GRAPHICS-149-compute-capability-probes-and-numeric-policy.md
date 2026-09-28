---
id: GRAPHICS-149
theme: I
depends_on: []
maturity_target: CPUContracted
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note planned with Fable 5.1 and Codex (2026-09-28); implementation owes its own tests, gpu;vulkan smokes and sealed evidence.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-149 — Compute capability probes and exact-arithmetic shader policy

## Goal
- Probe and expose what the GPU sampling and registration ports need:
  `IDevice::SupportsShaderInt64Atomics()` (enable `VK_KHR_shader_atomic_int64` when present),
  `SupportsSubgroupArithmetic()`, `SubgroupSize()`, `MaxComputeSharedMemoryBytes()`; Null
  returns conservative values. `shaderInt64` is always on and `SupportsShaderFloat64()`
  already exists.
- Document the numeric shader policy in `docs/architecture/compute-parallel-primitives.md`:
  exact-double kernels mark every operation `precise` (SPIR-V NoContraction, matching the CPU
  references' `fp contract(off)`), no float atomics, fixed-order reductions only, GLSL has no
  double `exp` (reuse the `ExpNonPositive` of `property_filter.comp`), long work split into
  bounded dispatches (GPU watchdog).

## Acceptance criteria
- [ ] Probes on `IDevice`, Null defaults tested in the default gate; a gpu;vulkan smoke asserts they match the physical-device query.
- [ ] Policy section written and linked from METHOD-055 and METHOD-056.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'DeviceCapabilit|ComputeCapabilit' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'DeviceCapabilit|ComputeCapabilit' -L 'gpu|vulkan' --timeout 300
```
