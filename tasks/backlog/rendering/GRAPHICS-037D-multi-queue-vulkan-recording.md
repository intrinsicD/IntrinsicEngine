# GRAPHICS-037D — Multi-queue Vulkan recording + opt-in gpu;vulkan smoke

## Goal
- Implement the Vulkan recording bodies for multi-queue submission (queue-family
  selection, real `VkQueue` submit per affinity, timeline-semaphore signal/wait, and
  ownership-transfer barrier recording) and add an opt-in `gpu;vulkan` smoke validating
  async-compute overlap (`GRAPHICS-037` decisions 11/12), gated by `GRAPHICS-033`.

## Non-goals
- No async-compute pass content changes (shadow/BVH/post bodies are owned elsewhere).
- No change to the CPU/null partition/edge/barrier contracts from `GRAPHICS-037A/B/C`.

## Context
- Owner layer: `graphics/vulkan` (queue-family selection + concrete `VkQueue` /
  ownership-transfer recording; decision 12 keeps this out of `graphics/framegraph`).
- Depends on `GRAPHICS-037A`, `GRAPHICS-037B`, `GRAPHICS-037C`, and `GRAPHICS-033`
  (operational gate, queue-family rules — done).
- Decision 11: opt-in `gpu;vulkan` smoke for real multi-queue submission once Vulkan is
  operational. The smoke runs only on Vulkan + GLFW hosts and is excluded from the
  default CPU gate.

## Required changes
- [ ] Select async-compute/transfer queue families during device bring-up (capability-gated).
- [ ] Record per-affinity command buffers and submit on the corresponding `VkQueue`.
- [ ] Record timeline-semaphore signal/wait and queue-family ownership-transfer barriers
      from the compiled schedule.
- [ ] Add an opt-in `gpu;vulkan` smoke asserting an async-compute pass actually runs on
      the async queue (`AsyncComputeUtilizedFrames` ≥ 1) with correct output.

## Tests
- [ ] `gpu;vulkan` smoke — multi-queue submission overlap on a Vulkan-capable host;
      skips on hosts without an operational Vulkan/GLFW lane.
- [ ] Default CPU gate stays green (no regression to the single-queue path).

## Docs
- [ ] Document multi-queue Vulkan recording + queue-family selection in
      `src/graphics/vulkan/README.md`.

## Acceptance criteria
- [ ] Multi-queue submission records and runs on a Vulkan-capable host via the opt-in smoke.
- [ ] Capability-absent hosts fall back to the single-queue path; CPU gate unchanged.
- [ ] No new layering violations.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan -L 'gpu' -L 'vulkan' --output-on-failure
# CPU regression:
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Moving queue-family selection or `VkQueue` recording into `graphics/framegraph`.
- Changing the CPU/null contracts from `GRAPHICS-037A/B/C`.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `Operational` on Vulkan-capable hosts via the opt-in `gpu;vulkan` smoke;
  the single-queue path remains the unconditional default elsewhere.
