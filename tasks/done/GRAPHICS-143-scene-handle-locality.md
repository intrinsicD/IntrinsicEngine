---
id: GRAPHICS-143
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive compile-locality refactor; fixed-diff review, compiler metadata and preset verification.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GRAPHICS-143 — Separate scene identities from residency storage

## Goal
Keep stable GPU instance/geometry identities independent of mutable named-buffer
and asset residency state. The operator requests continued reuse/compilation
cleanup with Claude outside the standing product focus.

## Decisions
- Baseline `013df9342`. GpuSceneSlot owns both two handle aliases and mutable
  residency storage. Five graphics interfaces use only the handles; LightSystem
  and transient-debug uploads inherit storage through them. Original compiler
  metadata rejects all seven proposed residency exclusion guards.
- Reuse Core.StrongHandle unchanged. Move the existing two tags and aliases to
  one declaration-only Graphics.SceneHandles module in ExtrinsicGraphics.
  Consumers spelling handles import their owner; actual residency consumers
  retain GpuSceneSlot. No re-exports, duplicate declarations or compatibility
  aliases. Whole-tree tag search finds only the owner and GpuWorld allocators.
- Right-sizing: one small leaf module isolates mutable residency layout from
  snapshot/renderer consumers. No new library, interface, wrapper or algorithm.
  Keeping everything together propagates buffer-map edits to handle-only users.
  Moving AssetId instead would leave that map dependency intact and enlarge
  scope across unrelated asset consumers. Borrowing GpuWorld alone also leaves
  the storage coupling. Retain existing lifetime/synchronization owners.
- Claude accepts the plan, flags changed tag module attachment and asks for
  direct consumer imports and fresh verification. All in-tree consumers rebuild;
  there are no external API clients. Fresh cache-disabled Clang 20 producer
  verification supplements the canonical Clang 23 build and CPU tests.
- Add compiler dependency guards for the seven consumers and the small handle
  owner using the existing boundary-test runner. These establish dependency
  shape, not an elapsed compilation-time improvement.

## Review
- Claude's diff review prompted consolidating the four overlapping compiler
  guards into their existing loop and giving the three additional consumers
  a clear NoResidencyStorage suffix. The handle-owner guard documents why RHI
  resource identities do not belong in this graphics identity leaf.
- Both alias and tag searches include all C++ source/header/test files; actual
  residency users retain that import. GpuWorld's private tag-based allocators
  import SceneHandles directly. Matching implementation units can use their
  primary interface; both compiler builds verify those consumers.
- Compiler metadata identified a legitimate second Asset.Registry path in
  RenderWorld and transient-debug uploads: VisualizationPackets stores texture
  AssetIds. Their guards exclude GpuSceneSlot, preserving that required asset
  dependency; the other five also exclude Asset.Registry. No asset API split
  or weakening of a pre-existing guard is needed.
- The VisualizationSyncSystem synopsis is required by source-documentation
  policy for a materially touched interface, not a behavioral change.

- Claude reviewed the final fixed source/test diff after these corrections and
  reported no blockers. Normalizing imports/comments confirms the 14 touched
  production C++ files retain identical bodies except the moved declarations.
  Source-documentation audit: zero errors; 24 existing ownership/lifetime/unit
  comments reviewed. Strict clean-workshop, task policy and docs-sync pass.
- Manual architecture scorecard: ownership/layer rows 1–3 pass; renderer growth,
  passes and capability rows 4–7 n/a; exception row 8 passes. SceneHandles uses
  only Core.StrongHandle, stays in the existing graphics target, and exports
  no mutable state. Inventory has 418 modules.

## Acceptance criteria
- [x] One handle owner; runtime and graphics capabilities preserved.
- [x] All seven compiler closures exclude residency storage; handle-only paths exclude asset storage.
- [x] Claude review findings addressed; focused and full CPU checks pass.
- [x] Fresh Clang 20 graphics producer passes; docs/inventory current; retire.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'CompilationLocality|GpuWorld|GpuSceneSlot|LightSystem|RenderWorld|TransformSync|UvView|GeometryResidency|RenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
tools/ci/run_clean_workshop_review.sh . --strict
```

Minimum-compiler verification uses a disposable ci-derived Null/headless
Clang 20 build with matching scanner, package installation and caches disabled,
tests/benchmarks off, targeting ExtrinsicGraphics. No GPU or sanitizer claim.

## Completion — 2026-09-15
- Commit reference: the enclosing scene-handle locality commit.
- Canonical ci Clang 23 full IntrinsicTests build passes with compiler-cache
  reuse disabled; final reconfigure/reconciliation reports no work remaining.
  Focused CTest: 182 passed, including all 66 compilation-locality guards.
  Full CPU: 4,662 passed, zero failures, one expected ASan-only GLFW skip
  (4,663 selected, 139.30 seconds).
- Fresh cache-disabled Clang 20 ExtrinsicGraphics build passes (546 steps).
  Eight interface boundary checks pass; the real residency owner's negative
  control still correctly rejects its required Asset.Registry dependency.
- GpuSceneSlot is absent from all seven guarded interface closures. Compiler
  module counts: GpuWorld 10 → 9, LightSystem 11 → 10, TransformSyncSystem
  11 → 10, UvView 24 → 23, GeometryResidency 11 → 10. RenderWorld stays 16
  and TransientDebugUploadHelper stays 17: the small handle owner replaces
  the map-bearing slot dependency, while visualization texture AssetIds remain.
- The final source/test diff exactly matches Claude's final reviewed packet.
  One declaration-only module replaces the original tag/alias owner directly;
  no runtime algorithm, lifetime policy, or GPU behavior changes. No elapsed
  compilation, runtime GPU or sanitizer claim.
- BUILD-006 retains independent backend/cache comparisons. RenderWorld and
  LightSystem still use GpuWorld; upload-helper sharing still requires matching
  byte/vertex caps, empty-input behavior and failure ownership before reuse.
