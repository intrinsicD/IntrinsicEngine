---
id: BUG-039
theme: G
depends_on: [RUNTIME-081]
maturity_target: CPUContracted
---
# BUG-039 — Orbit camera rotation lock

## Goal
- Make the promoted orbit camera rotate like the legacy trackball orbit camera: unrestricted accumulated rotation around the camera-local right/up axes, with no pitch clamp or world-up lock.

## Non-goals
- No changes to fly, free-look, or top-down camera control policy.
- No platform input API changes; mouse drag and scroll events already reach the controller.
- No renderer, RHI, Vulkan, shader, or GPU upload changes.
- No ECS camera entity promotion or camera-slot composition redesign.

## Context
- Owner/layer: `runtime` owns concrete camera controllers and produces `Graphics::CameraViewInput`; `platform` only supplies input state and `graphics` only consumes the camera snapshot.
- Symptom: orbit rotation has a locked region instead of freely rotating by any amount around any axis.
- Legacy reference: `src/legacy/Graphics/Graphics.Camera.cpp` updates orbit by rotating both the camera-target offset and `CameraComponent::Orientation` with quaternions around the current camera-local up and right vectors.
- Before this fix, promoted behavior stored orbit as scalar yaw/pitch, clamped pitch to +/-89 degrees, derived right/up from fixed world-up, and published a fixed `{0, 1, 0}` up vector.
- Ranked hypotheses tested by this task:
  1. The pitch clamp is the direct lock; a vertical drag beyond 90 degrees stalls near the pole instead of continuing through it.
  2. Fixed world-up reconstruction discards accumulated roll/local-up state; even if pitch were unclamped, the view would not match legacy trackball behavior.
  3. Mouse deltas are applied as world-yaw plus scalar pitch rather than as camera-local quaternion increments, producing gimbal-like behavior near the poles.
  4. Seeding collapses arbitrary camera orientation into yaw/pitch and can lose a non-world-up seed.

## Completion
- Completed: 2026-06-12. Commit/PR: this retirement commit.
- Root cause: promoted orbit reused fly-style yaw/pitch state for a trackball camera. That clamped pitch to +/-89 degrees, rebuilt the view from scalar yaw/pitch, and always published world-up, so a vertical drag could not pass through the pole or carry accumulated camera-local orientation.

## Required changes
- [x] Add a `contract;runtime` regression proving orbit drag can cross the pitch pole and invert the camera up vector.
- [x] Replace orbit yaw/pitch view state with accumulated orientation state derived from the seed forward/up vectors.
- [x] Apply orbit drag deltas as legacy-style quaternion rotations around the current camera-local up/right axes.
- [x] Keep radius clamp, scroll zoom, target focusing, and WASD panning behavior intact.
- [x] Remove any temporary diagnosis probes.

## Tests
- [x] Run the new focused orbit-camera regression red before the fix and green after the fix.
- [x] Run the full `RuntimeCameraControllers` focused contract suite.
- [x] Run the relevant build target and structural checks.

## Docs
- [x] Update `tasks/backlog/bugs/index.md`, retire the task record after the fix, append the retirement narrative, and regenerate `tasks/SESSION-BRIEF.md`.
- [x] Refresh generated module inventory only if the module surface changes.

## Acceptance criteria
- [x] A large orbit drag beyond +/-90 degrees continues rotation instead of clamping at the pole.
- [x] The resulting `CameraViewInput::Up` reflects accumulated camera orientation instead of fixed world-up.
- [x] Existing orbit zoom, yaw diagnostic wrapping, middle/right mouse drag, focus, and panning coverage still passes.
- [x] The fix stays within the `runtime` camera-controller layer and does not introduce new dependency violations.

## Verification
```bash
# Red gate before the fix:
#   RuntimeCameraControllers.OrbitDragCanCrossPitchPoleAndInvertUpVector failed with
#   view.Forward.z == -0.0174523834 and view.Up.y == 1 because promoted orbit
#   clamped pitch and published fixed world-up.

cmake --build --preset ci --target IntrinsicRuntimeContractTests
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeCameraControllers\\.OrbitDragCanCrossPitchPoleAndInvertUpVector' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed after the fix: 1/1 tests.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeCameraControllers' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 16/16 tests.

python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
# Passed: wrote docs/api/generated/module_inventory.md (482 modules).

cmake --build --preset ci --target IntrinsicTests
# Passed.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 2995/2995 tests.

python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
# Passed.
```

## Forbidden changes
- Shipping a fix without a regression test at the runtime camera-controller seam.
- Moving camera input handling into platform, graphics, ECS, or app layers.
- Reintroducing legacy module dependencies into promoted runtime code.
- Treating the issue as a renderer/GPU visibility bug without a runtime controller repro.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed because the bug is backend-neutral controller math covered by the runtime contract suite.
