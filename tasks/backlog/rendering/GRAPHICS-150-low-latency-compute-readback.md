---
id: GRAPHICS-150
theme: I
depends_on: [METHOD-056]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from METHOD-056 measurements (2026-09-29); implementation owes its own gpu;vulkan smoke and a sealed latency comparison.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-150 — Low-latency compute submit and readback for iterative GPU jobs

## Goal
- Iterative GPU work driven from a CPU loop (the METHOD-056 CPD E-step, the sparse CG
  workspace) records through `SpatialIndexCache::QueueGpuCompute`, which submits with the
  frame, waits `FramesInFlight` frames and reads back with a separate transfer submit: 4-6
  frames per round trip. C117 measured about 0.4 s per device E-step at 10^5 points and a
  CPD run at 10^3 points 3.4x slower than the CPU. Give such jobs a fenced off-frame compute
  submit whose readback completes as soon as the GPU finishes.

## Acceptance criteria
- [ ] An RHI path records a compute command buffer outside the frame, signals a fence and maps
      the result (or copies it) without waiting for frames in flight; the frame loop only polls.
- [ ] `QueueGpuCompute` callers opt in without changing their record callbacks.
- [ ] gpu;vulkan smoke: identical bytes to the framed path; round-trip latency reported.
- [ ] Rerun `METHOD056VulkanCpdEStep.ScalingProfile`: the 10^3-point Vulkan run no slower than
      its CPU twin, and the 10^5 result sealed again.

## Verification
```bash
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'METHOD056|GRAPHICS150' -L 'gpu|vulkan' --timeout 300
```
