# GRAPHICS-037D — Multi-queue Vulkan recording + opt-in gpu;vulkan smoke

## Status
- Commit reference: this task-landing commit.
- Landed 2026-06-04 at maturity `Operational` on Vulkan-capable hosts. The
  renderer/RHI submit-plan seam now records per-affinity command contexts;
  Vulkan submits per-affinity command buffers with timeline waits/signals and
  queue-family ownership-transfer barriers; the default recipe routes
  `PostProcessHistogramPass` to async compute and the opt-in `gpu;vulkan`
  smoke validates `AsyncComputeUtilizedFrames >= 1` with readback parity.
- Capability-absent hosts demote optional async/transfer work to the graphics
  queue and keep the default single-queue path covered by the CPU gate.

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
- Renderer/RHI submit-plan seam now exposes per-affinity command contexts and
  timeline wait/signal descriptors, allowing Vulkan to place inter-queue waits
  at real submit boundaries without importing Vulkan into renderer/framegraph.

## Slice plan
- **Slice A (this slice).** Add Vulkan async-compute queue-family discovery/acquisition
  diagnostics, expose backend-local queue-family token resolution, and make Sync2
  barrier recording translate `GRAPHICS-037C` queue-affinity tokens into concrete
  Vulkan queue-family indices. Closes the Vulkan-owned ownership-transfer recording
  prerequisite while preserving the single graphics submission path.
- **Slice B.** Add the backend-neutral queue-command/submit-plan seam needed by the
  renderer to record pass batches at queue submission boundaries without importing
  Vulkan.
- **Slice C.** Record per-affinity Vulkan command buffers and submit them on their
  corresponding `VkQueue` with timeline semaphore waits/signals synthesized from the
  compiled schedule.
- **Slice D.** Add the opt-in `gpu;vulkan` smoke proving `AsyncComputeUtilizedFrames >= 1`
  with correct output on Vulkan+GLFW hosts.

## Required changes
- [x] Select async-compute/transfer queue families during device bring-up (capability-gated).
- [x] Record Vulkan Sync2 queue-family ownership-transfer barriers by translating
      framegraph queue-affinity tokens to concrete backend queue-family indices.
- [x] Add backend-neutral queue-command/submit-plan seam and CPU/null submit-plan
      contract coverage.
- [x] Record per-affinity command buffers and submit on the corresponding `VkQueue`.
- [x] Record timeline-semaphore signal/wait and queue-family ownership-transfer barriers
      from the compiled schedule.
- [x] Add an opt-in `gpu;vulkan` smoke asserting an async-compute pass actually runs on
      the async queue (`AsyncComputeUtilizedFrames` ≥ 1) with correct output.

## Tests
- [x] `gpu;vulkan` smoke — multi-queue submission overlap on a Vulkan-capable host;
      skips on hosts without an operational Vulkan/GLFW lane.
- [x] CPU-only Vulkan contract coverage proves queue-token resolution and fail-closed async diagnostics.
- [x] CPU-only submit-plan contract coverage proves queue batching, timeline wait/signal
      placement, and demotion fallback.
- [x] Default CPU gate stays green (no regression to the single-queue path).

## Docs
- [x] Document multi-queue Vulkan recording + queue-family selection in
      `src/graphics/vulkan/README.md`.
- [x] Document backend-neutral RHI queue profile/context seam in
      `src/graphics/rhi/README.md`.
- [x] Document framegraph submit-plan semantics in
      `src/graphics/framegraph/README.md`.

## Acceptance criteria
- [x] Multi-queue submission records and runs on a Vulkan-capable host via the opt-in smoke.
- [x] Capability-absent hosts fall back to the single-queue path; CPU gate unchanged.
- [x] No new layering violations.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan -L 'gpu' -L 'vulkan' --output-on-failure
# CPU regression:
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Slice A verification run on 2026-06-04:

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanContractTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'Vulkan(FailClosedContract|OperationalStatusEvaluator|OperationalDiagnosticsSnapshot)' --timeout 60
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'VulkanBootstrapSmoke' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```

Slice B verification run on 2026-06-04:

```bash
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanContractTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'Vulkan(FailClosedContract|OperationalStatusEvaluator|OperationalDiagnosticsSnapshot)' --timeout 60
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'VulkanBootstrapSmoke' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# First CPU CTest attempt failed only because IntrinsicBenchmarkSmoke was not built by IntrinsicTests.
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```

Slice C focused verification run on 2026-06-04:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'Graphics(QueueAffinity|CrossQueueTimeline|OwnershipTransfer)' --timeout 60
build/ci/bin/IntrinsicGraphicsContractCpuTests --gtest_filter='GraphicsQueueAffinity.MockDeviceRecordsFrameQueueSubmitPlanAndBatchContexts'
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanContractTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'Vulkan(FailClosedContract|OperationalStatusEvaluator|OperationalDiagnosticsSnapshot)' --timeout 60
```

Slice C clean verification run on 2026-06-04:

```bash
rm -rf build/ci build/ci-vulkan
cmake --preset ci
cmake --preset ci-vulkan
cmake --build --preset ci --target IntrinsicTests
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanContractTests
ctest --test-dir build/ci --output-on-failure -R 'Graphics(QueueAffinity|CrossQueueTimeline|OwnershipTransfer)' --timeout 60
# First default CPU CTest attempt failed only because IntrinsicBenchmarkSmoke was not built by IntrinsicTests.
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci-vulkan --output-on-failure -R 'Vulkan(FailClosedContract|OperationalStatusEvaluator|OperationalDiagnosticsSnapshot)' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```

Slice D focused verification run on 2026-06-04:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R '(Graphics(QueueAffinity|CrossQueueTimeline|OwnershipTransfer)|GraphicsPostProcessChainContract\.FrameRecipeDeclaresPostProcessHistogramAsOrderedPass|RendererFrameLifecycle\.AsyncComputeQueuePlanIncrementsUtilizationStat)' --timeout 60
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke\.AsyncComputeHistogramQueueReadbackMatchesMinimalHarnessSamples' --timeout 120
```

Final verification run on 2026-06-04:

```bash
cmake --build --preset ci --target IntrinsicTests
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci-vulkan --output-on-failure -R 'Vulkan(FailClosedContract|OperationalStatusEvaluator|OperationalDiagnosticsSnapshot)' --timeout 60
ctest --test-dir build/ci-vulkan --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke\.(RecipeSelectorReachesOperationalVulkanCommandStream|ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples|AsyncComputeHistogramQueueReadbackMatchesMinimalHarnessSamples)' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```

## Forbidden changes
- Moving queue-family selection or `VkQueue` recording into `graphics/framegraph`.
- Changing the CPU/null contracts from `GRAPHICS-037A/B/C`.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `Operational` on Vulkan-capable hosts via the opt-in `gpu;vulkan` smoke;
  the single-queue path remains the unconditional default elsewhere.
