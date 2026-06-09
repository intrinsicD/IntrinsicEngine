# GRAPHICS-086 — RHI retirement parity and CUDA decision

## Goal
- Close the remaining legacy RHI/runtime backend blockers by auditing command helper, persistent descriptor, swapchain/image ownership, scene-instance convenience APIs, and CUDA keep/remove decisions against promoted `graphics/rhi` and `graphics/vulkan` surfaces before accepting any implementation.

## Non-goals
- No default CUDA enablement.
- No renderer feature implementation beyond RHI/backend parity seams.
- No platform import into `graphics/rhi`; runtime remains responsible for platform-to-RHI device descriptors.
- No deletion of `src/legacy/RHI/`; deletion remains `LEGACY-009`.

## Context
- Owner/layer: `graphics/rhi -> core`, `graphics/vulkan -> core + graphics/rhi + backend-local Vulkan dependencies`.
- The parity matrix lists backend shader compilation, explicit swapchain/image ownership, command helper parity, persistent descriptor policy, CUDA device/error behavior, and scene-instance convenience APIs as unproven or open decisions.
- Existing promoted pieces include `RHI.CommandContext`, `RHI.Descriptors`, `RHI.Bindless`, `RHI.TextureManager`, `RHI.TransferQueue`, `RHI.PipelineManager`, `RHI.PipelineRegistry`, `RHI.QueueAffinity`, `RHI.TimelineSemaphore`, `Backends.Vulkan.*`, and `GRAPHICS-037D` multi-queue recording.

## Value gate
- Current state: promoted RHI/Vulkan already expose command contexts, descriptors, bindless resources, transfers, pipelines, queue affinity, timeline semaphores, and backend-local Vulkan modules.
- Improvement: deletion blockers become tests, narrow fixes, or retirement decisions while preserving platform-free RHI and optional CUDA.
- Scope decision: audit first. Do not implement legacy convenience APIs or CUDA seams unless a current promoted consumer or benchmark/method task requires them.

## Required changes
- [ ] Inventory legacy `src/legacy/RHI/**` modules against promoted RHI/Vulkan modules and tests.
- [ ] Decide for each gap whether promoted coverage already exists, a narrow implementation task is needed, or the behavior is retired.
- [ ] Add missing CPU/null contract tests for command helper parity, persistent descriptor semantics, swapchain/image state ownership, and scene-instance convenience replacement if needed.
- [ ] Decide CUDA: remove as legacy-only, promote an optional `INTRINSIC_ENABLE_CUDA` backend seam, or defer to a compute-specific follow-up with task ID.
- [ ] Update `LEGACY-009` prerequisites with any remaining concrete blockers.

## Tests
- [ ] Add or update `contract;graphics` RHI tests for any retained parity behavior.
- [ ] Keep CUDA tests opt-in and skip-safe under `INTRINSIC_ENABLE_CUDA=ON` when no driver is present.
- [ ] Run strict layering checks to prove RHI remains platform-free.

## Docs
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` RHI and legacy RHI rows.
- [ ] Update `src/graphics/rhi/README.md` and `src/graphics/vulkan/README.md` if contracts change.
- [ ] Update `tasks/backlog/rendering/README.md` and `docs/migration/legacy-retirement.md` with final blockers.

## Acceptance criteria
- [ ] Every legacy RHI module has a promoted replacement, explicit retirement decision, or named follow-up task.
- [ ] CUDA has a concrete keep/remove/defer decision with verification expectations.
- [ ] `LEGACY-009` is blocked only by consumer-grep results or named implementation tasks.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'graphics|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making CUDA mandatory for default builds or tests.
- Reintroducing platform imports or links into `graphics/rhi`.

## Maturity
- Target: `CPUContracted` for RHI parity decisions/tests; optional CUDA `Operational` proof only if CUDA is retained.
