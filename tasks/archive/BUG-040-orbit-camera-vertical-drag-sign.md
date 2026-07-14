---
id: BUG-040
theme: G
depends_on: [BUG-039, RUNTIME-081]
maturity_target: CPUContracted
---
# BUG-040 — Orbit camera vertical drag sign

## Goal
- Make promoted orbit-camera vertical mouse drag use the expected app convention: dragging upward moves the camera above the target and dragging downward moves it below, while preserving the unrestricted trackball rotation fixed by `BUG-039`.

## Non-goals
- No fly, free-look, top-down, keyboard movement, scroll zoom, or horizontal orbit sign changes.
- No platform input coordinate-system changes; platform continues to report screen-space mouse positions with Y increasing downward.
- No renderer, RHI, Vulkan, shader, or GPU upload changes.
- No UI text/help changes unless the camera behavior contract changes beyond this sign fix.

## Context
- Owner/layer: `runtime` owns concrete camera controllers and consumes `Platform::Input::Context`; `platform` only reports raw mouse positions, and `graphics` consumes the resulting `CameraViewInput`.
- Symptom: after `BUG-039`, orbit camera up/down mouse movement feels inverted.
- Before this fix, promoted orbit computed `yDelta = pos.y - lastY` and applied `glm::angleAxis(glm::radians(-yDelta), right)`. Because screen Y grows downward, a mouse-up drag (`yDelta < 0`) rotated the camera below the target.
- Ranked hypotheses tested by this task:
  1. Orbit pitch sign is inverted: replacing `-yDelta` with `+yDelta` should make a mouse-up drag move the camera above the target and keep a mouse-down drag below.
  2. The bug is orbit-specific; changing fly/free-look pitch signs would regress existing legacy-sign tests.
  3. `BUG-039`'s cross-pole regression verifies freedom from clamping, but not the small-drag vertical sign, so it stayed green while the interaction felt wrong.
  4. Seeding and orientation-basis construction are not the cause; horizontal orbit and view validity tests still pass.

## Completion
- Completed: 2026-06-12. Commit/PR: this retirement commit.
- Root cause: `BUG-039` moved orbit rotation to quaternion accumulation but preserved the legacy algebraic `-yDelta` pitch sign. In promoted app screen coordinates, Y increases downward, so mouse-up (`yDelta < 0`) rotated the camera below the target. Orbit pitch needed to use `+yDelta`; fly/free-look retain their existing signs.

## Required changes
- [x] Add a `contract;runtime` regression proving a small upward orbit drag moves the camera above the target and points downward at it.
- [x] Flip only the orbit pitch drag sign while preserving quaternion accumulation and yaw behavior.
- [x] Keep the `BUG-039` pole-crossing regression meaningful under the corrected sign.
- [x] Remove any temporary diagnosis probes.

## Tests
- [x] Run the new focused orbit vertical-sign regression red before the fix and green after the fix.
- [x] Run the full `RuntimeCameraControllers` focused contract suite.
- [x] Run relevant structural checks.

## Docs
- [x] Update `tasks/backlog/bugs/index.md`, retire the task record after the fix, append the retirement narrative, and regenerate `tasks/SESSION-BRIEF.md`.
- [x] Refresh generated module inventory only if the module surface changes.

## Acceptance criteria
- [x] Dragging upward from the default orbit seed places the camera above the target and keeps the target centered.
- [x] Dragging downward remains valid and opposite in sign.
- [x] Unrestricted cross-pole orbit rotation from `BUG-039` still passes.
- [x] Existing yaw, middle-mouse, zoom, focus, registry, and other controller tests still pass.

## Verification
```bash
# Red gate before the fix:
#   RuntimeCameraControllers.OrbitMouseUpDragMovesCameraAboveTarget failed because
#   mouse-up produced Position.y == -0.62373507 and Forward.y == 0.2079117;
#   mouse-down produced the opposite inverted signs.

cmake --build --preset ci --target IntrinsicRuntimeContractTests
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeCameraControllers\\.OrbitMouseUpDragMovesCameraAboveTarget' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed after the fix: 2/2 tests in the current CTest table.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeCameraControllers' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 34/34 tests in the current CTest table.

cmake --build --preset ci --target IntrinsicTests
# Passed.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 3372/3372 tests.

python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
# Passed.
```

## Forbidden changes
- Changing platform mouse-coordinate semantics to compensate for one controller.
- Changing fly/free-look vertical mouse signs under this orbit-specific bug.
- Reintroducing yaw/pitch clamping or fixed world-up orbit behavior.
- Treating this as a renderer/GPU issue without a runtime controller repro.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed because this is backend-neutral controller math covered by the runtime contract suite.
