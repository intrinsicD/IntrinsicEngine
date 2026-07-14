---
id: RUNTIME-153
theme: F
depends_on:
  - RUNTIME-152
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-153 — Extract mesh primitive-view controls out of Engine

## Goal
- Move the legacy `MeshPrimitiveViewSettings` compatibility translation from `Runtime.Engine.cpp` into a narrow runtime control module, so `Engine` delegates instead of directly mutating `RenderEdges` / `RenderPoints` ECS components.

## Non-goals
- No public Engine API removal; `Engine::{Set,Get,Clear}MeshPrimitiveViewSettings(...)` remain compatibility facades for existing callers.
- No change to render-component authority, mesh primitive-view packing, extraction residency, or graph/point-cloud render-lane behavior.
- No UI redesign and no changes to `SandboxEditorUi` command DTOs.

## Context
- Owning subsystem/layer: `runtime`.
- `RUNTIME-106` made `RenderEdges` / `RenderPoints` component presence authoritative and left `MeshPrimitiveViewSettings` as a compatibility DTO. After `RUNTIME-152`, `Runtime.Engine.cpp` still contains the concrete DTO-to-component mutation/readback body and imports `Extrinsic.Graphics.Component.RenderGeometry` for that shim.
- The control translation is runtime-owned policy: it may import ECS scene registry, stable render-id decode, graphics render component value types, and the `MeshPrimitiveViewSettings` DTO. `Engine` should remain the caller that provides the current scene and clears the extraction compatibility cache.

## Required changes
- [x] Add `Extrinsic.Runtime.MeshPrimitiveViewControls` with apply/read/clear helpers for `MeshPrimitiveViewSettings`.
- [x] Move `MeshVertexViewRenderMode` <-> `RenderPoints::RenderType` conversion and `RenderEdges` / `RenderPoints` mutation bodies out of `Runtime.Engine.cpp`.
- [x] Keep Engine compatibility methods delegating to the new helpers and clearing `RenderExtractionCache` compatibility state.
- [x] Register the new module interface and implementation in `src/runtime/CMakeLists.txt`.

## Tests
- [x] Add/adjust a runtime layering contract proving `Runtime.Engine.cpp` no longer imports `Extrinsic.Graphics.Component.RenderGeometry` or names `RenderEdges` / `RenderPoints` directly.
- [x] Preserve scene lifecycle / primitive-view behavior through existing Engine compatibility tests.
- [x] Run focused runtime tests for mesh primitive-view controls and Engine layering.
- [x] Run the default CPU-supported correctness gate.

## Docs
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Update `src/runtime/README.md` and `tasks/backlog/runtime/README.md` with the factual control-module location.

## Acceptance criteria
- [x] `Runtime.Engine.cpp` delegates mesh primitive-view compatibility accessors to `Extrinsic.Runtime.MeshPrimitiveViewControls`.
- [x] `Runtime.Engine.cpp` does not import `Extrinsic.Graphics.Component.RenderGeometry` and contains no direct `RenderEdges` / `RenderPoints` mutation/readback body.
- [x] Existing `Engine::{Set,Get,Clear}MeshPrimitiveViewSettings(...)` behavior remains compatible for callers.
- [x] CPU gate, layering, task policy, docs sync, and test-layout checks pass.

## Status
- Completed on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.MeshPrimitiveViewControls` now owns the legacy `MeshPrimitiveViewSettings` apply/read/clear translation to authoritative ECS `RenderEdges` / `RenderPoints` components.
- `Runtime.Engine` keeps the compatibility facade only: it passes the current scene to the controls module and clears extraction compatibility state, but no longer imports `Extrinsic.Graphics.Component.RenderGeometry` or names the render component types directly.
- Clean-workshop manual rows 3-6: pass/n/a for this slice. No lower-layer public API exposes a higher-layer type, and no renderer member, frame-graph pass, or recipe dependency changed.
- PR/commit: pending.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'MeshPrimitiveView|RuntimeSceneLifecycle|RuntimeEngineLayering' --timeout 90
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
```

## Forbidden changes
- Removing Engine compatibility methods in this slice.
- Moving mesh primitive-view packing or render extraction residency.
- Changing render-lane component semantics or defaults.
- Adding Sandbox/UI feature behavior while moving the shim.

## Maturity
- Target: `Operational` — behavior-preserving extraction of an existing compatibility shim, verified through existing Engine/runtime contract tests and the default CPU gate. No GPU/Vulkan follow-up is owed because this does not change rendering execution.
