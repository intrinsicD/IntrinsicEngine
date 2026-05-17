# ADR 0009 — Visualization Packets, Validation, and Overlay Upload

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Runtime extraction (visualization adapters, packet authorship), Graphics (frozen `Extrinsic.Graphics.VisualizationPackets` validation seam, backend-local overlay upload helper)
- **Related tasks:** [`tasks/done/GRAPHICS-014`](../../tasks/done/GRAPHICS-014-visualization-attributes-overlays.md), [`GRAPHICS-014Q`](../../tasks/done/GRAPHICS-014Q-visualization-runtime-backend-clarifications.md), [`GRAPHICS-010Q`](../../tasks/done/GRAPHICS-010Q-transient-debug-backend-clarifications.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`docs/architecture/rendering-three-pass.md`](../architecture/rendering-three-pass.md), [`src/graphics/renderer/README.md`](../../src/graphics/renderer/README.md)
- **Supersedes:** none. Extracted from the `Extrinsic.Graphics.VisualizationPackets` bullet in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/active/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0008](0008-spatial-debug-visualizer-adapters.md) records the analogous `Extrinsic.Runtime.SpatialDebugAdapters` umbrella for spatial debug visualizers; the `Extrinsic.Runtime.VisualizationAdapters` umbrella in this ADR mirrors that pattern. [ADR-0012](0012-imgui-overlay-and-present-finalization.md) and the transient-debug expansion in `GRAPHICS-010Q` are the sibling backend-local upload helpers whose pattern the visualization overlay upload mirrors.

## Context

`GRAPHICS-014` established `Extrinsic.Graphics.VisualizationPackets` as the data-only graphics seam for scalar / color / vector attribute buffers, vector-field overlays, isoline overlays, UV-backed fragment-bake atlas descriptors, and Htex patch-preview / bake atlas descriptors. The packet contract is **frozen** by `GRAPHICS-014` — no new packet types or new diagnostic fields are added without an explicit follow-up.

`GRAPHICS-014Q` answered four producer-side / backend-side questions that `GRAPHICS-014` deferred:

1. Who owns translating `Geometry::PropertySet` attributes, KMeans labels, isoline results, vector fields, and Htex metadata into `RuntimeRenderSnapshotBatch` visualization spans?
2. Where does packet validation live, and who is allowed to filter invalid records?
3. How are vector-field glyphs and isoline polylines actually uploaded to the GPU — through the retained cull buckets or through a backend-local transient path?
4. Who decides whether a fragment bake uses existing texcoords, an existing Htex atlas, or triggers Htex regeneration?

This ADR captures those answers as the canonical durable home. `docs/architecture/graphics.md` keeps a short canonical summary of the `Extrinsic.Graphics.VisualizationPackets` seam (data-only packet builder, graphics-side validation, frozen diagnostics) and retains a single pointer line to this ADR for the runtime ownership, centralized validation rule, backend-local overlay upload, and fragment-bake mapping policy.

## Decision

### 1. Runtime ownership of visualization packet translation

Runtime extraction (`Extrinsic.Runtime.RenderExtraction`) is the sole owner of translating `Geometry::PropertySet` attributes, KMeans labels, isoline results, vector fields, and Htex metadata into the `RuntimeRenderSnapshotBatch` visualization packet spans:

- `VisualizationAttributeBuffers`.
- `VisualizationScalars`.
- `VisualizationColors`.
- `VisualizationVectorFields`.
- `VisualizationIsolines`.
- `VisualizationHtexAtlases`.
- `VisualizationFragmentBakeAtlases`.

Concrete producer adapters live in **runtime extraction** under the planned umbrella module name `Extrinsic.Runtime.VisualizationAdapters`, mirroring the `Extrinsic.Runtime.SpatialDebugAdapters` pattern from [ADR-0008](0008-spatial-debug-visualizer-adapters.md).

Runtime is the only layer permitted to import **both** PropertySet / geometry algorithm authority **and** the graphics packet types. Geometry stays at the `geometry -> core` layer rule and graphics never imports geometry algorithm modules.

Editor / app code may own user-facing surfaces — selected attribute name, colormap selection, scalar range, isoline value count, vector-field scale / color, Htex regeneration request flag — but funnels them through the runtime adapter as pre-filter inputs rather than calling graphics-side packet builders directly.

Async visualization baking (Htex regeneration, isoline extraction, vector-field generation when expensive) remains **CPU / runtime-only** per the existing legacy-feature classification, scheduled through `Extrinsic.Runtime.StreamingExecutor` rather than on a render-graph pass.

### 2. Centralized validation in graphics; no runtime/upload filtering

Runtime / extraction performs **no** packet filtering: every packet authored by editor / algorithm code is submitted through `IRenderer::SubmitRuntimeSnapshots()`. Validation is centralized in graphics through:

```
Extrinsic.Graphics.VisualizationPackets::ValidateVisualizationPackets(...)
```

The renderer calls the validator at snapshot extraction time. Rejected records are dropped from the consumed `RenderWorld::Visualization` snapshot and counted in `VisualizationDiagnostics`:

- `MissingAttributeCount`.
- `DomainMismatchCount`.
- `InvalidRangeCount`.
- `UnsupportedColormapCount`.
- `InvalidResourceCount`.
- `MissingTexcoordCount`.
- `HtexRecreateRequestCount`.
- `TextureResidencyDeferredCount`.
- `HasErrors`.

**Future backend upload stages do not re-validate.** They consume only `RenderWorld::Visualization` as already-validated input. Editor / runtime tools surface the diagnostics counters for user-visible diagnosis but never replicate validation logic.

This mirrors the snapshot-record drain pattern from `GRAPHICS-002` / `GRAPHICS-010Q` (transient debug `InvalidSnapshotRecordCount`), prevents runtime / upload divergence, and keeps validation deterministic and CPU-testable in the default null path.

### 3. Backend-local upload for vector-field and isoline overlays

Vector-field glyphs and isoline polylines are **NOT** routed through the retained `GpuRender_Line` / `GpuRender_Point` cull buckets or the `Cull.Lines.*` / `Cull.Points.*` indirect resources. They are **NOT** GPU-scene renderable instances.

They are auxiliary draw resources owned by a backend-local upload helper under `src/graphics/vulkan`, mirroring:

- The transient debug expansion pattern from `GRAPHICS-007Q` / `GRAPHICS-010Q`.
- The ImGui overlay upload pattern from `GRAPHICS-013CQ`.

Per-frame host-visible (transient) GPU buffers are recycled each frame, never retained on `GpuWorld`, and never exposed through RHI or renderer module surfaces.

The helper expands the packet BDAs into per-frame transient buffers:

- `VectorFieldOverlayPacket::PositionBufferBDA` / `VectorBufferBDA` → per-frame transient vertex / index buffers (one glyph per element with backend-fixed glyph geometry).
- `IsolineOverlayPacket` source-scalar samples → per-frame transient line vertex / index buffers (one polyline per requested iso-value).

The expanded overlays are consumed by dedicated visualization-overlay passes that `LOAD` `SceneColorHDR` / `SceneDepth` next to `Pass.Forward.Line` / `Pass.Forward.Point`, expressing depth-tested vs always-on-top behavior as the same two-pipeline-variant policy resolved for transient debug primitives in `GRAPHICS-010Q` — **not** as separate cull buckets or new frame-recipe resources.

Concrete pipeline binding, the numbered pipeline-order step, and a future `VisualizationOverlayUploadDiagnostics` field analogous to `TransientDebugUploadDiagnostics` are tracked under `GRAPHICS-018` (Vulkan integration) scope and are not anticipated by this ADR.

Auxiliary GPU resources referenced through packet BDAs and the Htex / UV bake atlas textures are uploaded by the existing `Graphics.GpuAssetCache` / `RHI::BufferManager` / `RHI::TextureManager` paths once `GRAPHICS-015` texture/buffer residency lands; until then the CPU/null contract validates packet metadata only and `VisualizationDiagnostics::TextureResidencyDeferredCount` reports atlas descriptors whose texture residency is intentionally deferred.

### 4. UV-backed vs Htex-backed fragment bake mapping

Bake mapping selection is **runtime / editor-owned**. Editor UI exposes a per-fragment-bake selector that maps directly to the three values of `VisualizationFragmentBakeMapping`:

- `ExistingTexcoords`.
- `ExistingHtex`.
- `RecreateHtex`.

Default runtime-side policy:

1. Prefer `ExistingTexcoords` when `MeshHasTexcoords == true` and the user has not explicitly selected an Htex mapping.
2. Otherwise prefer `ExistingHtex` when an Htex atlas is already resident for the mesh.
3. Otherwise emit `RecreateHtex`.

`RecreateHtex` is an explicit user-driven request:

- Runtime extraction submits the `FragmentBakeAtlasPacket` with `Mapping = RecreateHtex` (and `MeshHasTexcoords` set to its true value, which may be `true` when the user explicitly requested Htex regeneration on a mesh that already has UVs).
- Graphics increments `VisualizationDiagnostics::HtexRecreateRequestCount` and accepts the descriptor **without** owning the Htex regeneration algorithm.
- Concrete Htex regeneration is scheduled by runtime / geometry on a background streaming task through `Extrinsic.Runtime.StreamingExecutor` (the existing classification of "async visualization baking" as CPU / runtime-only). The runtime / editor surface owns the dirty-domain stamp that triggers regeneration.
- Once regeneration completes, the next extraction frame submits the `FragmentBakeAtlasPacket` with `Mapping = ExistingHtex`.

UV-backed bakes require `MeshHasTexcoords = true` and a non-zero `TexcoordBufferBDA`. Missing or invalid texcoords increment `VisualizationDiagnostics::MissingTexcoordCount` and the packet is rejected from the consumed snapshot.

Concrete Htex / UV atlas texture residency, sampler binding, and per-fragment shader integration remain deferred to `GRAPHICS-015` (texture residency) and `GRAPHICS-018` (Vulkan integration) per the visualization packet contract's existing Non-goals.

### 5. Two-pipeline-variant policy citation

The depth-tested vs always-on-top behavior for the visualization overlay passes uses the **same two-pipeline-variant policy** that `GRAPHICS-010Q` resolved for transient debug primitives. There is no separate per-packet pipeline variant flag in `VisualizationPackets`; depth-test behavior is selected at pass-recording time exactly the way the transient debug passes select it.

This citation is recorded here so future readers do not introduce a third variant policy specific to visualization overlays.

## Consequences

Positive:

- The visualization packet contract is frozen; editor / runtime / geometry feature work cannot grow graphics fields under cover of an adapter change.
- Centralized graphics-side validation means runtime extraction never silently drops a record on the runtime side; every drop is visible through `VisualizationDiagnostics` counters and stays comparable across backends.
- Vector-field and isoline overlays do not contend with retained `GpuRender_Line` / `GpuRender_Point` cull buckets, so transient overlay traffic does not poison cull-bucket counts or force retained-resource churn.
- The fragment-bake mapping decision is editor / runtime policy; graphics accepts the descriptor and reports diagnostics without owning the regeneration algorithm.

Trade-offs and risks:

- The "no runtime filtering" rule means a buggy editor / algorithm can flood the graphics validator with invalid packets every frame. The diagnostics counters surface this immediately, but reviewers must reject editor code that "helpfully" filters before submitting — the validator must remain the single seam.
- The backend-local overlay upload helper is `src/graphics/vulkan`-local. Adding a future backend (e.g. promoted compute-only path, additional API backend) requires re-implementing the helper symmetrically; the architecture does not provide a portable upload abstraction here because doing so would have leaked transient-buffer lifetime through RHI, which the GRAPHICS-010Q / GRAPHICS-013CQ pattern explicitly rejects.
- The `Extrinsic.Runtime.VisualizationAdapters` umbrella is **planned** — no concrete module exists yet. The naming and module-placement decision in §1 must be respected when the umbrella lands; if a different name is chosen first, this ADR must be amended rather than silently superseded.
- The default fragment-bake selection policy in §4 hard-codes `ExistingTexcoords` as the preferred mapping when both UV and Htex are available. Editors that want Htex-by-default must surface that as an explicit user choice; runtime adapters must not silently invert the default.

Follow-up tasks required: none from this ADR. Concrete `Extrinsic.Runtime.VisualizationAdapters` module(s), `GRAPHICS-015` texture residency, and `GRAPHICS-018` Vulkan integration land under their own task IDs.

## Alternatives Considered

- **Runtime-side packet filtering.** Rejected per §2: would create two filtering seams (runtime + graphics) that can disagree silently and would force the diagnostics counters to be reconciled across layers.
- **Vector-field / isoline overlays routed through retained `GpuRender_Line` / `GpuRender_Point` cull buckets.** Rejected per §3: would conflate transient overlay traffic with retained scene draws, poison cull-bucket counts, and force retained-resource churn each frame.
- **Per-packet pipeline variant flag in `VisualizationPackets` for depth-tested vs always-on-top.** Rejected per §5: would introduce a third variant policy alongside the transient-debug one from `GRAPHICS-010Q`; the same two-pipeline-variant policy is reused at pass-recording time.
- **Graphics owns Htex regeneration.** Rejected per §4: Htex regeneration is a CPU / geometry algorithm scheduled through `Extrinsic.Runtime.StreamingExecutor`; graphics has no business owning a geometry algorithm or its dirty-domain stamp.
- **Runtime-side `VisualizationOverlayUploadDiagnostics` mirror.** Rejected per §3: diagnostics belong on the backend-local helper (under `GRAPHICS-018` scope) so they stay co-located with the transient buffer recycling counters; a runtime-side mirror would duplicate state without a clear owner.

## Validation

- [`tasks/done/GRAPHICS-014`](../../tasks/done/GRAPHICS-014-visualization-attributes-overlays.md) records the underlying `Extrinsic.Graphics.VisualizationPackets` packet contract, `ValidateVisualizationPackets(...)` validator, `VisualizationDiagnostics` field set, and overlay summary shapes.
- [`tasks/done/GRAPHICS-014Q`](../../tasks/done/GRAPHICS-014Q-visualization-runtime-backend-clarifications.md) records the four clarification decisions captured in §§1–4.
- [`tasks/done/GRAPHICS-010Q`](../../tasks/done/GRAPHICS-010Q-transient-debug-backend-clarifications.md) records the two-pipeline-variant policy reused by §5 and the transient-debug backend-local upload pattern referenced by §3.
- `docs/architecture/rendering-three-pass.md` carries the matching visualization attribute and overlay packet contract section authored by `GRAPHICS-014Q`.
- `src/graphics/renderer/README.md` carries the matching ownership-contract bullet next to the existing `Graphics.VisualizationPackets` entry.
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises `ValidateVisualizationPackets(...)`, the `VisualizationDiagnostics` counter set, and the snapshot-drop behavior without a Vulkan device.
