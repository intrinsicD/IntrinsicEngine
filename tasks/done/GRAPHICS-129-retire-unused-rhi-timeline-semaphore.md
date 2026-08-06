---
id: GRAPHICS-129
theme: F
depends_on:
  - GRAPHICS-037B
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-graphics129"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T15:35:42Z"
contract_schema: 1
contracts:
  - repo.task-contract-discovery
---
# GRAPHICS-129 — Retire unused RHI timeline-semaphore abstraction

## Status

- Completed and retired on 2026-08-06. The zero-production-consumer
  `Extrinsic.RHI.TimelineSemaphore` module and self-referential test-double case
  are deleted. Compiled cross-queue signal/wait/edge records,
  `FrameQueueSubmitPlanDesc`, and backend-local Vulkan timelines remain the
  operational contract. Focused coverage passed 52/52 and the full default CPU
  selector passed 4,103/4,103 with its expected GLFW/LSan skip. Fresh module
  configure/build, strict retired-symbol, inventory, clean-workshop, layering,
  task, docs-sync/link, and whitespace gates pass.
- Completion commit: this implementation/retirement commit.

## Goal

- Delete the production-unused `Extrinsic.RHI.TimelineSemaphore` module instead
  of retaining a test-only interface as a promoted RHI contract.

## Non-goals

- No change to compiled render-graph cross-queue wait/signal records, Vulkan
  timeline submission, queue scheduling, or synchronization behavior.
- No PipelineRegistry work; `GRAPHICS-130` owns that independent finding.
- No replacement interface, factory, compatibility alias, or wrapper.

## Context

- At the rejected `REVIEW-003` baseline, `ITimelineSemaphore` has zero
  production implementations and zero production consumers. Its sole
  implementation is a test double that signals and waits on itself.
- Real frame-graph/Vulkan synchronization uses compiled submit-plan records and
  backend-local timeline semaphores, so the interface is not the operational
  boundary described by its archived origin task.

## Right-sizing decision

- **Element:** `ITimelineSemaphore` and its promoted RHI module.
- **Deletion test:** removing the module and its self-referential direct test
  redistributes no production behavior.
- **Simpler alternative:** retain the existing compiled wait/signal data and
  Vulkan backend implementation without an unused object interface.
- **Reintroduction trigger:** a present production caller needs an object-level
  timeline seam and a second implementation or deterministic test-double
  boundary at that exact call site.

## Required changes

- [x] Delete the module interface and its CMake/module-inventory entry.
- [x] Delete the direct test whose only purpose is instantiating the unused
      interface; preserve owner-level cross-queue and submit-plan coverage.
- [x] Remove current-state docs/parity claims that name the deleted module as
      an operational RHI capability.
- [x] Add a structural ratchet proving source/tests do not recreate the module
      name as a compatibility surface.

## Tests

- [x] Focused frame-graph cross-queue and queue-submit contracts pass without
      importing `RHI.TimelineSemaphore`.
- [x] Complete default CPU-supported gate passes.
- [x] Strict layering, docs/task, clean-workshop, and inventory gates pass.

## Docs

- [x] Update RHI and parity inventories to describe the surviving submit-plan
      synchronization contract.
- [x] Regenerate module/task/session records.

## Acceptance criteria

- [x] No module, CMake entry, direct test, or current-state claim for
      `RHI.TimelineSemaphore`/`ITimelineSemaphore` remains.
- [x] Real cross-queue behavior stays covered and unchanged.
- [x] No replacement abstraction appears.
- [x] Independent fixed-surface review accepts the revision-bound deletion.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CrossQueue|QueueSubmit|FrameGraph' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
git diff --check
```

## Forbidden changes

- Altering Vulkan synchronization or queue scheduling.
- Moving the unused API behind another module or compatibility name.
- Deleting owner-level behavior tests with the direct abstraction test.

## Maturity

- Target: `Retired`; the unused promoted module disappears after surviving
  owner behavior is proven.
