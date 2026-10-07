---
id: UI-079
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: operational rerun of existing smokes on another host; evidence is the recorded run
contract_schema: 1
contracts: [runtime.gizmo-transform-session]
contract_review: Extends the Operational evidence of the gizmo-transform-session contract (scene rectangle and cursor mapping at a pixel ratio other than 1); no contract change.
---
# UI-079 — Run the ImGuizmo smokes at a real HiDPI pixel ratio

## Goal
- Prove the Sandbox ImGuizmo drag on Vulkan with a framebuffer/window ratio
  other than 1, so the operational claim (ara C118) is no longer limited to
  ratio 1.

## Context
- UI-078 slice 4a (`49a609334`) ran both ImGuizmo smokes only on X11 at ratio
  1. The ratio ≠ 1 mapping rests on CPU evidence:
  `SceneInteractionModule.GizmoUiSceneRectIsTheCurrentClaimMappedBackFromFramebufferPixels`.
- Needs a host with real scaling, e.g. Wayland with a scale factor or macOS
  Retina. A forced `DisplayFramebufferScale` does not count.

## Acceptance criteria
- [ ] Both `RuntimeSandboxAcceptanceGpuSmoke.ImGuizmo*` cases pass on a host
      with ratio ≠ 1; window size, framebuffer size, both scale factors, scene
      rectangles, GPU/driver, session and revision are recorded here.
- [ ] Any failure is fixed or filed with its repro; ara C118 and ADR 0006
      Validation are updated to the measured ratio.

## Verification
```bash
cmake --preset ci-vulkan -DINTRINSIC_BUILD_SANDBOX=ON -DINTRINSIC_PLATFORM_BACKEND=Glfw -DINTRINSIC_HEADLESS_NO_GLFW=OFF -DINTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.ImGuizmo'
python3 tools/agents/check_ara_claims.py --root . --strict
```
