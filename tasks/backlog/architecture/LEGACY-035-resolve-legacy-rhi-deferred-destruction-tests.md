---
id: LEGACY-035
theme: F
depends_on: [LEGACY-034]
---
# LEGACY-035 — Resolve legacy RHI deferred-destruction tests

## Goal
- [ ] Migrate or explicitly retire the Vulkan deferred-destruction behavior
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
- `LEGACY-034` retires the runtime-owned CPU/null portion because promoted
  `Extrinsic.Core.FrameLoop` and `Extrinsic.Runtime.Engine` contracts already
  own the retained maintenance and frame-loop ordering. The backend-facing
  deferred-destruction behavior is not a runtime contract and should be
  resolved under RHI/Vulkan ownership.
- Existing legacy RHI coverage includes `Test_RuntimeRHI.cpp` concurrency checks
  for `SafeDestroy`, but it does not fully replace the old
  `MaintenanceLaneGpuTest` insertion-order, real-buffer deferred destroy, and
  multi-frame retirement assertions.

## Required changes
- [ ] Inventory existing `RHI::VulkanDevice` deferred-destruction tests and
      classify the old `MaintenanceLaneGpuTest` cases as duplicate,
      retained opt-in Vulkan coverage, or retired legacy-only behavior.
- [ ] If retained, move the behavior to an RHI/graphics-owned test with a
      `Test.<Name>.cpp` filename and labels that keep it opt-in
      (`gpu;vulkan;slow`).
- [ ] If retired, document the retirement rationale in the parity matrix and
      the relevant deletion tasks before removing the blocker.
- [ ] Update `LEGACY-009` blocker notes with the resulting consumer-grep state.

## Tests
- [ ] Run focused CTest discovery/filtering for any migrated deferred-destruction
      tests.
- [ ] Run opt-in Vulkan coverage only on a Vulkan-capable host.
- [ ] Run `python3 tools/repo/check_test_layout.py --root . --strict`.

## Docs
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` for the retained or
      retired deferred-destruction decision.
- [ ] Update `docs/migration/legacy-removal-audit.md` and `LEGACY-009` with the
      current legacy RHI consumer count.
- [ ] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [ ] No runtime-owned test file is the only owner of Vulkan deferred-destruction
      behavior.
- [ ] Retained coverage is owned by an RHI/graphics test or the retirement
      decision is documented.
- [ ] Legacy RHI deletion-task blocker notes are current and reproducible by
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
- This task closes when deferred-destruction behavior is either covered by an
  appropriately labeled RHI/graphics test or documented as retired.
