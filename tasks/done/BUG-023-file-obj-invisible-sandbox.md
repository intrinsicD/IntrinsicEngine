# BUG-023 — File-backed OBJ imports render visibly in sandbox

## Status
- Status: done (retired 2026-06-09).
- Owner/agent: codex.
- Branch: `main`.
- Started: 2026-06-09.
- Completed: 2026-06-09. Commit/PR: pending.
- Next verification step: complete; focused CPU and opt-in GPU/Vulkan checks passed.

## Goal
Make OBJ files loaded through the runtime/editor import path visibly render in `ExtrinsicSandbox` instead of only materializing as hidden or culled mesh-domain entities.

## Non-goals
- Do not weaken geometry algorithm topology validation beyond the runtime-only renderability fallback already tracked in BUG-022.
- Do not add app-layer rendering shortcuts or legacy graphics fallback paths.
- Do not require broad PBR/material parity for OBJ assets; the default sandbox debug surface may remain the visual material.
- Do not make Vulkan/GPU smoke mandatory in the default CPU gate.

## Context
Owner layer: `runtime` for file-backed import/materialization and `graphics/renderer` for promoted default-recipe surface/depth/selection draw state. The reported symptom is stronger than BUG-022: any OBJ can import but is not visible. Current CPU coverage proves materialization and `GpuWorld` residency, but it does not prove file-backed OBJ pixels are drawn in the live sandbox view. The localized failure was a camera/bounds gap: centered file-backed OBJ geometry was already resident and rasterizable, but arbitrary imported geometry materialized at its authored coordinates without runtime-authored local/world culling bounds or a one-shot camera focus, so off-origin OBJ files could remain out of view or be culled against fallback bounds.

## Required changes
- [x] Add a deterministic file-backed OBJ sandbox repro that distinguishes imported OBJ pixels from the reference triangle/background.
- [x] Fix the smallest owning layer so imported OBJ meshes become visible without bypassing runtime composition.
- [x] Preserve renderer pipeline contracts; no culling/front-face policy change was needed.
- [x] Keep drag/drop and manual import paths routed through runtime-owned asset import/materialization.

## Tests
- [x] Add CPU/null contract coverage for imported standalone geometry culling bounds and camera focus.
- [x] Add opt-in `gpu;vulkan` smoke coverage for an off-origin file-backed OBJ visible in the sandbox default recipe.
- [x] Run focused runtime import, camera-controller, editor-import, and sandbox GPU smoke tests.
- [x] Run relevant structural checks.

## Docs
- [x] Update runtime docs and migration matrix for imported-geometry culling bounds and camera focus.
- [x] Retire this task to `tasks/done/` with completion metadata when verification passes.

## Acceptance criteria
- [x] An off-origin OBJ triangle imported through `Engine::ImportAssetFromPath(...)` is resident on the mesh lane, focused by the main camera, and contributes a distinguishable center pixel in an operational default-recipe Vulkan frame.
- [x] The default reference triangle remains visible.
- [x] Imported OBJ materialization still produces the same render-critical components as the promoted mesh lane, including local/world culling bounds.
- [x] No new layering violations are introduced.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|RendererFrameLifecycle' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
```

## Forbidden changes
- Do not edit `Graphics.RenderGraph.Compiler.cpp` unless the repro proves the compiled graph, rather than import/extraction/render state, is the owner.
- Do not add ECS/runtime imports to graphics layers or graphics/Vulkan imports to assets/geometry.
- Do not suppress validation-layer diagnostics to hide a render-path failure.
- Do not revert the existing UI-007/BUG-021/BUG-022 work in this checkout.

## Maturity
- Target: `Operational` on Vulkan-capable hosts for the file-backed OBJ sandbox path, with `CPUContracted` coverage for backend-neutral import/extraction behavior.
