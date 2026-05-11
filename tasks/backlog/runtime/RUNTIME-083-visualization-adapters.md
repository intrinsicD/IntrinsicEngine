# RUNTIME-083 — `Extrinsic.Runtime.VisualizationAdapters` umbrella

## Goal
- Open the runtime-side adapter umbrella declared by `GRAPHICS-014Q`: a new module `Extrinsic.Runtime.VisualizationAdapters` (planned home: `src/runtime/Visualization/Runtime.VisualizationAdapters.cppm`) hosting concrete producer adapters that translate `PropertySet` attributes, KMeans labels, isoline results, vector fields, and Htex metadata into `RuntimeRenderSnapshotBatch` visualization packet spans (`VisualizationAttributeBuffers`, `VisualizationScalars`, `VisualizationColors`, `VisualizationVectorFields`, `VisualizationIsolines`, `VisualizationHtexAtlases`, `VisualizationFragmentBakeAtlases`).

## Non-goals
- No graphics-side ownership of geometry algorithms.
- No mutation of the existing `ValidateVisualizationPackets(...)` validation surface.
- No editor UI; the editor maps user choices into adapter pre-filter inputs.

## Context
- Status: not started.
- Owner/layer: `runtime`.
- Planning anchor: `tasks/done/GRAPHICS-014Q-visualization-runtime-backend-clarifications.md` ("runtime extraction is the sole owner of translating PropertySet attributes, KMeans labels, isoline results, vector fields, and Htex metadata into the `RuntimeRenderSnapshotBatch` visualization packet spans; concrete producer adapters live under a planned `Extrinsic.Runtime.VisualizationAdapters` umbrella").
- Bake-mapping selection (`UVBake`/`ExistingHtex`/`RecreateHtex`) is runtime/editor-owned. `RecreateHtex` requests are scheduled by runtime/geometry on a background task through `Extrinsic.Runtime.StreamingExecutor`; graphics increments `HtexRecreateRequestCount`.

## Required changes
- [ ] Add `src/runtime/Visualization/Runtime.VisualizationAdapters.cppm` exporting `Extrinsic.Runtime.VisualizationAdapters` with:
  - `class IVisualizationAdapter { virtual void Append(VisualizationPacketBatch& out, const VisualizationAdapterOptions& filters) = 0; }`,
  - concrete `PropertyScalarAdapter` (`PropertySet` → `ScalarAttributePacket`),
  - concrete `KMeansLabelAdapter` (`KMeansLabels` → `ColorAttributePacket`),
  - concrete `IsolineAdapter` (consumes geometry isoline algorithm outputs),
  - concrete `VectorFieldAdapter` (consumes vector-field samples),
  - concrete `HtexMetadataAdapter` (`HtexAtlas` metadata, including `RecreateHtex` background scheduling).
- [ ] Wire `RenderExtractionCache::ExtractAndSubmit` to invoke active adapters for entities that carry a visualization-source component, accumulate packets into `RuntimeRenderSnapshotBatch::Visualization*`, and surface adapter-side stats in `RuntimeRenderExtractionStats` (rejected counts, baked-vs-deferred counts, `RecreateHtex` enqueued count).
- [ ] Add `VisualizationAdapterRegistry` for selecting the active adapter set per renderable / per editor toggle.

## Tests
- [ ] `contract;runtime` test: `PropertyScalarAdapter` produces a deterministic `ScalarAttributePacket` for a fixture `PropertySet`; out-of-range filters reject as expected.
- [ ] `contract;runtime` test: `KMeansLabelAdapter` and `VectorFieldAdapter` produce deterministic outputs for fixtures.
- [ ] `contract;runtime` test: `HtexMetadataAdapter` enqueues a streaming task on `RecreateHtex` request and increments `RuntimeRenderExtractionStats::HtexRecreateScheduled` by 1.
- [ ] `contract;runtime` test: `ValidateVisualizationPackets` continues to reject malformed adapter outputs (centralized validation per `GRAPHICS-014Q`).
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/runtime/README.md` to flip the planned `VisualizationAdapters` umbrella row to current state.
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding the module.

## Acceptance criteria
- [ ] All five adapter kinds compile, register, and produce deterministic outputs.
- [ ] `RecreateHtex` runs as a streaming task and triggers exactly once per request.
- [ ] No new graphics imports beyond the existing `Graphics.VisualizationPackets` edge.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract|integration' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Importing geometry algorithm modules from `src/graphics/*`.
- Filtering packets at extraction time (validation is centralized in `ValidateVisualizationPackets`).
- Adding graphics-visible adapter-specific fields.

## Next verification step
- Land the umbrella + at least one adapter, wire extraction, exercise the contract tests above.
