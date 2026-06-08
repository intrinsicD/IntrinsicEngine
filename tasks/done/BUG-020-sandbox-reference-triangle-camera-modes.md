# BUG-020 — Sandbox reference triangle camera modes

## Goal
- Ensure the default sandbox `ReferenceTriangle` remains a first-class authored ECS renderable and starts centered/usable under every runtime camera controller mode.

## Non-goals
- No renderer/framegraph compiler dependency rewiring.
- No Vulkan-only fix path or promoted-backend-specific assertion.
- No new sandbox features beyond preserving the authored triangle contract and camera seeding.

## Context
- Symptom: the default triangle is expected to behave like an imported mesh-backed entity, but scene persistence currently drops its `VisualizationConfig` appearance component.
- Symptom: switching to or starting in top-down camera mode seeds the top-down target from the camera position XZ instead of the point the seed is looking at, moving the reference triangle to the viewport edge.
- Expected behavior: the default triangle keeps mesh-domain `GeometrySources`, render hints, selection/stable-id/editor-visible components, and visualization config through the promoted scene document seam; all camera controller modes start with the reference triangle focus centered.
- Impact: sandbox startup and mode switching can look broken even though extraction/rendering are valid, and save/load can silently degrade the default triangle's authored appearance.
- Completed: 2026-06-08.
- PR/commit: pending local commit.

## Required changes
- [x] Persist and restore `Graphics::Components::VisualizationConfig` through `Extrinsic.Runtime.SceneSerialization`.
- [x] Seed `TopDownCameraController` from the reference camera focus point instead of the seed position XZ.
- [x] Add regression coverage proving the default triangle remains a mesh-backed authored entity after scene-document round-trip.
- [x] Add regression coverage proving every camera controller mode centers the reference triangle focus at startup.

## Tests
- [x] Update runtime contract tests for `RuntimeSceneSerialization`, `RuntimeReferenceScene`, and `RuntimeCameraControllers`.
- [x] Run focused runtime contract verification.
- [x] Run repository task/docs/layering/test-layout structural checks for the touched scope.

## Docs
- [x] Update `src/runtime/README.md` to document visualization-config persistence and camera-mode seeding behavior.
- [x] Retire this bug record to `tasks/done/` and update `tasks/backlog/bugs/index.md`.

## Acceptance criteria
- [x] Repro is documented and covered by automated tests.
- [x] Default triangle scene-document round-trip preserves render hints, mesh topology, stable/selectable identity, and white `VisualizationConfig`.
- [x] Orbit, fly, free-look, and top-down controller seeds all place the reference triangle focus at the viewport center.
- [x] Fix does not introduce layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'Runtime(CameraControllers|ReferenceScene|SceneSerialization)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R '(ReferenceSceneRegistry|TriangleProviderContract|ReferenceCameraBuildInput|EngineReferenceScene)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
```

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Adding renderer/RHI state, GPU handles, or asset-service traffic to scene serialization.
- Editing `src/graphics/framegraph/Graphics.RenderGraph.Compiler.cpp` unless a failing test proves pass-order compilation is the owner.

## Maturity
- Target: `CPUContracted`.
- This slice proves the runtime/ECS/document and camera-controller contracts in the default CPU/null gate; no `Operational` follow-up is owed unless promoted Vulkan visual proof finds a separate backend defect.
