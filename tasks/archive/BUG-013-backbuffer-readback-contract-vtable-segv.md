# BUG-013 — Default-recipe + minimal-debug backbuffer readback contract tests SEGV under clang-20 modules

## Status

- Closed 2026-05-29 as **not reproducible on a clean `ci` preset build**.
- Maturity: `CPUContracted`. The two contract cases pass on the default
  CPU/null gate; this bug has no GPU/Vulkan dimension (it lives entirely on
  the `MockCommandContext` / `NullRenderer` CPU path).
- Owner: local agent. Branch: `claude/intrinsicengine-agent-onboarding-M8XiO`.
- Commit reference: this retirement commit on the branch above (docs +
  task-record only; no engine source or test source modified).

## Goal
- Restore `MinimalDebugBackbufferReadbackContract.ConfiguredHandleRecordsReadbackTripletOnce` and `DefaultRecipeBackbufferReadbackContract.ConfiguredHandleRecordsReadbackTripletOnce` to the green default CPU gate.
- **Outcome:** both cases (and the full 225-test `IntrinsicGraphicsContractCpuTests` binary) already pass on a clean `cmake --preset ci` build. The reported SEGV did not reproduce in a freshly-cloned tree; see `## Resolution`.

## Non-goals
- Do not change the renderer-visible API surface of `IRenderer::SetMinimalDebugBackbufferReadbackBuffer(...)` / `IRenderer::SetDefaultRecipeBackbufferReadbackBuffer(...)` or the counter shapes — both are correct as authored.
- Do not retire the `MinimalDebug` scaffold (`GRAPHICS-081`) as part of this bug.
- Do not widen the HARDEN-072 fallback by re-pure-virtualizing `ICommandContext::CopyTextureToBuffer`.
- Do not weaken AddressSanitizer / default CPU gate strictness.
- **Deferred (optional, not owed by this bug):** belt-and-suspenders structural
  hardening of `RHI::ICommandContext` — giving the class a single out-of-line
  key function (out-of-line destructor in an `RHI.CommandContext.cpp` module
  implementation unit, plus moving the non-trivial `SubmitBarriers` body out of
  the `.cppm` per AGENTS.md §5) so the vtable is anchored to one TU. This was
  considered and **deliberately not done**: there is no defect to fix on a clean
  build, the area is known-fragile (the strategy-1 attempt below surfaced a
  secondary slot mismatch on an already-incremental tree), and clean-rebuild
  discipline (now documented) is the authoritative prevention. If a future
  reviewer wants the structural anchor regardless, it is captured as its own
  scoped task `HARDEN-073`
  (`tasks/archive/HARDEN-073-rhi-command-context-vtable-key-function.md`),
  which records the honest assessment that it does **not** prevent the stale-BMI
  recurrence and exists only for AGENTS.md §5 hygiene and compiler-bug-variant
  defense.

## Context
- Original symptom (as filed 2026-05-28): `IntrinsicGraphicsContractCpuTests --gtest_filter='*BackbufferReadbackContract.ConfiguredHandleRecordsReadbackTripletOnce'` SEGV (PC = 0x0) inside `NullRenderer::ExecuteFrame` at the per-recipe `graphicsContext.CopyTextureToBuffer(...)` call against a `MockCommandContext`.
- Root-cause hypothesis at filing time: commit `cc06edef` added a new non-pure virtual `BindFrameSampledTexture(TextureHandle)` with an inline body to `RHI::ICommandContext`, between `BindPipeline` and `BindIndexBuffer`. The theory was a clang-20 / C++23-modules vtable-layout disagreement (same shape as HARDEN-072, different trigger) leaving `MockCommandContext` with a divergent vtable so the renderer's `CopyTextureToBuffer` call dispatched a null slot.
- Naive workaround attempts recorded at filing time that did **not** resolve the SEGV (preserved so the next agent does not repeat them):
  1. Moving the inline bodies of `BindFrameSampledTexture` / `CopyTextureToBuffer` out of `RHI.CommandContext.cppm` into a new `RHI.CommandContext.cpp`. Symbols emitted/linked, but the subclass vtables still appeared to omit the slot.
  2. Adding an explicit `BindFrameSampledTexture(...) override {}` to every subclass. Built (proving the base virtual is visible to the test TUs), but surfaced a *different* slot mismatch on `PushConstants`. This second symptom is itself the tell that the failure was a cross-TU layout disagreement, not a single missing slot — i.e. a stale-BMI artifact rather than a source defect.

## Resolution
- This bug was investigated in a freshly-cloned container (only `build/ci`
  exists; no stale tree was inherited). On a clean `cmake --preset ci` +
  `cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests`:
  - The two `ConfiguredHandleRecordsReadbackTripletOnce` cases pass (verified
    8/8 for the `*BackbufferReadbackContract*` filter, 5× repeat for the two
    cases, and 225/225 for the whole contract binary).
  - They run **through the default-gate CTest invocation** (`ctest -R ... -LE
    'gpu|vulkan|slow|flaky-quarantine'`), and are individually registered
    (tests #25 and #87) with labels `contract graphics`.
- The exact reported crash site executes correctly: `MockCommandContext` does
  **not** override `CopyTextureToBuffer`, so the renderer dispatches the base
  inline no-op body through an `ICommandContext&` and increments its own
  `*BackbufferReadbackCopyCount` counter to 1 — no null-slot dereference.
- The single `vtable for Extrinsic::RHI::ICommandContext@Extrinsic.RHI.CommandContext`
  symbol is module-owned and emitted once; there is no cross-TU layout
  divergence in a clean build.
- **Conclusion:** the SEGV was a stale module-BMI / incremental-build artifact
  on the reporter's tree after the `cc06edef` interface change — a known
  clang-20 C++23-modules hazard. AGENTS.md §7 already designates clean preset
  builds as the authoritative verification and warns that stale non-preset/
  incremental module trees are not valid evidence. The committed source is
  correct; the prevention is clean-rebuild discipline after any
  `RHI::ICommandContext` interface change, now documented in
  `src/graphics/rhi/README.md`.
- **Test-seam note (per the diagnosis discipline):** a cross-TU BMI-staleness
  divergence has no correct in-source test seam — within any single clean build
  all TUs agree, so a single-TU "vtable slot index" assertion would give false
  confidence. The existing behavioral readback contract tests already exercise
  the real cross-TU dispatch path (renderer TU → base `ICommandContext&` →
  `MockCommandContext` TU) and now run under the `contract` label in the default
  gate, which is the correct fail-closed guard for a genuine layout-shifting
  interface change. No shallow guard test was added.

## Required changes
- [x] Reproduce attempt on a fresh `cmake --preset ci` build — could not reproduce; captured the green evidence (8/8 filter, 225/225 binary, default-gate CTest) instead of a backtrace, because no crash occurs.
- [x] Determine the authoritative state: clean-build gate is green and the source is correct; root cause attributed to stale incremental module BMIs.
- [x] Document the clang-20 / C++23-modules clean-rebuild requirement for `RHI::ICommandContext` changes in `src/graphics/rhi/README.md`.
- [x] Cross-link this finding from `GRAPHICS-076E` (now unblocked) and from the HARDEN-072 done-task note as a follow-on manifestation of the same upstream clang-20 issue. (`GRAPHICS-081` is blocked on operational Vulkan, not on this bug, so no change there.)
- [x] No engine source or test source change is required (and none was made), because there is no defect on a clean build.

## Tests
- [x] `DefaultRecipeBackbufferReadbackContract.ConfiguredHandleRecordsReadbackTripletOnce` passes on the default CPU gate (CTest #25, label `contract`).
- [x] `MinimalDebugBackbufferReadbackContract.ConfiguredHandleRecordsReadbackTripletOnce` passes on the default CPU gate (CTest #87, label `contract`).
- [x] The contract cases are CTest-registered and visible to `ctest -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine'`, so a real future layout-shifting regression fails closed at the top-level gate (the original "masked from `ctest -N`" concern is already resolved by gtest discovery).
- [x] No shallow single-TU vtable-slot test added — documented above as a false-confidence seam for a cross-TU BMI bug.

## Docs
- [x] `src/graphics/rhi/README.md` records the module-ABI hygiene rule: after adding/removing/reordering any virtual on the exported polymorphic `RHI::ICommandContext`, a clean preset rebuild is mandatory because incremental trees can retain stale module BMIs and SEGV via divergent vtable slots.
- [x] `tasks/archive/HARDEN-072-rhi-surface-fixes-for-default-recipe-pipeline-bringup.md` extended with a back-reference to this finding.

## Acceptance criteria
- [x] `cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests` builds cleanly.
- [x] `build/ci/bin/IntrinsicGraphicsContractCpuTests --gtest_filter='*BackbufferReadbackContract*'` reports 8/8 passing (4 `MinimalDebug` + 4 `DefaultRecipe`).
- [x] `ctest --test-dir build/ci --output-on-failure -R 'ConfiguredHandleRecordsReadbackTripletOnce' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` is green and the entries are present under the `contract` label.
- [x] No new layering violations (no source change; `python3 tools/repo/check_layering.py --root src --strict` clean).

## Verification
```bash
set -o pipefail
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests 2>&1 | tee /tmp/bug013-build.log | tail -n 20
# 8/8 pass:
build/ci/bin/IntrinsicGraphicsContractCpuTests --gtest_filter='*BackbufferReadbackContract*' 2>&1 | tail -n 8
# whole contract binary green (225/225):
build/ci/bin/IntrinsicGraphicsContractCpuTests 2>&1 | tail -n 4
# through the default-gate CTest invocation (both #25 and #87 Passed):
ctest --test-dir build/ci --output-on-failure -R 'ConfiguredHandleRecordsReadbackTripletOnce' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
Result captured 2026-05-29: 8/8 for the filter, 225/225 for the binary, 2/2 Passed through CTest #25/#87. No SEGV.

## Forbidden changes
- Disabling AddressSanitizer, quarantining the tests, or labelling them `flaky-quarantine`.
- Adding a `gpu`/`vulkan` label to the CPU contract tests to push them out of the default gate.
- Reverting GRAPHICS-076E's renderer-visible API surface instead of relying on the clean-build verification.
