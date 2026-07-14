# BUG-022 — Sandbox reference triangle camera frustum visibility

## Goal
- Ensure the default promoted sandbox camera initializes on the reference triangle and the active triangle-facing pipelines render that centered triangle after camera projection is applied.

## Non-goals
- No new ECS camera entity; the promoted camera source remains `ReferenceScenePopulation::Camera` plus `CameraControllerRegistry`.
- No framegraph compiler changes unless a focused failing test proves pass compilation owns the regression.
- No changes to line, point, post-process, or fullscreen rasterizer winding where triangle backface culling is not involved.

## Context
- Symptom: after active BDA shaders started consuming `scene.CameraViewProj`, the camera responds, but the default reference triangle can disappear.
- Expected behavior: the reference camera seed and every camera controller mode should place the triangle vertices inside clip space, and retained triangle pipelines should not backface-cull the default triangle solely because the Vulkan camera projection flips Y.
- Impact: the default sandbox no longer provides a visible out-of-the-box reference primitive even when camera input and scene-table camera publication work.
- Hypothesis: the reference seed frames the triangle, but triangle-facing pipeline descriptors still use `CounterClockwise` front-face winding while the Vulkan projection path flips Y, causing backface culling to reject the visible triangle.
- Completed: 2026-06-08.
- PR/commit: pending local commit.

## Required changes
- [x] Add regression coverage proving the reference triangle vertices are inside the default seeded camera frustum for all camera controller modes.
- [x] Update triangle-facing retained pipeline descriptors to use the front-face winding that matches the Vulkan Y-flipped camera projection.
- [x] Add/adjust renderer pipeline descriptor tests so future camera/shader changes cannot reintroduce the winding mismatch.
- [x] Update task/docs records describing the promoted BDA camera/winding contract.

## Tests
- [x] Focused runtime camera-controller tests cover full-triangle clip-space containment, not just center-point focus.
- [x] Focused graphics renderer tests cover triangle-facing pipeline front-face winding.
- [x] Run the default CPU-supported gate for touched tests.
- [x] Run promoted Vulkan/default-recipe smoke if the host/device is operational.

## Docs
- [x] Update renderer documentation if the pipeline winding/camera projection contract changes.
- [x] Retire this bug record to `tasks/done/` and update `tasks/backlog/bugs/index.md` when verification passes.

## Acceptance criteria
- [x] The default reference triangle vertices are inside clip space under Orbit, Fly, FreeLook, and TopDown seeded views.
- [x] Default debug, depth prepass, forward/deferred surface, and triangle selection pipelines use the front-face winding compatible with the Vulkan Y-flipped camera projection.
- [x] Line/point pipelines remain unchanged.
- [x] No new layering violations are introduced.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R '(RendererFrameLifecycle|RuntimeCameraControllers)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci --output-on-failure -R '(DefaultRecipeSurfaceGpuSmoke\.ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples|RuntimeSandboxAcceptanceGpuSmoke\.ExtrinsicSandboxDefaultConfigProducesVisibleFrameWithValidation|RuntimeSandboxAcceptanceGpuSmoke\.HierarchySelectionKeepsDefaultSandboxVisibleWithOutline)' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
```

## Forbidden changes
- Adding live ECS/runtime knowledge to graphics subsystems.
- Weakening the promoted camera scene-table contract from `BUG-021`.
- Reverting the reference triangle's first-class authored scene-document contract from `BUG-020`.

## Maturity
- Closed as Operational on a Vulkan-capable host. CPU contracts prove camera/frustum containment and pipeline winding, and the promoted Vulkan smoke proves the default sandbox triangle remains visible with validation enabled.
