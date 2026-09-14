---
id: RUNTIME-241
theme: J
depends_on: [RUNTIME-240]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive refactor with a captured dirty baseline, fixed Claude review and correctness/module-metadata checks; no timing claim.
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, runtime.texture-bake-interface-locality]
---
# RUNTIME-241 — Keep texture-bake composition inside its implementation

## Goal
Continue the operator-authorized cleanup with Claude while preserving the
verified, uncommitted RUNTIME-240 source. Remove private composition methods
from the texture-bake public module interface and collapse their redundant
forwarders. Preserve public API records, defaults, behavior and feature coverage.

## Reuse and plan
`TextureBakeService::Impl` in the existing `Runtime.TextureBakeModule.cpp` owns
all bake state and GPU work. Its owning `TextureBakeModule` is already a friend;
all private binding/lifetime calls are in that same implementation unit.
Move Bind, SetTarget, SetCommandHistory, RegisterGpuQueueParticipant and Unbind
there. Call existing Impl DetachTargets and DestroySceneAssets directly.
Remove the seven service declarations and the imports needed only by them.
No new production file, wrapper, Pimpl, model copy or linkage convention.

The service constructs its Impl unconditionally, has no move/copy operations,
and never resets/releases that pointer while alive. Module-owned internal
calls can use it directly; public service guards remain. Preserve participant
registration checks, target epochs, callbacks, device-idle ordering, stale
completion, output ownership and shutdown exactly.

Claude's initial editor plan targeted snapshot-request ownership and borrowed
snapshot declarations. Root identified this smaller upstream fix instead:
private bake composition imports dominate the visualization/workspace closure.
The seven-import Snapshot prune remains insufficient by itself. Do not mix the
outbound attachment proposal into this slice. Root's hypothetical retained-owner
union predicts Bake 102→21, Visualization 117→50, Snapshots 135→99 and private
Attachment 136→100; only rebuilt compiler metadata may establish final counts.

## Acceptance criteria
- [x] Capture exact prior source, discover existing owners and review candidate scope with Claude.
- [x] Confirm private access/non-null ownership and implement the existing-Impl refactor.
- [x] Preserve all public record bodies and runtime behavior; add compiler-metadata guards.
- [x] Review a fixed diff with Claude and fix confirmed findings.
- [x] Pass native CPU, focused separate sanitizer, affected actual Vulkan and strict structural checks.
- [x] Synchronize ownership docs/inventory and record exact footprint, dependency evidence and remaining scope.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox --parallel 8
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```
Use the isolated ci-asan and ci-ubsan presets, fresh configuration and the
IntrinsicCpuTests build target. Run each serially with the same exclusions and
`-R 'TextureBake|BakedTexture|SurfaceAppearance|RuntimeSceneLifecycle|RuntimeWorldRegistry|RuntimeJobService|Workspace|ModelCache|SelectedAnalysis|EditorCompilationLocality'`.
Focused sanitizer selection is appropriate to the private composition-only
change; the full native CPU gate covers the combined source. Full sanitizer
PR/merge requirements remain unchanged.

Use ci-vulkan and IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests for the GPU
cases PropertyTextureModuleBakesRebindsRebakesAndRemovesOnVulkan,
SurfaceAppearanceBakesSelectedPropertyAndRestoresAttributes, and
ImportedObjectSpaceNormalBakeBindsAndReadsBackExactTargetSlice. Retain existing
labels, registered deadlines and leak settings. Run tests only after builds
finish; preserve BUG-188's host discovery workaround and BUG-180's separate
leak-enabled follow-up. Add missing behavior coverage only if review exposes a gap.

Run strict clean-workshop, task/layout/root, source-documentation and skill
checks; regenerate the module inventory and session brief. Keep changes
uncommitted. Evidence starts in `/tmp/intrinsic-runtime241-20260913/`; final
records belong in `build/analysis/runtime241-texture-bake-boundary-2026-09-13/`.
No repeatable compile-time or whole-engine completion claim is implied.

## Implementation notes
Claude confirmed the existing friendship and non-null ownership, and identified
that direct Impl calls must preserve the deleted wrappers' `noexcept` boundary.
That correction is implemented for detachment and scene-asset cleanup. Duplicate
participant registration still returns an invalid handle. Claude implemented
the method moves; root completed the remaining callers and metadata guards
after the bounded CLI invocation reached its turn limit. No production file,
wrapper, compatibility path or backend is added.

## Results and remaining work
Implemented, reviewed and verified in the working tree. Two production files,
56 physical lines and 53 nonblank lines removed; no production file added or
deleted. All public record/declaration bodies and public service methods remain
unchanged. Independent fixed-diff Claude review found no blocking defect.

Actual rebuilt Clang23/CMake module closures:

| Interface | Before | After |
| --- | ---: | ---: |
| TextureBakeModule | 102 | 21 |
| VisualizationEditingOperations | 117 | 50 |
| EditorWorkspaceSnapshots | 135 | 99 |
| Private.EditorWorkspaceAttachment | 136 | 100 |

No new dependency appears in those closures. These are structural counts, not
matched compilation timings or a performance claim.

Verification on the same 1,362 captured source/build inputs:
- Native ci IntrinsicTests and ExtrinsicSandbox build passed. Full CPU gate:
  4,591 passed, one expected ASan-only GlfwLifecycleLsan skip, zero failures.
- Fresh isolated ci-asan and ci-ubsan IntrinsicCpuTests builds passed; the
  declared focused serial selection passed 86/86 in each, without skips.
- ci-vulkan sandbox acceptance build passed; all three declared actual Vulkan
  bake/rebake, surface-display and normal-map readback cases passed, no skips.
- Four new compiler-metadata guards passed in native and both sanitizer trees.
  Strict structural/documentation checks and source comparisons passed;
  module inventory and session brief refreshed.

Full records, frozen review and exact before/after source identity are in
`build/analysis/runtime241-texture-bake-boundary-2026-09-13/`. Source remains
uncommitted and non-claim-eligible; all prior dirty work is preserved. Keep
this task active until accumulated changes are integrated. Remaining scope is
integration, matched compile timing, justified further editor frame/attachment
locality work, and existing Framework24 acceptance. The alternative outbound
snapshot-request/borrow plan was not implemented. BUG-188's host discovery
workaround and BUG-180's separate leak-enabled follow-up remain open.
