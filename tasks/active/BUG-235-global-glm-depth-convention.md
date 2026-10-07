---
id: BUG-235
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive correctness repair; evidence is the diff, tests, and CI
contract_schema: 1
contracts: [repo.source-documentation]
maturity_target: Operational
---
# BUG-235 — Global GLM depth convention

## Goal
Every TU in the build uses one GLM clip convention (right-handed, radians,
Vulkan depth [0, 1]), and `Geometry::Frustum` extracts its near plane and
corners for that convention.

## Context
- Found 2026-10-07 by the UI-078 slice 4a orthographic ImGuizmo smoke: the
  top-down camera rendered no surfaces on Vulkan. At altitude 3 (near 0.1,
  far 100) the target projected to NDC z ≈ −0.941942 instead of ≈ 0.029029
  and was clipped.
- Root cause: `GLM_FORCE_DEPTH_ZERO_TO_ONE` (with `GLM_FORCE_RADIANS`,
  `GLM_RIGHT_HANDED`) was an `IntrinsicConfig` usage requirement that engine
  libraries do not link. In the `ci-vulkan` compile DB 0/885 engine `src`
  entries had it, while 434/434 test, 8/8 method and 38/47 benchmark entries
  did. `glm::perspective`/`glm::ortho` are inline templates, so the weak
  instantiations ODR-merge per binary: test binaries picked a test TU's ZO
  copy (hiding the defect), the Sandbox kept the controllers' OpenGL copy.
- `Geometry.Frustum` extracted the near plane as row3 + row2 and built near
  corners at NDC z = −1 (OpenGL), wrong for every RH-ZO matrix.
- Operator decision 2026-10-07: set all three defines build-wide and fix
  `Geometry.Frustum` (plan `/tmp/ui078/bug235-plan.md`). Shaders, picking,
  culling, HZB and `Graphics.CameraSnapshots` already assume [0, 1] depth.

## Scope
- `CMakeLists.txt`: directory-scope `add_compile_definitions` for the three
  defines; removed from `IntrinsicConfig`. `cmake/Dependencies.cmake`: the
  directory definitions join the ccache global module context.
- `Runtime.CameraControllers.cpp`: `static_assert` on `GLM_CLIP_CONTROL_RH_ZO`.
- `Geometry.Frustum.{cpp,cppm}`: near plane from row2, near corners at z = 0.
- Regressions: `RuntimeCameraControllers.EveryControllerProjectsVulkanZeroToOneDepth`,
  `Containment.FrustumFromZeroToOneDepthMatrixUsesCameraNearPlane`.
- Docs: clip-space convention in `docs/architecture/graphics.md`, pointers in
  `rendering-three-pass.md` and ADR 0006; debug-view depth is raw device depth.
- UI-078 slice 4a stays a separate diff/commit.

## Acceptance criteria
- [x] All engine, test, method and benchmark compile entries carry the three defines; nothing relies on `IntrinsicConfig` for them.
      Compile DBs of `ci` and `ci-vulkan`: src 885/885, tests 434/434, methods 8/8, benchmarks 47/47 (before: src 0/885, benchmarks 38/47).
- [x] Runtime projections and `Geometry::Frustum` map near to 0 and far to 1. The `static_assert` in `Runtime.CameraControllers.cpp` fails to compile with the HEAD CMake config; `Containment.FrustumFromZeroToOneDepthMatrixUsesCameraNearPlane` fails without the frustum fix; `RuntimeCameraControllers.EveryControllerProjectsVulkanZeroToOneDepth` confirms the analytic depth mapping (it cannot fail on HEAD in its binary, where the ODR merge picked a test TU's ZO copy).
      All three checked as stated (static_assert with the HEAD CMake config, frustum test with the frustum fix reverted).
- [x] Picking, Y orientation and existing depth tests stay consistent; the CPU gate and named Vulkan smokes pass without weakened assertions.
      Focused CPU 204/204; full CPU gate 5825 entries: 5823 passed, 1 skipped, 1 failure (`SandboxEditorGizmo.RejectedPreviewShowsReasonWritesNothingAndReleaseCommitsLastAccepted` on a degenerate pose where the X-scale plane contains the eye) fixed by a non-degenerate pose with unchanged assertions (`SandboxEditorGizmo` 3× 12/12); `Test.CcacheWorkflow.py` 23/23; Vulkan 59/59.
- [x] `RuntimeSandboxAcceptanceGpuSmoke.ImGuizmoOrthographicSplitViewportDragAndUndo` passes operationally (revision, GPU/driver recorded).
      Passed in the 59/59 `ci-vulkan` run (NVIDIA RTX 3050, driver 590.48.01, X11) on the `95d093bb1` source plus the then-uncommitted UI-078 slice 4a test diff (`49a609334`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGeometryTests IntrinsicGraphicsContractCpuTests IntrinsicGraphicsIntegrationCpuTests
ctest --test-dir build/ci --output-on-failure --no-tests=error --timeout 60 -R '^(RuntimeCameraControllers|CameraModule[^.]*|PrimitiveSelectionRefinementWiring|SceneInteractionModule|RenderWorldContract|GraphicsCullingContracts|GraphicsCullingSystem|GraphicsGpuWorld|GraphicsLightingShadowContracts|GraphicsLightClusterGrid|Containment|Overlap|Support)\.'
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure --no-tests=error -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan -DINTRINSIC_BUILD_SANDBOX=ON -DINTRINSIC_PLATFORM_BACKEND=Glfw -DINTRINSIC_HEADLESS_NO_GLFW=OFF -DINTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure --no-tests=error -L gpu -L vulkan --parallel 1 --timeout 120 -R '^(RuntimeSandboxAcceptanceGpuSmoke|DefaultRecipeSurfaceGpuSmoke|TransientDebugSurfaceGpuSmoke|HzbOcclusionConservatismGpuSmoke|ViewCaptureGpuSmoke|ImGuiSurfaceGpuSmoke)\.'
python3 tests/regression/tooling/Test.CcacheWorkflow.py
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Completion
Completed 2026-10-07. Commit: `95d093bb1`, after one Codex review (approve with two
notes, both applied: separated acceptance wording, `Geometry.Frustum.cppm`
synopsis). Maturity: Operational on Vulkan for the named smokes (the
orthographic gizmo run is part of ara C118).
