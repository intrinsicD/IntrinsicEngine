---
id: GRAPHICS-164
theme: B
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive behavior-preserving refactor; evidence is the diff, CPU renderer contract tests, the existing Vulkan renderer smokes, review and CI.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog. A private refactor inside `Graphics.Renderer.cpp`; no module surface, recipe-slot lookup, property binding, publication or locality contract changes. If a helper lands in a module interface, add repo.source-documentation.
---
# GRAPHICS-164 — Factor the pipeline reset/create/publish blocks in renderer init

## Goal
- `InitializeOperationalPassResources` (`src/graphics/renderer/Graphics.Renderer.cpp`,
  about 1,200 lines) repeats one pattern 40 times
  (`m_Subsystems.PipelineManager->Create`): reset the lease, clear the pass
  pipeline, create, then emplace the lease and publish the device handle, or log
  a warning. Factor this into helpers, one family at a time, where the blocks
  are truly the same.
- Origin: REVIEW-007 G04 (2026-10-06). The audit estimated −650 lines and a
  single helper; triage found the blocks are not uniform. Realistic saving:
  −250…−400 lines.

## Acceptance criteria
- [ ] Group the blocks into families by publication target and failure
      behavior: pass `SetPipeline`, lease only, special setters (for example
      transient-debug sphere depth-tested/always-on-top), and blocks whose lease
      gates the `bool` return (depth prepass). Only identical blocks
      share a helper; the others stay inline.
- [ ] Behavior is preserved per block: reset-before-create (no stale device
      handle after a failed rebuild), warning text, skipped-unavailable pass
      state, and the return value.
- [ ] `PipelineManager->Create` call order is unchanged, so the
      `FailPipelineCreateCall` indices used by MockRHI contract tests stay valid.
- [ ] Net reduction is recorded in the commit message; no new module surface.
- [ ] CPU renderer contract tests and the existing renderer GPU smokes pass on
      `ci-vulkan`.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure --timeout 120 --no-tests=error -R '^(RendererFrameLifecycle|DebugViewPassContract|PresentPassContract|TransientDebugSurfacePassContract|VisualizationOverlayPassContract)\.'
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^(DefaultRecipeSurfaceGpuSmoke|HzbOcclusionConservatismGpuSmoke|ImGuiSurfaceGpuSmoke|TransientDebugSurfaceGpuSmoke|VisualizationOverlaySurfaceGpuSmoke|RuntimeSandboxAcceptanceGpuSmoke)\.'
python3 tools/agents/check_task_policy.py --root . --strict
```

## Context
- Example sites: forward surface/line/point blocks near the top of the function;
  `m_SelectionEntityIdOutlinePipelineLease` and the UI-067 transient-debug
  sphere loop at the end (appended last so historical failure indices stay
  stable).
- Out of scope: pipeline descriptors (`Build*PipelineDesc`), the
  GRAPHICS-136 `Pipeline*` → `GraphicsState*` rename, shader changes.
