---
id: BUG-236
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive follow-up of an accepted UI-078 limitation; evidence is the diff, tests, and CI
contract_schema: 1
contracts: [runtime.gizmo-transform-session]
contract_review: The fix patches how the vendored ImGuizmo picks and drags single-axis scale handles under the Sandbox frontend, which the gizmo-transform-session contract covers; the runtime matrix session is unaffected.
---
# BUG-236 — ImGuizmo scale axis handles fail where the eye lies in the axis plane

Status: done. Commit 1 (CPU fix) and commit 2 (Vulkan scale smoke) landed;
all acceptance criteria are met.

## Goal
- The Sandbox gizmo's X/Y/Z scale handles can be grabbed and dragged in
  every view, including views where the eye lies in an axis plane through
  the pivot, and the top-down orthographic view.

## Context
- Reported in UI-078 slice 3c (`b513f2787`) as "top-down orthographic X/Z
  scale handles cannot be picked" and accepted as a limitation at UI-078's
  retirement.
- **Reproduction on `3c0261993` (unpatched ImGuizmo 1.10):** the top-down
  X and Z drags pass; the original report does not reproduce. The test
  helper `AxisHandle` chose between exactly symmetric `+`/`−` projections by
  float noise and could press the side ImGuizmo does not draw (ImGuizmo keeps
  `+` unless `−` is longer by more than `FLT_EPSILON`). With the old helper,
  the top-down X case and the original symmetric pose of
  `RejectedPreviewShowsReasonWritesNothingAndReleaseCommitsLastAccepted`
  (moved in `95d093bb1`) fail on unpatched 1.10; with the fixed helper,
  which keeps `+` on a tie, both pass. Whether the slice 3c observation had
  the same cause (or the pre-BUG-235 OpenGL depth range) is not known.
- **Real defect:** perspective views where the eye lies in an axis-normal
  plane through the pivot, e.g. the default orbit camera looking along −Z at
  the origin. In `ImGuizmo.cpp`:
  - `GetScaleType` intersects the pick ray with the plane through the pivot
    whose normal is the axis. With the eye in that plane the hit lies at the
    eye, and its projection is noise: frontal X is not picked at all; frontal
    Y grabs the X handle instead.
  - `HandleScale` drags single-axis scale on a fixed plane (X→normal Y,
    Y→Z, Z→X). Frontal X's plane contains the eye, so an off-line drag
    collapses the scale to 0.001. An exactly on-line drag works only through
    `IntersectRayPlane`'s parallel fallback (`t = −1`).
- **Fix:** overlay port `tools/vcpkg/overlay-ports/imguizmo` (registry 1.10
  port, same archive hash, port-version 1) with
  `fix-scale-axis-picking.patch`:
  - `GetScaleType` tests the mouse against the drawn screen segment (as
    `GetMoveType` does for translate), keeping the 12 px threshold, axis
    order, center priority, flip and masks, and skipping hidden axes.
  - `HandleScale` replaces a fixed plane within ~6° of edge-on
    (`|n·v| < 0.1`) at drag start with the camera-facing plane through the
    axis, `n = normalize(v − a·(a·v))`; `v` is the pivot direction from the
    eye, or the ray direction when orthographic. A singular or non-finite
    ratio keeps the last scale.
- Rotate rings are out of scope and were not exercised here.

## Acceptance criteria
- [x] The scale axis handles grab and drag in the top-down orthographic and
      in the frontal perspective view; other views keep their behavior.
- [x] `SandboxEditorGizmo.FrontalScaleAxesDragAndUndo` fails on unpatched
      ImGuizmo 1.10 and passes with the patch;
      `TopDownScaleAxesDragAndUndo` guards the top-down case.
- [x] ADR 0006 §3 and the Sandbox README drop the limitation.
- [x] A Vulkan acceptance smoke drags a scale axis handle and undoes it
      (commit 2): `RuntimeSandboxAcceptanceGpuSmoke.ImGuizmoScaleAxisDragAndUndo`.

## Evidence (commit 1, 2026-10-08)
- Red (unpatched 1.10, final tests): `FrontalScaleAxesDragAndUndo` fails.
  X: no hover at the press, the click reaches scene picking, no drag, no
  undo. Y: the drag commits, but on the X handle (X scale 0.001, Y 1.0).
  `TopDownScaleAxesDragAndUndo` and the other 12 cases pass.
- Partial mutation (only the `GetScaleType` hunk): frontal Y passes,
  frontal X picks but its scale stays wrong, so both hunks are needed.
- Green (patched): `SandboxEditorGizmo.*` 14/14, three runs; focused CPU
  selection (Sandbox editor, gizmo, interaction, UI host, engine-layering
  suites) 202/202.

## Evidence (commit 2, 2026-10-08)
- `ImGuizmoScaleAxisDragAndUndo` reuses the UI-078 smoke driver
  (`RunImGuizmoGroupDragSmoke`, new `GizmoSmokeLayout::ScaleAxis`): R through
  the registered GLFW key callback, then a drag 4 px beside the X scale
  handle from 60% to 120% of its length, in the frontal perspective view
  centered on the pivot (asserted: eye in the pivot's X axis plane). The
  drag is claimed with camera and pick count unchanged and records exactly
  one history row on release; the first changed preview frame's own readback
  shows both triangles where their full world matrices put them (scale
  1.8 ± 0.1 about the pivot, old interiors background); the Undo click
  restores transforms, world matrices and pixels, and the geometry under the
  release frame's committed world matrices is gone from the undo frame. Pixel
  checks of the translate cases now also use full world matrices.
- Gone-check mutation: reading the first post-release frame back as the undo
  frame fails that check on both triangles in all three cases (with the
  80% preview matrices instead, both translate cases would miss it).
- Red (imguizmo 1.10 without the patch, ci-vulkan): the group never changes,
  so the first preview readback is missing. Exactly on the handle line the
  unpatched build passes by numerical accident, hence the 4 px offset (as in
  the CPU cases).
- Green (port-version 1): the scale case and both translate cases 3/3 each,
  12.4–13.0 s per run; NVIDIA RTX 3050, driver 590.48.01, X11/GNOME unlocked
  session, window = framebuffer 1600x900 (ratio 1), ci-vulkan with ASan/UBSan,
  on `53d44fef7` plus the then-uncommitted commit-2 diff. Claim C118
  (O262).

## Verification
```bash
cmake --preset ci -DVCPKG_MANIFEST_INSTALL=ON   # a cached OFF keeps the unpatched port; check Port-Version: 1 in external/vcpkg-installed/ci/vcpkg/status
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^SandboxEditorGizmo\.'
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -LE 'gpu|vulkan|slow|flaky-quarantine' -R '^(SandboxEditorGizmo|SandboxEditorPresentation|SandboxConfigSections|GizmoInteraction|GizmoInteractionEngineWiring|SceneInteractionModule|EditorUiHost|ImGuiAdapterEngineWiring|RuntimeEngineLayering|RuntimeEnginePrivateGlue)\.'
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root . --strict
# Vulkan (ci-vulkan; same Port-Version: 1 check in external/vcpkg-installed/ci-vulkan/vcpkg/status)
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure --no-tests=error -L gpu -L vulkan --repeat until-fail:3 -R '^RuntimeSandboxAcceptanceGpuSmoke\.ImGuizmo'
python3 tools/agents/check_ara_claims.py --root . --strict
```

## Completion
Completed 2026-10-08. Commit: `53d44fef7` (overlay patch and CPU regressions,
Codex approve with two notes, applied) and `87427dcd7` (Vulkan scale smoke,
two Codex rounds). Maturity: Operational on Vulkan for the frontal X scale
handle (ara C118); the top-down orthographic case was not broken.
