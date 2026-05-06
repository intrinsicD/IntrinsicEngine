# GRAPHICS-014Q — Visualization runtime/backend clarification follow-ups

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `GRAPHICS-013CQ` retirement cleared `tasks/active/`.
- Branch: `claude/agentic-workflow-session-T6CQd`.
- Promotion commit: `869f4b4` (move file from `tasks/backlog/rendering/` to `tasks/active/`, redirect rendering backlog README link).
- Implementation commit: pending — resolves decisions and syncs `docs/architecture/rendering-three-pass.md`, `docs/architecture/graphics.md`, and `src/graphics/renderer/README.md`.
- Task-state commit: pending retirement commit (will move the file from `tasks/active/` to `tasks/done/`).
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .` after docs sync lands.

## Decisions
- **Runtime ownership for visualization packet translation.** Runtime
  extraction (`Extrinsic.Runtime.RenderExtraction`) is the sole owner of
  translating PropertySet attributes, KMeans labels, isoline results,
  vector fields, and Htex metadata into the `RuntimeRenderSnapshotBatch`
  visualization packet spans (`VisualizationAttributeBuffers`,
  `VisualizationScalars`, `VisualizationColors`,
  `VisualizationVectorFields`, `VisualizationIsolines`,
  `VisualizationHtexAtlases`, `VisualizationFragmentBakeAtlases`).
  Concrete producer adapters that translate `Geometry::PropertySet`
  scalar/color/vector attributes, KMeans label outputs, isoline
  extractor results, vector-field outputs, and Htex patch metadata
  live in **runtime extraction** (planned umbrella module name
  `Extrinsic.Runtime.VisualizationAdapters`), mirroring the
  `Extrinsic.Runtime.SpatialDebugAdapters` pattern resolved by
  `GRAPHICS-011Q`. Runtime is the only layer permitted to import both
  PropertySet/geometry algorithm authority and the graphics packet
  types; geometry stays at the `geometry -> core` layer rule and
  graphics never imports geometry algorithm modules. Editor/app code
  may own user-facing surfaces (selected attribute name, colormap
  selection, scalar range, isoline value count, vector-field
  scale/color, Htex regeneration request flag) but funnels them
  through the runtime adapter as pre-filter inputs rather than
  calling graphics-side packet builders directly. Async visualization
  baking (Htex regeneration, isoline extraction, vector-field
  generation when expensive) remains CPU/runtime-only per the
  existing legacy-feature classification, scheduled through
  `Extrinsic.Runtime.StreamingExecutor` rather than on a render-graph
  pass.
- **Invalid-packet handling.** Runtime/extraction performs no packet
  filtering: every packet authored by editor/algorithm code is
  submitted through `IRenderer::SubmitRuntimeSnapshots()`. Validation
  is centralized in graphics through
  `Extrinsic.Graphics.VisualizationPackets::ValidateVisualizationPackets(...)`
  called by the renderer at snapshot extraction time. Rejected
  records are dropped from the consumed `RenderWorld::Visualization`
  snapshot and counted in `VisualizationDiagnostics`
  (`MissingAttributeCount`, `DomainMismatchCount`,
  `InvalidRangeCount`, `UnsupportedColormapCount`,
  `InvalidResourceCount`, `MissingTexcoordCount`,
  `HtexRecreateRequestCount`, `TextureResidencyDeferredCount`,
  `HasErrors`). Future backend upload stages do not re-validate; they
  consume only `RenderWorld::Visualization` as already-validated
  input. Editor/runtime tools surface the diagnostics counters for
  user-visible diagnosis but never replicate validation logic. This
  mirrors the snapshot-record drain pattern from
  `GRAPHICS-002`/`GRAPHICS-010Q` (transient debug
  `InvalidSnapshotRecordCount`), prevents runtime/upload divergence,
  and keeps validation deterministic and CPU-testable in the default
  null path.
- **Backend upload strategy for vector-field/isoline overlays.**
  Vector-field glyphs and isoline polylines are NOT routed through
  the retained `GpuRender_Line`/`GpuRender_Point` cull buckets or the
  `Cull.Lines.*`/`Cull.Points.*` indirect resources, and they are
  NOT GPU-scene renderable instances. They are auxiliary draw
  resources owned by a backend-local upload helper under
  `src/graphics/vulkan`, mirroring the transient debug expansion
  pattern from `GRAPHICS-007Q`/`GRAPHICS-010Q` and the ImGui overlay
  upload pattern from `GRAPHICS-013CQ`: per-frame host-visible
  (transient) GPU buffers recycled each frame, never retained on
  `GpuWorld`, and never exposed through RHI or renderer module
  surfaces. The helper expands
  `VectorFieldOverlayPacket::PositionBufferBDA`/`VectorBufferBDA`
  into per-frame transient vertex/index buffers (one glyph per
  element with backend-fixed glyph geometry) and
  `IsolineOverlayPacket` source-scalar samples into per-frame
  transient line vertex/index buffers (one polyline per requested
  iso-value). The expanded overlays are consumed by dedicated
  visualization-overlay passes that `LOAD` `SceneColorHDR`/
  `SceneDepth` next to `Pass.Forward.Line`/`Pass.Forward.Point`,
  expressing depth-tested vs always-on-top behavior as the same
  two-pipeline-variant policy resolved for transient debug primitives
  in `GRAPHICS-010Q` rather than as separate cull buckets or new
  frame-recipe resources. Concrete pipeline binding, the numbered
  pipeline-order step, and a future
  `VisualizationOverlayUploadDiagnostics` field analogous to
  `TransientDebugUploadDiagnostics` are tracked under `GRAPHICS-018`
  Vulkan integration scope and are not anticipated by this
  clarification. Auxiliary GPU resources referenced through packet
  BDAs and the Htex/UV bake atlas textures are uploaded by the
  existing `Graphics.GpuAssetCache`/`RHI::BufferManager`/
  `RHI::TextureManager` paths once `GRAPHICS-015` texture/buffer
  residency lands; until then the CPU/null contract validates packet
  metadata only and `VisualizationDiagnostics::TextureResidencyDeferredCount`
  reports atlas descriptors whose texture residency is intentionally
  deferred.
- **UV-backed versus Htex-backed fragment bake selection policy.**
  Bake mapping selection is runtime/editor-owned. Editor UI exposes
  a per-fragment-bake selector that maps directly to the three
  values of `VisualizationFragmentBakeMapping`: `ExistingTexcoords`,
  `ExistingHtex`, `RecreateHtex`. The default runtime-side policy is:
  prefer `ExistingTexcoords` when `MeshHasTexcoords == true` and the
  user has not explicitly selected an Htex mapping; otherwise prefer
  `ExistingHtex` when an Htex atlas is already resident for the mesh;
  otherwise emit `RecreateHtex`. `RecreateHtex` is an explicit
  user-driven request — runtime extraction submits the
  `FragmentBakeAtlasPacket` with `Mapping = RecreateHtex` (and
  `MeshHasTexcoords` set to its true value, which may be `true` when
  the user explicitly requested Htex regeneration on a mesh that
  already has UVs). Graphics increments
  `VisualizationDiagnostics::HtexRecreateRequestCount` and accepts
  the descriptor without owning the Htex regeneration algorithm.
  Concrete Htex regeneration is scheduled by runtime/geometry on a
  background streaming task through `Extrinsic.Runtime.StreamingExecutor`
  (the existing classification of "async visualization baking" as
  CPU/runtime-only); the runtime/editor surface owns the dirty-domain
  stamp that triggers regeneration, and once regeneration completes
  the next extraction frame submits the `FragmentBakeAtlasPacket`
  with `Mapping = ExistingHtex`. UV-backed bakes require
  `MeshHasTexcoords = true` and a non-zero `TexcoordBufferBDA`;
  missing or invalid texcoords increment
  `VisualizationDiagnostics::MissingTexcoordCount` and the packet is
  rejected from the consumed snapshot. Concrete Htex/UV atlas texture
  residency, sampler binding, and per-fragment shader integration
  remain deferred to `GRAPHICS-015` (texture residency) and
  `GRAPHICS-018` (Vulkan integration) per the visualization packet
  contract's existing Non-goals.

## Resolution
- Decisions recorded above and consequential notes synced into
  `docs/architecture/rendering-three-pass.md` (visualization
  attribute and overlay packet contract section, "Per `GRAPHICS-014Q`"
  paragraph), `docs/architecture/graphics.md`
  (`Extrinsic.Graphics.VisualizationPackets` ownership bullet
  extension under the GPU scene ownership block), and
  `src/graphics/renderer/README.md` (matching ownership-contract
  bullet next to the existing `Graphics.VisualizationPackets`
  entry). The rendering backlog README entry for `GRAPHICS-014Q` is
  redirected from the `tasks/backlog/rendering/` path to the
  `tasks/active/` path by the promotion commit and will be
  redirected to `tasks/done/` by the retirement commit.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

## Goal
- Clarify runtime, geometry, and backend details that remain after the CPU/null `GRAPHICS-014` visualization packet and snapshot contracts.

## Non-goals
- No C++ behavior changes.
- No GPU texture residency implementation; that belongs to `GRAPHICS-015`.
- No editor widget or importer/exporter work.

## Context
- `GRAPHICS-014` established data-only visualization packets, UV-vs-Htex fragment bake mapping policy, diagnostics, overlay summaries, and renderer-owned `RenderWorld::Visualization` snapshot spans.
- Remaining questions affect runtime/geometry producers, concrete GPU upload, and backend shader/resource binding.

## Required changes
- Clarify runtime ownership for translating PropertySet attributes, KMeans labels, isoline results, vector fields, and Htex metadata into `RuntimeRenderSnapshotBatch` visualization packet spans.
- Clarify whether invalid visualization packets are dropped by runtime producers, filtered by future upload stages, or surfaced as diagnostics-only records in tooling.
- Clarify backend upload strategy for vector-field/isoline overlays after texture residency lands, including line/point bucket expansion versus auxiliary draw resources.
- Clarify UV-backed versus Htex-backed fragment bake selection policy in UI/runtime terms, including when user-requested Htex regeneration is scheduled.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update rendering architecture, graphics architecture, and runtime extraction docs with chosen producer/upload responsibilities.

## Acceptance criteria
- Runtime/geometry/backend work can implement concrete visualization uploads without changing the CPU/null graphics contracts from `GRAPHICS-014`.
- Graphics remains free of live ECS, geometry algorithm, and editor widget ownership.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Implementing texture/atlas residency outside `GRAPHICS-015`.
- Depending on legacy visualization managers.

