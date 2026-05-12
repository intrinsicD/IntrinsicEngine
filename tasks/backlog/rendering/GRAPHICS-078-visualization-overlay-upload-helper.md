# GRAPHICS-078 — Backend visualization-overlay upload helper (vector field + isoline)

## Goal
- Implement the per-frame host-visible upload helper for `VectorFieldOverlayPacket` / `IsolineOverlayPacket` declared by `GRAPHICS-014Q`: a backend-local helper under `src/graphics/vulkan` that expands sanitized packet spans into per-frame transient vertex/index buffers consumed by dedicated visualization-overlay passes that LOAD `SceneColorHDR`/`SceneDepth` next to `Pass.Forward.Line`/`Pass.Forward.Point`. Two pipeline variants per packet kind (depth-tested vs always-on-top), mirroring `GRAPHICS-077`.

## Non-goals
- No routing through retained line/point cull buckets (rejected per `GRAPHICS-014Q`).
- No Htex / UV-bake atlas residency (texture residency is `GRAPHICS-015` which is done — the atlas reference path is consumed but not produced here).
- No PropertySet → packet adapter (that is `RUNTIME-082` `VisualizationAdapters`).
- No RHI/renderer module surface change.

## Context
- Status: not started.
- Owner/layer: `graphics/vulkan` for the helper + passes; `graphics/renderer` for executor routes.
- Planning anchors: `tasks/done/GRAPHICS-014-visualization-attributes-overlays.md`, `tasks/done/GRAPHICS-014Q-visualization-runtime-backend-clarifications.md`.
- Today: `NullRenderer::SubmitRuntimeSnapshots` accepts `VisualizationVectorFields` / `VisualizationIsolines` spans, but no GPU upload helper or pass body exists.
- The helper mirrors the `GRAPHICS-077` transient-debug pattern: per-frame host-visible buffers, recycled each frame, never retained on `GpuWorld`.

## Required changes
- [ ] Add a `VisualizationOverlayUploadHelper` (backend-local) producing per-frame transient vertex/index buffers for vector-field glyphs and isoline polylines.
- [ ] Add two pipeline variants per packet kind (depth-tested + always-on-top) for `VectorField` and `Isoline`. Total 4 pipelines.
- [ ] Add `VisualizationOverlayPass` (or per-kind passes — implementer decides between one combined pass or two split passes) writing `SceneColorHDR` + `SceneDepth` (LOAD-store) after `Pass.Forward.Line` / `Pass.Forward.Point`.
- [ ] Add `VisualizationOverlayUploadDiagnostics` field on the renderer diagnostics aggregate, mirroring `TransientDebugUploadDiagnostics`.
- [ ] Add executor branches routing the new pass(es) through the recorded taxonomy.

## Tests
- [ ] `contract;graphics` test: with N vector-field glyph packets + M isoline polylines per frame, the helper allocates one transient buffer set per kind and reports `VisualizationOverlayUploadDiagnostics` matching the submitted counts.
- [ ] `contract;graphics` test: per-packet depth-tested flag selects the matching pipeline variant.
- [ ] `contract;graphics` test: rejected packets (per `ValidateVisualizationPackets`) are not uploaded and counted in `VisualizationDiagnostics`.
- [ ] `contract;graphics` test: per-frame helper buffers do not leak across frames.

## Docs
- [ ] Update `src/graphics/vulkan/README.md` to add the helper + diagnostics row.
- [ ] Update `src/graphics/renderer/README.md` to record the new visualization-overlay passes' ordering position.

## Acceptance criteria
- [ ] Submitted vector-field / isoline packets draw into `SceneColorHDR`/`SceneDepth` deterministically.
- [ ] No retained GPU resources on `GpuWorld`; no RHI/renderer surface change beyond the diagnostics field.
- [ ] No regression in `ValidateVisualizationPackets` / `VisualizationDiagnostics`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests ExtrinsicBackendsVulkanContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Routing through retained line/point cull buckets.
- Retaining transient buffers on `GpuWorld`.
- Owning Htex regeneration or PropertySet → packet conversion (runtime-owned per `GRAPHICS-014Q`).

## Next verification step
- Land the helper + 4 pipelines + new pass(es) + executor routes, exercise the contract tests above.
