---
id: GRAPHICS-086
theme: F
depends_on: []
---
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
- Inventory ground truth (2026-06-10): legacy modules with **no** promoted equivalent are `RHI.SceneInstances` (scene-instance convenience APIs), `RHI.CudaDevice`, and `RHI.CudaError`. No promoted graphics or runtime module imports CUDA today; `INTRINSIC_ENABLE_CUDA` (default `OFF`) only gates the legacy CUDA modules. The audit step below confirms and completes this list rather than starting from scratch.
- `RUNTIME-103` (geometry algorithm execution queue) defers its optional CUDA K-Means backend decision to this task; the CUDA decision here must name the consumer story it accepts or retires.

## Value gate
- Current state: promoted RHI/Vulkan already expose command contexts, descriptors, bindless resources, transfers, pipelines, queue affinity, timeline semaphores, and backend-local Vulkan modules.
- Improvement: deletion blockers become tests, narrow fixes, or retirement decisions while preserving platform-free RHI and optional CUDA.
- Scope decision: audit first. Do not implement legacy convenience APIs or CUDA seams unless a current promoted consumer or benchmark/method task requires them.

## Required changes
- [x] Inventory legacy `src/legacy/RHI/**` modules against promoted RHI/Vulkan modules and tests.
- [x] Decide for each gap whether promoted coverage already exists, a narrow implementation task is needed, or the behavior is retired.
- [x] Add missing CPU/null contract tests for command helper parity, persistent descriptor semantics, swapchain/image state ownership, and scene-instance convenience replacement if needed.
- [x] Decide CUDA with rationale recorded in the parity matrix, choosing exactly one:
      (a) **remove** — record that no promoted compute seam is owed (deletion itself stays with `LEGACY-009`) and update `RUNTIME-103`'s deferral note;
      (b) **promote** — port a minimal `INTRINSIC_ENABLE_CUDA`-gated device/error seam into `src/graphics/rhi/` with opt-in skip-safe tests, and name the follow-up task that owns `Operational` proof;
      (c) **defer** — open a compute-backend follow-up task gated on `RUNTIME-103` identifying a real workload, and record its ID in the parity matrix.
- [x] Update `LEGACY-009` prerequisites with any remaining concrete blockers.

## Tests
- [x] Add or update `contract;graphics` RHI tests for any retained parity behavior.
- [x] Keep CUDA tests opt-in and skip-safe under `INTRINSIC_ENABLE_CUDA=ON` when no driver is present.
- [x] Run strict layering checks to prove RHI remains platform-free.

## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` RHI and legacy RHI rows.
- [x] Update `src/graphics/rhi/README.md` and `src/graphics/vulkan/README.md` if contracts change.
- [x] Update `tasks/backlog/rendering/README.md` and `docs/migration/legacy-retirement.md` with final blockers.

## Acceptance criteria
- [x] Every legacy RHI module has a promoted replacement, explicit retirement decision, or named follow-up task.
- [x] CUDA has a concrete keep/remove/defer decision with verification expectations.
- [x] `LEGACY-009` is blocked only by consumer-grep results or named implementation tasks.

## Status
- Completed 2026-06-11 at maturity `CPUContracted`.
- PR/commit: this retirement commit.
- Decision: CUDA is **removed** from the promoted default path. No current
  runtime, graphics, method, or benchmark consumer requires a CUDA compute seam;
  future CUDA work must open a new opt-in method/backend task with a concrete
  workload and verification plan.
- Inventory result: promoted RHI/Vulkan/renderer seams cover legacy command
  helpers, persistent descriptors, swapchain/image ownership, and
  scene-instance convenience for current scope. `LEGACY-009` remains blocked by
  consumer-grep/subtree ordering, not by unnamed RHI parity gaps.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'graphics|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Verification results
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'graphics|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
tools/ci/run_clean_workshop_review.sh . --strict
```

Result: passed on 2026-06-11. The CTest selection ran 978 tests with 0
failures. Clean workshop automated rows passed; manual rows 3-6 remain a
human-review activity.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making CUDA mandatory for default builds or tests.
- Reintroducing platform imports or links into `graphics/rhi`.

## Maturity
- Target: `CPUContracted` for RHI parity decisions/tests; optional CUDA `Operational` proof only if CUDA is retained.
