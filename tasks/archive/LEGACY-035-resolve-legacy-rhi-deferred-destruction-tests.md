---
id: LEGACY-035
theme: F
depends_on: [LEGACY-034]
---
# LEGACY-035 — Resolve legacy RHI deferred-destruction tests

## Goal
- [x] Migrate or explicitly retire the Vulkan deferred-destruction behavior
      formerly covered by `MaintenanceLaneGpuTest` after `LEGACY-034` removes
      the runtime-owned legacy maintenance-lane test file.

## Non-goals
- Do not delete `src/legacy/RHI/` in this task.
- Do not make Vulkan/GPU tests part of the default CPU-supported gate.
- Do not reintroduce `Runtime.FrameLoop` or `Runtime.ResourceMaintenance`
      imports to preserve the old fixture.

## Context
- Owner/layer: legacy RHI/Vulkan test cleanup under the `LEGACY-012` consumer
  migration program.
- `tests/unit/runtime/Test_MaintenanceLane.cpp` mixed two concerns: CPU
  maintenance-lane ordering over legacy runtime hooks and opt-in Vulkan
  deferred-destruction checks for `RHI::VulkanDevice::SafeDestroy`,
  `SafeDestroyAfter`, `CollectGarbage`, and
  `FlushTimelineDeletionQueueNow`.
- `LEGACY-034` retired the runtime-owned CPU/null portion because promoted
  `Extrinsic.Core.FrameLoop` and `Extrinsic.Runtime.Engine` contracts already
  own the retained maintenance and frame-loop ordering.
- Existing legacy RHI coverage still includes `Test_RuntimeRHI.cpp`
  `TransferTest.TimelineValue_ConcurrentSafeDestroy`, but the old
  `MaintenanceLaneGpuTest` insertion-order, real-buffer deferred destroy, and
  multi-frame retirement assertions are implementation-specific legacy RHI
  behavior.
- Promoted Vulkan owns a private backend-local `DeferDelete(...)` /
  per-frame `DeletionQueue` implementation. It intentionally does not expose
  legacy `SafeDestroyAfter`, timeline-value deletion queue, or unconditional
  flush APIs through promoted `RHI::IDevice`.

## Required changes
- [x] Inventory existing `RHI::VulkanDevice` deferred-destruction tests and
      classify the old `MaintenanceLaneGpuTest` cases as duplicate,
      retained opt-in Vulkan coverage, or retired legacy-only behavior.
- [x] If retained, move the behavior to an RHI/graphics-owned test with a
      `Test.<Name>.cpp` filename and labels that keep it opt-in
      (`gpu;vulkan;slow`).
- [x] If retired, document the retirement rationale in the parity matrix and
      the relevant deletion tasks before removing the blocker.
- [x] Update `LEGACY-009` blocker notes with the resulting consumer-grep state.

## Classification

| Legacy deferred-destruction case | Decision |
|---|---|
| `SafeDestroyDefersUntilTimelineCompletion` | Retired as legacy RHI timeline-queue behavior. Promoted Vulkan does not expose a timeline-value `SafeDestroy` contract. |
| `SafeDestroyAfterFlushExecutesInInsertionOrder` | Retired as an assertion on legacy unconditional flush ordering. Promoted Vulkan keeps deletion order backend-private. |
| `BufferDeferredDestroyDoesNotLeak` | Retired as legacy `RHI::VulkanBuffer` lifetime coverage. Promoted public lifetime checks should target `RHI::IDevice::Destroy*` or renderer/GPU-asset ownership, not legacy buffer classes. |
| `MultiFrameRetirementCycle` | Retired as legacy maintenance-lane simulation. Promoted runtime maintenance ordering is covered by `Extrinsic.Core.FrameLoop`; promoted Vulkan frame-slot deletion remains backend-private. |
| `TransferTest.TimelineValue_ConcurrentSafeDestroy` | Left in existing legacy `Test_RuntimeRHI.cpp` until `src/legacy/RHI` deletion; it is not promoted or expanded by this task. |

## Tests
- [x] Run focused CTest discovery/filtering for any migrated deferred-destruction
      tests.
- [x] Run opt-in Vulkan coverage only on a Vulkan-capable host.
- [x] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` for the retained or
      retired deferred-destruction decision.
- [x] Update `docs/migration/legacy-removal-audit.md` and `LEGACY-009` with the
      current legacy RHI consumer count.
- [x] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] No runtime-owned test file is the only owner of Vulkan deferred-destruction
      behavior.
- [x] Retained coverage is owned by an RHI/graphics test or the retirement
      decision is documented.
- [x] Legacy RHI deletion-task blocker notes are current and reproducible by
      grep.

## Verification
```bash
git grep -nE 'SafeDestroy|SafeDestroyAfter|FlushTimelineDeletionQueueNow|CollectGarbage' -- tests src/legacy/RHI
ctest --test-dir build/ci -N -R 'SafeDestroy|Deferred|RHI|Vulkan'
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

## Forbidden changes
- Moving legacy RHI behavior under runtime ownership.
- Adding default CPU-gate dependence on Vulkan availability.
- Mixing this coverage decision with mechanical deletion of `src/legacy/RHI/`.

## Maturity
- Target: `CPUContracted` planning/coverage cleanup, with `Operational`
  verification only when a Vulkan-capable host is available.
- Closed at `CPUContracted`: the old deferred-destruction cases are documented
  as legacy-only behavior, while promoted Vulkan deletion remains an internal
  backend detail until a future value-gated task asks for a public contract.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `LEGACY-009` still records 17 remaining RHI test consumers; this task changes
  the deferred-destruction blocker classification, not the count.

## Verification results
- `git grep -nE 'SafeDestroy|SafeDestroyAfter|FlushTimelineDeletionQueueNow|CollectGarbage' -- tests src/legacy/RHI`
  — reports existing legacy RHI implementation uses plus the remaining
  `TransferTest.TimelineValue_ConcurrentSafeDestroy` consumer.
- `ctest --test-dir build/ci -N -R 'SafeDestroy|Deferred|RHI|Vulkan'`
  — discovered 156 existing matching tests/entries, including the remaining
  legacy `TransferTest.TimelineValue_ConcurrentSafeDestroy`; no migrated
  deferred-destruction tests were added.
- Structural checks (`check_test_layout.py`, `validate_tasks.py`,
  `check_task_policy.py`, `check_task_state_links.py`, `check_doc_links.py`,
  `generate_session_brief.py --check`, `check_docs_sync.py`, and
  `git diff --check`) — passed.
