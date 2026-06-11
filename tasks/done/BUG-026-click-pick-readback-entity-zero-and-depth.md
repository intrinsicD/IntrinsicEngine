---
id: BUG-026
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-026 — Viewport click selection dead: render-id zero collision, UINT clear punning, and missing depth readback

## Goal

- Make viewport click selection work end-to-end in the promoted sandbox: a click resolves hit-vs-background, the true entity id, the world-space and entity-local cursor position reconstructed from a depth readback, and the closest sub-primitives (mesh face/edge/vertex, graph edge/node, point-cloud point) for the hit entity.

## Non-goals

- No hover-pick input bridge (hover requests stay unwired; the controller API already exists and is out of this bug's scope).
- No selection-outline visual redesign; outline keeps consuming the same id conventions.
- No transparent/special-material picking eligibility (`GRAPHICS-025` planning).
- No legacy `src/legacy` picking revival; legacy `Runtime.Selection.cpp` is reference material only.
- No editor UI redesign beyond keeping existing render-id displays consistent.

## Context

- Symptom: clicking anywhere in the sandbox viewport selects nothing — no outline, no hierarchy change, no diagnostics.
- Trace result (2026-06-10), two stacked defects plus one missing capability:
  1. **Render-id zero collision.** The render id written to the GPU instance table (`inst.EntityID`, sampled by `selection/*_id` shaders) is `static_cast<std::uint32_t>(entt::entity)` (`Runtime.RenderExtraction.cpp StableEntityId`, `SelectionController::ToStableEntityId`, `StableEntityLookup::ToRenderId`). A fresh registry hands the default sandbox `ReferenceTriangle` entity index 0/version 0 → render id 0. The readback drain (`Graphics.Renderer.cpp DrainCompletedPickingSlots`) and the `selection/entity_id.frag` contract both reserve `EntityId == 0` for background, so a click on the triangle publishes **NoHit**.
  2. **UINT clear-value punning.** `PickingPass` clears its two `R32_UINT` targets with `kDefaultClearTwoColorAttachments` (scene-color light blue). `Backends.Vulkan.CommandContext.cpp` writes `clearValue.color = {{ClearR, ...}}` (the `float32` union member); for a UINT attachment Vulkan reads `uint32[0]`, so background EntityId pixels read back as `0x3DCCCCCD` (bit pattern of 0.10f), not 0 → background clicks publish a bogus **Hit** whose id resolves to no entity and is silently dropped as a stale hit.
  3. **No depth readback.** Only EntityId+PrimitiveId (8 bytes/slot) are copied; `SceneDepth` is never sampled, so the runtime cannot reconstruct the world/local cursor position, and `RefinePickReadbackResult` passes no `LocalHit` anchor and no pick ray — the closest-vertex/edge refinement paths in `RefinePrimitiveSelection` are dead in the live path.
- Why existing gates missed it: CPU contract tests seed `MockDevice` readback bytes directly (e.g. EntityId=42) and never exercise entity-index-0 or the Vulkan clear translation; the `gpu;vulkan` smokes (BUG-018/019) drive *hierarchy* selection, which bypasses the readback path entirely.
- Owning subsystems/layers: `runtime` owns render-id encoding, pick consumption, unprojection, and refinement; `graphics/renderer` owns the recipe clear values, readback slot layout, and drain; `graphics/vulkan` owns clear-value translation.
- Legacy parity reference: `src/legacy/Runtime/Runtime.Selection.cpp` (cursor ray + world/local hit + nearest vertex/edge/face refinement), `src/legacy/Graphics/Passes/Graphics.Passes.Picking.cpp` (MRT id+depth pick pass).
- Completed: 2026-06-10.
- PR/commit: pending local commit on `claude/nifty-meitner-qz9y0z`.
- Verification (this session): full default CPU gate green (`ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`; only failures are the three pre-existing window-dependent `Engine::Run` tests that fail identically on the unmodified tree in this X11-less container, plus the benchmark smoke which passes once its binary is built); 12 new BUG-026 regression tests pass; `ExtrinsicBackendsVulkan` + `IntrinsicGraphicsVulkanContractTests` compile under the `ci-vulkan` preset; layering/test-layout/doc-links/task-policy checks clean; module inventory regenerated (no diff).

## Required changes

- [x] Centralize the render-id convention as `render id = uint32(entt handle) + 1`, `0 = background/none` (`entt::null` wraps to 0): `StableEntityLookup::ToRenderId`/`ToEntityHandle` become the single authority; `SelectionController::ToStableEntityId`/`ToEntityHandle` delegate; `RenderExtraction::StableEntityId` and `RefinePickReadbackResult`'s decode use the shared helpers; remove the raw cast in `Runtime.SandboxEditorUi.cpp`.
- [x] Clear `PickingPass` color targets to zero (dedicated zero-clear attachment pair instead of `kDefaultClearTwoColorAttachments`).
- [x] Make the Vulkan backend translate clear colors format-aware: UINT/SINT attachments convert `ClearR..ClearA` by value (`static_cast`) into `uint32[]`/`int32[]` instead of bit-punning floats.
- [x] Extend the picking readback slot from 8 to 16 bytes (`EntityId`, `PrimitiveId`, `Depth` as R32 float bits, pad); copy the `SceneDepth` pick pixel in the `PickingPass` executor route with `DepthRead → TransferSrc → DepthRead` barriers; require transfer-src usage on the depth target when picking readback is active.
- [x] Publish `Depth` plus the request pixel (`PixelX`/`PixelY`) on `Graphics::PickReadbackResult`.
- [x] Reconstruct the cursor position in runtime: `Engine` records per-sequence pick context (inverse view-projection, viewport, pixel, world pick ray) when it drains the pending pick; on readback consume it unprojects `(pixel, depth)` → world cursor position via the existing `BuildCameraViewSnapshot` NDC conventions.
- [x] Extend `RefinePickReadbackResult` with the pick context so refinement receives the entity-local cursor anchor (`HasLocalHit`/`LocalHit`) and the entity-local pick ray + radius fallback; result reports world/local cursor hit and closest face/edge/vertex/point ids.
- [x] Scale platform cursor positions from window (logical) coordinates into framebuffer pixels before pick submission and gizmo hit-testing (`WindowToFramebufferCursor`), so HiDPI hosts (content scale != 1) pick the pixel actually under the cursor.
- [x] _(Review follow-up, 2026-06-11.)_ Do not scale the missing-hint fallback radius by hit distance under orthographic projections: `PickReadbackContext` gains `OrthographicProjection` (set by `Engine` via the exported `IsOrthographicProjection`, keyed off the projection's perspective-divide coefficient), and `RefinePickReadbackResult` keeps the depth-invariant `orthoHeight / viewportHeight` pixel footprint for orthographic cameras (the promoted `TopDownCameraController`) instead of growing the radius with camera altitude. Regression: `OrthographicFallbackRadiusDoesNotScaleWithHitDistance` (same context resolves under perspective scaling, fails closed under the flag), `OrthographicFallbackStillResolvesWithinPixelFootprint`, `IsOrthographicProjectionDistinguishesProjectionKinds`.

## Tests

- [x] `contract;runtime` regression: entity with raw entt id 0 round-trips render-id encode → readback consume → `SelectedTag` (the exact sandbox triangle failure).
- [x] `contract;runtime` render-id encode/decode property coverage incl. `entt::null` → 0 and 0 → invalid handle.
- [x] `contract;graphics` recipe: `PickingPass` declares zero clear values for both ID targets.
- [x] `contract;graphics` readback: slot stride 16, three copies recorded (EntityId/PrimitiveId/SceneDepth) with depth barriers, drain publishes `Depth` and pixel, hit/miss/invalidated cases updated for the new layout.
- [x] `contract;runtime` unprojection: known camera + pixel + depth reconstructs the seeded world point (golden math vs `BuildCameraViewSnapshot` ray at parametric depth).
- [x] `contract;runtime` refinement: anchored mesh face hint resolves face + nearest vertex + nearest edge with world/local cursor positions; graph and point-cloud domains covered.
- [x] Default CPU gate stays green: `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.

## Docs

- [x] `src/graphics/renderer/README.md`: readback slot layout (16-byte slots incl. depth), zero-clear contract for ID targets, render-id `+1` convention note.
- [x] `src/runtime/README.md`: render-id encoding authority, pick-context capture, cursor reconstruction, refined-selection cursor anchor.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria

- [x] Clicking the default sandbox triangle (entt id 0) selects it; clicking background publishes a clean no-hit that clears selection per controller policy.
- [x] `Engine::GetLastRefinedPrimitiveSelection()` reports, for a mesh hit: entity id, face id, nearest vertex id, nearest edge id, world-space cursor position, and entity-local cursor position derived from the depth readback.
- [x] Graph hits report edge + nearest node; point-cloud hits report the point id.
- [x] All graphics/runtime selection contract suites pass on the default CPU gate.
- [x] This task closes at `CPUContracted` with the readback byte layout and id conventions contract-locked; the on-host Vulkan click smoke is owned by `BUG-026B`.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes

- Mixing mechanical file moves with this semantic fix.
- Making `graphics/*` read live ECS/runtime state (the pick context stays runtime-owned).
- Changing selection mutation policy or adding hover input in this task.
- Treating GPU primitive hints as authoritative over CPU geometry in refinement.

## Maturity

- Target: `CPUContracted`; the readback layout, id conventions, clear contract, and cursor/refinement math are contract-locked on the default CPU gate. **Reached.**
- This slice closes at `CPUContracted`; `Operational` owned by `BUG-026B` (on-host `gpu;vulkan` click-pick readback smoke — this container has no GPU/display, so the real-readback click round trip must be proven on a Vulkan-capable host).
