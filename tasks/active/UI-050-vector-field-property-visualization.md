---
id: UI-050
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive operator-directed session; evidence is the diff, CPU/Vulkan test runs and independent review."
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation]
contract_review: "Binds typed vec3 element-domain properties to a persisted presentation recipe and caches property-backed GPU residency across frames."
---
# UI-050 — Appearance vector fields

## Goal
- Display any vec3 vertex, edge, face, graph-node, graph-edge or point
  property as instanced Vulkan arrows from the Appearance panel, with
  persisted, validated, undoable per-entity settings (Framework24 parity:
  domain first, then property; several fields; per-field style; removal).

## Context
- Before this task the "vector-field renderer" drew fixed clip-space
  placeholder segments: `UploadVectorFields` ignored the source buffers, the
  vertex shader applied no camera or transform, the explicit vector recipe
  never uploaded its position buffer, and the Geometry Visualization list
  showed an internal "adapter residency is not owned by this UI slice" note.
- Decisions: fields persist in `GeometryPresentationRecipe::VectorFields`
  keyed by (domain, property); anchors are render-derived (vertex positions,
  edge midpoints, face centers = vertex mean) and never written to geometry;
  vectors are ordinary object-space vectors (no normal-transform mode);
  every vec3 on a supported domain is offered, positions included;
  `Normalized` length defaults to 2% of the position bounding-box diagonal.
- Integer properties: current typed scalar support already offers `Scalar`,
  `Isolines` and `Color buffer` actions for UInt32/Int32 properties; no
  change is owed here.

## Acceptance criteria
- [x] Appearance > Vector fields: domain dropdown, compatible vec3 property
      dropdown, per-field visibility/length mode/length/width/RGBA/depth
      test/stride/max arrows and removal, independent of lane visibility.
- [x] One validated command (`ApplyEditorGeometryVectorFieldCommand`) serves
      UI and agents; changes are undoable; scene files round-trip and reject
      invalid layers.
- [x] Anchors and live rows are cached per entity domain and shared by
      fields; payloads are cached per property revision; caches are released
      when unused; steady frames rebuild, scan and upload nothing.
- [x] Residency never overwrites a buffer in flight and evicts unsubmitted
      inputs; the renderer no longer copies property payloads per frame.
- [x] Arrows are GPU-instanced from resident buffers with camera, transform,
      near-plane clipping, robust normalization, zero/non-finite, view-axis
      and sub-width handling and a distance-relative depth bias.
- [x] The internal-ownership message is replaced by a `Vector field` action.
- [x] Vulkan readback smokes pass on a Vulkan-capable host (Operational):
      9/9 vector-field/overlay smokes on NVIDIA GeForce RTX 4090, driver
      580.159.04 (2026-09-25). The unrelated shutdown LSan contract failure on
      this driver reproduces on unchanged `main`; see `BUG-221`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Visualization|SandboxEditor|RenderExtraction|SceneSerialization|VectorField' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -R 'VectorField|VisualizationOverlay' -L gpu -L vulkan --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Maturity
- Target: `Operational` on Vulkan-capable hosts via the readback smokes in
  `Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` and
  `Test.VisualizationOverlaySurfaceGpuSmoke.cpp`; `CPUContracted` elsewhere.

## Clean-workshop scorecard (2026-09-25)
`tools/ci/run_clean_workshop_review.sh . --strict` passed (`/tmp/vf-clean-workshop.log`).

| # | Check | Score | Evidence |
| --- | --- | --- | --- |
| 1 | Layer imports | pass | strict `check_layering.py`: no violations, empty allowlist |
| 2 | CMake links | pass | only source-list additions (`Runtime.RenderExtraction.VectorFields.cpp`, one test file); no new link edges |
| 3 | No downward type exposure | pass | graphics exports only graphics/RHI/glm types (`VectorFieldOverlayPacket`, `VisualizationVectorFieldDrawRecord`); ECS/runtime types stay in runtime |
| 4 | Renderer growth in owning seam | pass | no new renderer member: records live in `VisualizationOverlayUploadHelper`, residency policy in `VisualizationPropertyBufferResidency`, recording in `VisualizationOverlayPass` |
| 5 | Typed pass IDs | pass | reuses the existing `VisualizationOverlay` recipe pass; no new pass |
| 6 | Resource-driven recipe deps | n/a | no recipe edge changed |
| 7 | Maturity follow-up | pass | target `Operational`, proven by the listed Vulkan smokes |
| 8 | Temporary exceptions | n/a | none added |
