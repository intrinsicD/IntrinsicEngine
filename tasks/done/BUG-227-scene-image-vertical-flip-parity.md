---
id: BUG-227
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive renderer bug found while verifying UI-055; evidence is the live Sandbox reproduction, the new orientation-pinning GPU smoke and CPU camera pins, and the CPU and gpu;vulkan gates.
contract_schema: 1
contracts: []
contract_review: No catalog contract covers clip-space or fullscreen-pass orientation; the convention is recorded in the renderer README.
---
# BUG-227 — Scene image flips vertically with the number of fullscreen passes

## Completion — 2026-09-28
Commit: the enclosing `claude/cpd` commit records this retirement.

## Goal
The presented scene keeps world up on screen regardless of which fullscreen
passes (tonemap, AA, debug view, present) a frame recipe runs.

## Symptom
Live Sandbox (2026-09-28, Vulkan): the whole 3D image (meshes and selection
outline, not ImGui) mirrored vertically whenever transient debug primitives
appeared or disappeared (the CPD preview overlay). The reference triangle's apex
(+Y, camera up +Y) pointed down in the default path. Confirmed on the X display,
not only in engine screenshots, and also with the transient pass recording no
draws.

## Causes
- Two Vulkan Y compensations stacked: `SetViewport` always uses a negative-height
  viewport (NDC +Y at row 0) and the runtime camera projections also negated
  `Projection[1][1]`, so scene targets were rendered upside down. BUG-022 had
  treated the culling symptom by switching retained triangle pipelines to
  clockwise front faces.
- `present.vert`, `post_fullscreen.vert` and `debug_view.vert` derived
  `vUV = 0.5 * (p + 1)`, which under the negative viewport samples the source's
  last row at the top: every fullscreen blit mirrored its input. Tonemap plus
  present (2 blits) kept the upside-down scene; frames with transient primitives
  also enable the DebugView blit (`EnableDebugView` includes
  `HasTransientDebug`), and 3 blits looked right by accident. FXAA/SMAA would
  have changed the parity too.
- The acceptance smoke knew: it read presented pixels at `height - 1 - y`.

Root cause found with two independent read-only analyses (Fable 5.1 subagent,
Codex) that agreed with the per-pass bind log.

## Fix
- Camera projections no longer flip Y (`Runtime.CameraControllers.cpp`); retained
  triangle pipelines use counter-clockwise front faces again.
- The three fullscreen vertex shaders use `vUV.y = 0.5 * (1 - ndc.y)`, so a blit
  preserves orientation and parity no longer matters.
- Renderer README states the convention. Picking rays, gizmo projection and
  primitive refinement go through the same view-projection and stay consistent.
- Marching cubes emitted inward-wound triangles (the table's order faces the
  low-value side with this cube layout; `kTriTable[1]` gives a normal toward the
  inside corner). Reconstructed surfaces were drawn from their inside under
  back-face culling; `PointConstructionGpuSmoke` exposed it once the projection
  changed. Triangles are now emitted counter-clockwise from outside, matching the
  gradient normals (`Test_Grid` sphere test pins it). Found by the Fable review.
- Depth prepass, forward surface and entity/face id shaders declare
  `invariant gl_Position`, which the EQUAL depth tests need for bit-identical
  depth. Hardening: it is not proven to have caused a visible defect.
- Scalar isoline acceptance smoke: its non-isoline probe compared the lit,
  tonemapped band to the raw LUT colour and had passed on the mirrored image
  by 6 units; it now requires clear separation from the isoline and background
  colours.
- Follow-ups offered as separate tasks: decouple DebugView from transient
  primitives (cost), verify the shadow-map lookup's Y convention.

## Acceptance criteria
- [x] GPU smoke `RuntimeSandboxAcceptanceGpuSmoke.PresentedSceneKeepsWorldUpWithAndWithoutTransientDebug` probes an asymmetric inside/mirror pair on the presented reference triangle with and without a transient overlay.
- [x] The vector-field acceptance smoke reads unmirrored pixels; camera-controller contract tests pin `Projection[1][1] > 0` and apex above base for up = +Y.
- [x] Frame-lifecycle and refinement/culling tests use the unflipped projection and CCW winding.
- [x] Live Sandbox shows the reference triangle apex up with and without the CPD overlay.
- [x] Marching-cubes output faces outward (unit test); the point-construction GPU smoke renders the reconstructed sphere lit from outside.

## Verification
```bash
cmake --build --preset ci
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci-vulkan
DISPLAY=:7 ctest --test-dir build/ci-vulkan -L 'gpu|vulkan' --timeout 600 --output-on-failure
```
