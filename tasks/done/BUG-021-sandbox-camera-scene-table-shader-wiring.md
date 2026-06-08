# BUG-021 — Sandbox camera scene-table shader wiring

## Goal
- Make the promoted sandbox camera affect the default triangle, depth, line, point, and selection rendering paths by publishing the runtime camera into the BDA scene table consumed by active shaders.

## Non-goals
- No new ECS camera component or renderer ownership inversion; the current promoted path uses `ReferenceScenePopulation::Camera` plus `CameraControllerRegistry` as the runtime camera source.
- No reintroduction of legacy `CameraBuffer` descriptor bindings into promoted Vulkan pipelines.
- No framegraph compiler rewrites unless a failing regression proves pass compilation is the owner.

## Context
- Symptom: the reference camera/controller can produce finite view/projection matrices, but the default triangle still appears camera-independent.
- Root cause: active BDA vertex shaders (`forward/default_debug_surface.vert`, `depth_prepass.vert`, `forward/line.vert`, `forward/point.vert`, and selection ID variants) wrote `gl_Position` from `dyn.Model` only, so controller updates never affected retained geometry clip-space positions.
- Promoted Vulkan binds the global bindless descriptor set and passes `GpuScenePushConstants::SceneTableBDA`; camera data must therefore travel through `GpuSceneTable`, not through legacy `layout(set=0,binding=0) CameraBuffer` shaders.
- The default sandbox does not currently create a first-class ECS camera entity. That is an intentional promoted-runtime seam from `RUNTIME-081`/`GRAPHICS-029B`; this bug verifies the existing controller-backed camera source is valid and consumed by shaders.
- Completed: 2026-06-08.
- PR/commit: pending local commit.

## Required changes
- [x] Add current-frame camera matrices and camera scalar state to `RHI::GpuSceneTable` with GLSL mirror fields in `assets/shaders/common/gpu_scene.glsl`.
- [x] Publish the extracted `RenderWorld.Camera` into `GpuWorld` before `GpuWorld::SyncFrame()` runs in render prep.
- [x] Update active BDA vertex shaders to transform world positions with the scene-table camera view-projection matrix.
- [x] Add regression coverage for scene-table camera publication and active shader source contracts.
- [x] Add or strengthen runtime camera-controller input mapping coverage against the legacy mouse/key semantics.

## Tests
- [x] Focused graphics contract tests cover scene-table camera writes and active shader camera usage.
- [x] Focused runtime camera-controller tests cover user input mapping to camera updates.
- [x] Run default CPU-supported verification for touched tests.
- [x] Run promoted Vulkan/default-recipe smoke if the host/device is operational; otherwise record the blocker.

## Docs
- [x] Update renderer documentation to state promoted BDA shaders read camera data from `GpuSceneTable`.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module-surface changes.
- [x] Retire this bug record to `tasks/done/` and update `tasks/backlog/bugs/index.md` when verification passes.

## Acceptance criteria
- [x] The controller-backed default camera remains finite, invertible, and centered on the reference triangle seed.
- [x] `GpuSceneTable` written during `PrepareFrame()` contains the active `RenderWorld.Camera` matrices before draw/cull passes consume scene-table data.
- [x] Active BDA vertex shaders no longer emit clip-space positions from model-space alone.
- [x] Runtime camera input mapping changes view state in the same directions as the legacy camera implementation.
- [x] No new layering violations or promoted Vulkan descriptor-layout regressions are introduced.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R '(RendererFrameLifecycle|RuntimeCameraControllers)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target ExtrinsicSandbox_Shaders
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci --output-on-failure -R '(DefaultRecipeSurfaceGpuSmoke\.ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples|RuntimeSandboxAcceptanceGpuSmoke\.ExtrinsicSandboxDefaultConfigProducesVisibleFrameWithValidation|RuntimeSandboxAcceptanceGpuSmoke\.HierarchySelectionKeepsDefaultSandboxVisibleWithOutline)' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

## Forbidden changes
- Adding live ECS/runtime knowledge to graphics subsystems.
- Adding `Vk*` or descriptor-set-specific details to renderer/RHI public APIs.
- Editing `src/graphics/framegraph/Graphics.RenderGraph.Compiler.cpp` without a focused failing test that identifies compiler ownership.
- Reverting or weakening BUG-020's first-class reference triangle scene-document contract.

## Maturity
- Target: Operational on Vulkan-capable hosts; CPUContracted everywhere else.
- This bug should not close as Scaffolded. If Vulkan is unavailable locally, CPU contracts must still prove the camera data path and active shader contract, with the Vulkan blocker recorded.
