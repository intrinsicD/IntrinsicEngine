---
id: GRAPHICS-149
theme: I
depends_on: []
maturity_target: CPUContracted
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Finished interactively; evidence is Test.DeviceCapabilityDefaults.cpp (default gate) and Test.ComputeCapabilitiesGpuSmoke.cpp (gpu;vulkan, passed on the RTX host under Xephyr with subgroup 32 with arithmetic, 48 KiB shared memory, int64 atomics, float64).
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-149 — Compute capability probes and exact-arithmetic shader policy

## Completion — 2026-09-28
Commit: the GRAPHICS-149 commit on `claude/cpd-nystrom`. Maturity reached: `CPUContracted`
for the defaults with a passing gpu;vulkan smoke of the Vulkan probes. The smoke checks the
Vulkan guarantees (shared memory >= 16 KiB, power-of-two subgroup) rather than re-querying
the physical device, which the test cannot reach through the RHI.

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
- [x] Probes on `IDevice`, Null defaults tested in the default gate; a gpu;vulkan smoke asserts they match the physical-device query.
- [x] Policy section written and linked from METHOD-055 and METHOD-056.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'DeviceCapabilit|ComputeCapabilit' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'DeviceCapabilit|ComputeCapabilit' -L 'gpu|vulkan' --timeout 300
```
