# RUNTIME-083 — `Extrinsic.Runtime.VisualizationAdapters` umbrella

## Status
- Status: in-progress.
- Owner/agent: Codex.
- Branch: `main`.
- Current slice: Slice C.2 — isoline adapter behavior.
- Next verification step: build `IntrinsicRuntimeContractTests`, run the
  focused isoline `VisualizationAdapters` contract tests, regenerate module
  inventory, and run runtime structural checks.

## Slice plan
- **Slice A.** Add the promoted
  `Extrinsic.Runtime.VisualizationAdapters` module under
  `src/runtime/Visualization/` with a data-only batch container, adapter
  options/stats, `IVisualizationAdapter`, `PropertyScalarAdapter`, and
  `VisualizationAdapterRegistry`. The property-scalar adapter maps a
  `Geometry::ConstPropertySet` float/double property to
  `Graphics::ScalarAttributePacket`, computes deterministic ranges from CPU
  values, and requires the caller to provide the externally owned GPU buffer
  address. No extraction-cache wiring and no GPU upload ownership in this
  slice.
- **Slice B (this slice).** Wire `RenderExtractionCache::ExtractAndSubmit` to resolve active
  visualization adapters for renderables carrying visualization-source
  components, accumulate packets into `RuntimeRenderSnapshotBatch`, and surface
  runtime extraction stats.
- **Slice C.1.** Add KMeans/color and vector-field adapters with
  deterministic CPU contracts. These adapters consume already-authored
  property buffers plus externally supplied GPU buffer addresses; they do not
  schedule KMeans or allocate vector-field residency.
- **Slice C.2 (this slice).** Add isoline adapter behavior with deterministic
  CPU contracts.
- **Slice D.** Add Htex metadata / fragment-bake adapter behavior, including
  `RecreateHtex` streaming scheduling and final extraction stats.

## Goal
- Open the runtime-side adapter umbrella declared by `GRAPHICS-014Q`: a new module `Extrinsic.Runtime.VisualizationAdapters` (planned home: `src/runtime/Visualization/Runtime.VisualizationAdapters.cppm`) hosting concrete producer adapters that translate `PropertySet` attributes, KMeans labels, isoline results, vector fields, and Htex metadata into `RuntimeRenderSnapshotBatch` visualization packet spans (`VisualizationAttributeBuffers`, `VisualizationScalars`, `VisualizationColors`, `VisualizationVectorFields`, `VisualizationIsolines`, `VisualizationHtexAtlases`, `VisualizationFragmentBakeAtlases`).

## Non-goals
- No graphics-side ownership of geometry algorithms.
- No mutation of the existing `ValidateVisualizationPackets(...)` validation surface.
- No editor UI; the editor maps user choices into adapter pre-filter inputs.

## Context
- Status: in-progress; Slices A-C.1 landed scalar extraction, KMeans/color, and
  vector-field adapter contracts, while isoline and Htex/fragment-bake adapters
  remain open.
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
- [x] Slice A: add the umbrella module, mutable visualization packet batch,
  options/stats, `IVisualizationAdapter`, `PropertyScalarAdapter`, and
  `VisualizationAdapterRegistry`.
- [x] Slice B: wire `RenderExtractionCache::ExtractAndSubmit` to invoke
  active scalar-field adapters for renderables carrying
  `Graphics::Components::VisualizationConfig::ScalarField`, accumulate packets
  into `RuntimeRenderSnapshotBatch::Visualization*`, and surface adapter-side
  scalar counters in `RuntimeRenderExtractionStats`.
- [x] Slice B: add runtime-owned visualization adapter registration and
  per-renderable binding APIs (`RegisterVisualizationAdapter`,
  `UnregisterVisualizationAdapter`, `SetVisualizationAdapterBinding`,
  `ClearVisualizationAdapterBinding`) so the cache owns adapter lifetime and
  externally supplied buffer-device-address metadata.
- [x] Slice C.1: add `KMeansLabelAdapter` / color-property packet production
  and `VectorFieldAdapter` packet production with deterministic source
  validation and externally supplied buffer-device-address metadata.
- [ ] Slice C.2: add isoline adapter packet production.

## Tests
- [ ] `contract;runtime` test: `PropertyScalarAdapter` produces a deterministic `ScalarAttributePacket` for a fixture `PropertySet`; out-of-range filters reject as expected.
- [x] Slice A: add `contract;runtime` coverage for scalar float/double property
  adaptation, missing/empty/non-finite source rejection, range override, batch
  clearing, and registry replace/unregister semantics.
- [x] Slice B: add `integration;runtime;graphics` coverage proving scalar
  adapter packets reach `RenderWorld::Visualization`, missing bindings do not
  synthesize packets, and invalid external buffer addresses are counted without
  reaching renderer validation.
- [x] Slice C.1 `contract;runtime` tests: `KMeansLabelAdapter` and
  `VectorFieldAdapter` produce deterministic outputs for fixtures and reject
  missing, unsupported, empty, non-finite, invalid-buffer, and invalid-range
  inputs.
- [ ] Slice C.2 `contract;runtime` test: `IsolineAdapter` produces deterministic
  packet output for fixtures.
- [ ] `contract;runtime` test: `HtexMetadataAdapter` enqueues a streaming task on `RecreateHtex` request and increments `RuntimeRenderExtractionStats::HtexRecreateScheduled` by 1.
- [ ] `contract;runtime` test: `ValidateVisualizationPackets` continues to reject malformed adapter outputs (centralized validation per `GRAPHICS-014Q`).
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [x] Update `src/runtime/README.md` to flip the planned `VisualizationAdapters` umbrella row to current state.
- [x] Refresh `docs/api/generated/module_inventory.md` after adding the module.
- [x] Update UI-001 and the working-sandbox gate review to show that
  visualization packet production is now partially unblocked by Slice A while
  extraction wiring and remaining adapter kinds are still open.
- [x] Slice B: update runtime docs, migration parity notes, active task index,
  and sandbox gate review to record scalar extraction-cache wiring while
  KMeans/vector/isoline/Htex adapter kinds remain open.
- [x] Slice C.1: update runtime docs and active task state to record the
  KMeans/color and vector-field adapter contracts while isolines and
  Htex/fragment-bake metadata remain open.

## Acceptance criteria
- [x] Slice A compiles the umbrella and proves `PropertyScalarAdapter` plus
  registry behavior with CPU `contract;runtime` tests.
- [x] Slice B wires scalar-field adapter registration/bindings through
  `RenderExtractionCache::ExtractAndSubmit` into renderer-visible visualization
  packet spans with focused CPU integration coverage.
- [x] Slice C.1 compiles KMeans/color and vector-field adapters and proves their
  deterministic packet contracts without adding graphics-side geometry
  ownership.
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

## Maturity
- Slice A closes `Scaffolded → CPUContracted` for the visualization-adapter
  umbrella, `PropertyScalarAdapter`, and registry only. It does not claim
  extraction wiring or all five adapter kinds.
- Slice B extends `CPUContracted` to scalar adapter extraction-cache wiring. It
  does not claim KMeans labels, vector fields, isolines, Htex/fragment-bake
  metadata, or streaming scheduling.
- Slice C.1 extends `CPUContracted` to KMeans/color and vector-field packet
  producer adapters only. It does not claim isoline generation,
  Htex/fragment-bake metadata, or streaming scheduling.
- Full `CPUContracted` for `RUNTIME-083` requires Slices C.2-D. `Operational`
  owned by [`RUNTIME-095`](../backlog/runtime/RUNTIME-095-working-sandbox-acceptance.md)
  or a later visualization-specific backend smoke.

## Next verification step
- Build `IntrinsicRuntimeContractTests`, run the focused isoline
  `VisualizationAdapters` contract tests, regenerate the module inventory, and
  run runtime structural checks.
