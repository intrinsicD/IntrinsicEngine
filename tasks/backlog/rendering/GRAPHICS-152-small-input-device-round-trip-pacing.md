---
id: GRAPHICS-152
theme: I
depends_on: [GRAPHICS-150]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from the GRAPHICS-150 measurements (2026-09-29); implementation owes a resealed scaling run.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-152 — Device round trips that do not lose small inputs to frame pacing

## Goal
- After GRAPHICS-150 a device E-step round trip costs 3.4 ms and is delivered in the frame
  that submitted it, but the CPD broker's pump runs once per engine frame, so each device
  iteration waits for the next job drain. At 10^3 points (a 1 ms CPU E-step) the Vulkan route
  takes 34 ms against 28 ms for its CPU twin (C117); with VSync on a frame is 16 ms, so small
  inputs lose more in the sandbox. ICP's `QueueGpuNearest` batches are still framed.
- Options to weigh: route E-steps below a pair count to the CPU inside the Vulkan policy (a
  documented cost model, reported like narrow kernels, not as fallbacks); or let the pump
  submit and deliver more than once per frame (worker-side waits on the transfer timeline,
  queue ownership); move ICP nearest batches to the immediate path.

## Acceptance criteria
- [ ] `METHOD056VulkanCpdEStep.ScalingProfile`: the 10^3-point Vulkan run no slower than its
      CPU twin, 10^4 and 10^5 no slower than the GRAPHICS-150 seal; evidence resealed.
- [ ] The chosen rule is documented where the E-step policies are, with its measured crossover.

## Verification
```bash
INTRINSIC_METHOD056_SCALING_OUTPUT=<dir> DISPLAY=:7 build/ci-vulkan-release/bin/IntrinsicPointLBVHGpuTests --gtest_filter=METHOD056VulkanCpdEStep.ScalingProfile
```
