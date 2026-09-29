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

## Completion — 2026-09-29
Commit: `36bcf55db` on `claude/cpd-nystrom`; evidence in
`ara/evidence/diagnostics/graphics150_low_latency_compute_20260929/` (C117 revised).
`IDevice::SubmitComputeReadback` (fail-closed default; Vulkan: transfer-pool command buffer on
the graphics queue, shared validated download path, pool access under the mutex) and
`SpatialGpuLatency::Immediate` for `QueueGpuCompute` (index-backed only once built; refusals
stay framed). The CPD E-step broker, point sampling and sparse CG opt in. Release, RTX 3050:
round trip 3.4 ms in the submitting frame instead of 8.9 ms / 3 frames, identical bytes; CPD
Vulkan route 34 ms at 10^3 (78 ms framed, CPU twin 28 ms), 349 ms at 10^4 (3.8x), 20.1 s at
10^5 (9.3x). The GPU suite (METHOD-056, RUNTIME-269/290, GEOM-081 parity) passes on the
immediate path. Small inputs still lose to engine-frame pump pacing: GRAPHICS-152. Operational.

## Goal
- Iterative GPU work driven from a CPU loop (the METHOD-056 CPD E-step, the sparse CG
  workspace) records through `SpatialIndexCache::QueueGpuCompute`, which submits with the
  frame, waits `FramesInFlight` frames and reads back with a separate transfer submit: 4-6
  frames per round trip. C117 measured about 0.4 s per device E-step at 10^5 points and a
  CPD run at 10^3 points 3.4x slower than the CPU. Give such jobs a fenced off-frame compute
  submit whose readback completes as soon as the GPU finishes.

## Acceptance criteria
- [x] An RHI path records a compute command buffer outside the frame, signals a fence and maps
      the result (or copies it) without waiting for frames in flight; the frame loop only polls.
      (The transfer queue's timeline semaphore stands in for the fence.)
- [x] `QueueGpuCompute` callers opt in without changing their record callbacks.
- [x] gpu;vulkan smoke: identical bytes to the framed path; round-trip latency reported.
- [x] Rerun `METHOD056VulkanCpdEStep.ScalingProfile` and seal the 10^5 result again. The
      original sub-goal "10^3 no slower than the CPU twin" is not met (34.2 ms vs 28.0 ms, from
      78.4 ms framed): the remaining cost is the pump's once-per-frame cadence, not the round
      trip, and moved to GRAPHICS-152.

## Verification
```bash
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'METHOD056|GRAPHICS150' -L 'gpu|vulkan' --timeout 300
```
