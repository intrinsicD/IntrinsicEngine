---
id: UI-076
theme: J
depends_on: [UI-070]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: GPU evidence follow-up; evidence is the gpu;vulkan smoke output and captured images.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog. This adds GPU evidence for the UI-070 camera command only; it changes no property, publication, recipe-slot or locality contract.
---
# UI-076 — Camera preset and focus visual smoke

## Goal
Prove on a Vulkan host that the UI-070 camera presets, Focus selection and
`set_camera` frame real geometry as intended, closing UI-070's
`CPUContracted → Operational` gap.

## Context
- UI-070 landed the editor camera command, the Camera/Render panel buttons and
  the agent `set_camera`.
- Its tests are CPU only: framing math, ImGui buttons and agent replies. They do
  not show what the rendered frame looks like.
- UI-070 review called out three things to check on real geometry:
  - each of the seven presets frames an off-origin mesh;
  - Focus selection centres the selected entity;
  - the TopDown pose sets orthographic height and far plane from altitude.
- Run under Xephyr as the memory notes describe. Run the test binaries directly
  and check for `[       OK ]`, because a skipped smoke reports as passed.
- Follow `intrinsicengine-gpu-smoke-authoring` for skip vs fail and for readback
  assertions.

## Acceptance criteria
- [ ] A gpu;vulkan smoke loads an off-origin mesh and, for each preset, applies it through the editor command. It captures the viewport and asserts that the mesh projects into the frame centre region with a non-black fraction above a threshold.
- [ ] The smoke asserts that Focus selection on one of two entities centres that entity.
- [ ] The smoke asserts that a TopDown pose changes the framed extent with altitude.
- [ ] `set_camera` with a preset gives the same capture, within tolerance, as `view_screenshot` with the same preset.

## Verification
```bash
cmake --build build/ci-vulkan -j$(nproc)
# under Xephyr (see memory note live-sandbox-via-xephyr); run the binary directly and check for [       OK ]
ctest --test-dir build/ci-vulkan -R 'CameraPreset' -L gpu -L vulkan --output-on-failure --timeout 180
python3 tools/agents/check_task_policy.py --root . --strict
```
