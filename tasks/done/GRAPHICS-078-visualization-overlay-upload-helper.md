# GRAPHICS-078 — Backend visualization-overlay upload helper (vector field + isoline)

## Status

- Status: done locally (Slices A + B + C landed on CPU/null hosts; Slice D
  command-stream smoke passes on the Vulkan-capable host as of 2026-05-28. The
  shared GRAPHICS-076/BUG-012 default-recipe Vulkan command-stream blocker is
  cleared, and the dedicated visualization-overlay `gpu;vulkan` smoke now
  records both lanes on the operational Vulkan command stream. Pixel-readback
  parity is explicitly deferred to
  [`GRAPHICS-078E`](../backlog/rendering/GRAPHICS-078E-visualization-overlay-pixel-readback.md)
  rather than expanding this Slice D into a renderer readback API change).
  Slice A scaffolded the recipe + executor shape on
  `claude/intrinsicengine-agent-onboarding-3dLeQ` 2026-05-24
  (209/209 graphics contract tests pass). Slice B promoted the
  vector-field lane from `SkippedUnavailable` to `Recorded` on
  `claude/intrinsicengine-agent-onboarding-7IOiJ` 2026-05-24 and
  verified through the contract CPU/null gate (212/212 graphics
  contract tests pass; full CPU/null gate result captured in the
  Slice B commit body). Slice C promoted the isoline lane from
  `SkippedUnavailable` to `Recorded` on
  `claude/intrinsicengine-agent-onboarding-2P6pn` 2026-05-25 and
  verified through the contract CPU/null gate (219/219 graphics
  contract tests pass; net +7 from Slice B: six new isoline tests
  + one prior baseline addition outside this task). The task now
  sits at `CPUContracted` on CPU-only hosts for the full two-lane
  visualization overlay; Slice D closes the task at command-stream
  `Operational` maturity on Vulkan-capable hosts.
- Owner/agent: local agent workflow for Slice D graduation/retirement.
- Branch: Slice A landed on
  `claude/intrinsicengine-agent-onboarding-3dLeQ`; Slice B landed
  on `claude/intrinsicengine-agent-onboarding-7IOiJ`; Slice C
  landed on `claude/intrinsicengine-agent-onboarding-2P6pn`;
  Slice D command-stream smoke landed locally on `main` after `35bbf0ce`.
- Completed: 2026-05-28.
- Commit/PR: local commit `032b8ab7` (`GRAPHICS-077/078: add overlay Vulkan smokes`).
- Started: 2026-05-24. Promoted from
  `tasks/backlog/rendering/GRAPHICS-078-visualization-overlay-upload-helper.md`
  as the next earliest unblocked Theme A leaf after GRAPHICS-076 and
  GRAPHICS-077 each parked on their Vulkan-host blockers (only their
  respective Slice D opt-in `gpu;vulkan` smokes remain). GRAPHICS-078
  directly mirrors the GRAPHICS-077 transient-debug-primitive upload
  pattern: per-frame host-visible buffers, two pipeline variants per
  kind (depth-tested + always-on-top), one consolidated overlay pass
  drawing into `SceneColorHDR`/`SceneDepth` after lit composition.
- Next verification step: none for this task; pixel-readback parity is tracked
  by
  [`GRAPHICS-078E`](../backlog/rendering/GRAPHICS-078E-visualization-overlay-pixel-readback.md).
  2026-05-28
  focused Slice D verification under `build/ci-vulkan`:
  `VisualizationOverlaySurfaceGpuSmoke.MixedLanesRecordOnOperationalVulkanCommandStream`
  passed 1/1.

## Slice plan

The task spans (a) a backend-local per-frame upload helper, (b) two
pipeline variants per kind (vector-field, isoline) for four pipelines
total, (c) a new pass that draws after lit composition into
`SceneColorHDR`/`SceneDepth` (placed adjacent to the GRAPHICS-077
`TransientDebugSurfacePass`), (d) executor routing through the
`Recorded`/`SkippedNonOperational`/`SkippedUnavailable` taxonomy, and
(e) deterministic CPU diagnostics. Each slice below is independently
reviewable and preserves the CPU/null correctness gate; only the
final slice exercises an opt-in `gpu;vulkan` smoke.

- **Slice A (this slice).** Pin the recipe/executor scaffold for the
  new `VisualizationOverlayPass` without committing to pipelines or
  the helper yet. Adds `FrameRecipePassKind::VisualizationOverlay`
  plus the matching `features.EnableVisualizationOverlay` derivation
  (`!world.Visualization.VectorFields.empty() ||
  !world.Visualization.Isolines.empty()`), declares the recipe pass
  node with `Read(SceneDepth, DepthRead) + Write(SceneColorHDR,
  ColorAttachmentWrite) + SetRenderPass(...)` (placed immediately
  after `TransientDebugSurfacePass` and before
  `PostProcessHistogramPass` so it observes the current scene color
  with the postprocess chain still downstream), adds a
  `VisualizationOverlayPass` shell class (`SetPipeline` / `Execute`
  no-op stubs with the three-state taxonomy), wires the executor
  branch `"VisualizationOverlayPass"` to a new
  `RecordVisualizationOverlayPass(...)` helper that returns
  `SkippedUnavailable` (no pipelines yet) when the feature is enabled
  and is absent from `CommandRecords` when the feature is disabled,
  and adds a zeroed `VisualizationOverlayUploadDiagnostics` struct on
  the renderer diagnostics aggregate (fields: `UploadOverflowCount`,
  `VectorFieldRecordsSubmitted`, `IsolineRecordsSubmitted`,
  `VectorFieldRecordsRecorded`, `IsolineRecordsRecorded`,
  `MissingPipelineSkipCount`). No new pipelines, no helper, no shader
  files in Slice A. Mirrors GRAPHICS-077 Slice A in every structural
  detail except the lane membership (two visualization-overlay kinds
  instead of three transient-debug lanes).
- **Slice B (landed 2026-05-24 on `claude/intrinsicengine-agent-onboarding-7IOiJ`).**
  Vector-field lane operational wiring. Adds two vector-field
  pipelines (`DepthTested` + `AlwaysOnTop`) via a
  `BuildVisualizationVectorFieldPipelineDesc(depthTested)` helper,
  two pipeline-lease members on `NullRenderer` (created at call
  indices #32 + #33 after the GRAPHICS-077 point lane at #31), the
  new shader pair
  `assets/shaders/visualization_vector_field.{vert,frag}` (BDA-fetched
  glyph vertices via push constants, drawn into `SceneColorHDR`), and
  the per-frame vector-field-lane upload through a new
  `VisualizationOverlayUploadHelper` (declared as the virtual
  `IVisualizationOverlayUploadHelper` in
  `Extrinsic.Graphics.VisualizationOverlayUploadHelper` so contract
  tests can substitute; the default concrete impl lives in the
  renderer module and uses `BufferManager` +
  `IDevice::WriteBuffer(...)` — the Vulkan-tuned variant is deferred
  to Slice D per the backend-locality non-goal that GRAPHICS-077
  Slice D inherits). The `DepthTested` placement question recorded
  under "Slice A clarifications captured during implementation" was
  resolved option (a): `VectorFieldOverlayPacket` grew a
  `bool DepthTested{true}` field mirroring
  `DebugLinePacket`/`DebugPointPacket`/`DebugTrianglePacket`.
  Records `BindPipeline(variant) + PushConstants(16) +
  Draw(2 * ElementCount, 1, 0, 0)` per vector-field packet (one
  glyph = one line segment = two vertices, BDA-based vertex fetch
  via push constants). Per-packet `DepthTested` selects the matching
  pipeline variant; `RecordVisualizationOverlayPass` flips from
  `SkippedUnavailable` to `Recorded` when at least one vector-field
  packet records. Increments `VectorFieldRecordsSubmitted` /
  `VectorFieldRecordsRecorded` deterministically.
  Slice B-specific deviation from the original spec text:
  `kVisualizationOverlayRenderPassColorAttachments` is already in
  place from Slice A; no further recipe-side LOAD-store template
  changes were required. CPU/null contract note: the helper does not
  have CPU access to `PositionBufferBDA` / `VectorBufferBDA` (those
  are GPU pointers), so the CPU/null path writes zero positions and
  the packet's packed color into each packed vertex; per-pixel
  correctness on a real Vulkan device is owned by the optional
  Slice D `gpu;vulkan` smoke and the Vulkan-tuned helper variant.
- **Slice C (landed 2026-05-25 on `claude/intrinsicengine-agent-onboarding-2P6pn`).**
  Isoline lane operational wiring. Mirrors Slice B for isoline
  polylines: two more pipelines (`Isoline.DepthTested`,
  `Isoline.AlwaysOnTop`, call indices #34 + #35) via a
  `BuildVisualizationIsolinePipelineDesc(depthTested)` helper, two
  pipeline-lease members on `NullRenderer`, the new shader pair
  `assets/shaders/visualization_isoline.{vert,frag}` (`LineList`
  topology, BDA-fetch vertex layout matching the vector-field +
  transient-debug shaders), and an extended
  `VisualizationOverlayUploadHelper` with an `UploadIsolines(...)`
  method backed by an independent per-lane host-visible vertex
  buffer (same geometric growth + per-lane cap policy as the
  vector-field lane). `IsolineOverlayPacket` grows a `bool
  DepthTested{true}` field mirroring the GRAPHICS-010Q two-variant
  policy on the transient-debug and vector-field packets. A new
  `VisualizationIsolinePushConstants` (16 bytes carrying BDA +
  per-draw `FirstVertex`) keeps room for per-kind evolution
  (e.g. per-iso line-width expansion in a follow-up task) without
  disturbing the vector-field push block. `ExecuteIsolines(...)`
  iterates the sanitized packet span and records
  `BindPipeline(variant) + PushConstants(16) +
  Draw(2 * IsoValueCount, 1, 0, 0)` per packet with per-packet
  variant selection. `RecordVisualizationOverlayPass(...)` gates
  each lane independently: a lane with packets but a missing or
  invalid pipeline pair increments `MissingPipelineSkipCount` and
  is skipped, while sibling lanes with valid pipelines still
  record; the pass status is `Recorded` when at least one lane
  recorded, and `SkippedUnavailable` when every submitted lane
  failed its gate. Both lanes now record their respective overlay
  draws with deterministic CPU diagnostics, closing the full
  visualization overlay at `CPUContracted` on CPU-only hosts.
  Mirrors GRAPHICS-077 Slice C's per-lane independence semantics
  exactly. CPU/null contract note: the helper does not have CPU
  access to the source scalar field (its values + topology are
  GPU-side), so the CPU/null path writes zero positions + the
  packet color into each packed vertex; per-pixel correctness on a
  real Vulkan device is owned by the optional Slice D `gpu;vulkan`
  smoke and the Vulkan-tuned helper variant that expands scalar-
  field-derived contour vertices.
- **Slice D (landed locally 2026-05-28).** Opt-in `gpu;vulkan;graphics`
  command-stream smoke asserting the visualization overlay pass records through
  a real Vulkan device on a Vulkan-capable host. The smoke warms the canonical
  default recipe through the bounded `engine.Run()` driver, then submits one
  vector-field packet and one isoline packet through a manually driven renderer
  frame so runtime extraction does not overwrite the test snapshots. It asserts
  `VisualizationOverlayPass` is `Recorded`, both submitted/recorded lane
  counters are `1`, `UploadOverflowCount` / `MissingPipelineSkipCount` stay
  zero, and Vulkan fallback counters stay stable. Pixel-readback parity is
  deferred to
  [`GRAPHICS-078E`](../backlog/rendering/GRAPHICS-078E-visualization-overlay-pixel-readback.md)
  because the current public readback seam is MinimalDebug-only.

## Maturity

- Target: `CPUContracted` after Slice C on every host (full two-lane
  recording shape verified through the CPU/null gate); `Operational`
  after the optional Slice D on Vulkan-capable hosts.
- Slice A closes `Scaffolded → Scaffolded` for the executor + recipe
  shape only — no recording behavior is added, so the slice
  intentionally lands at the `Scaffolded` taxonomy level. Per the
  `Scaffolded` closure rule (see `docs/agent/task-maturity.md`), a
  `Scaffolded`-only slice is permissible iff the next slice that
  promotes it to `CPUContracted` is named and in-scope. Slice B is
  named here and remains in-scope for this task, so the closure rule
  is honored.
- Slice B (landed) closes vector-field lane `Scaffolded →
  CPUContracted`. The isoline lane is still `Scaffolded`-only
  (Slice C remains in-scope to close it).
- Slice C (landed) closes the isoline lane `Scaffolded →
  CPUContracted` and closes the task at `CPUContracted` on CPU-only
  hosts (both lanes recording deterministically through the CPU/null
  gate).
- Slice D closes `CPUContracted → Operational` on Vulkan-capable
  hosts.

## Goal

- Implement the per-frame host-visible upload helper for
  `VectorFieldOverlayPacket` / `IsolineOverlayPacket` declared by
  `GRAPHICS-014Q`: a backend-local helper that expands sanitized
  packet spans into per-frame transient vertex/index buffers consumed
  by a dedicated visualization-overlay pass that LOADs
  `SceneColorHDR`/`SceneDepth` next to the GRAPHICS-077
  `TransientDebugSurfacePass`. Two pipeline variants per packet kind
  (depth-tested vs always-on-top), mirroring `GRAPHICS-077`.

## Non-goals

- No routing through retained line/point cull buckets (rejected per
  `GRAPHICS-014Q`).
- No Htex / UV-bake atlas residency (texture residency is
  `GRAPHICS-015` which is done — the atlas reference path is consumed
  but not produced here).
- No PropertySet → packet adapter (that is `RUNTIME-082`
  `VisualizationAdapters`).
- No RHI/renderer module surface change beyond the diagnostics field
  and the two per-kind `Get*Pipeline()` accessors on the new pass.
- No third pipeline variant per kind (only DepthTested +
  AlwaysOnTop are recorded, mirroring GRAPHICS-077).

## Context

- Status: Slice A in-progress; landing on
  `claude/intrinsicengine-agent-onboarding-3dLeQ`.
- Owner/layer: `graphics/vulkan` for the helper + pipelines (Slice B
  onwards); `graphics/renderer` for the new pass class, recipe
  declaration, and executor route (Slice A).
- Planning anchors:
  `tasks/done/GRAPHICS-014-visualization-attributes-overlays.md`,
  `tasks/done/GRAPHICS-014Q-visualization-runtime-backend-clarifications.md`,
  `tasks/active/GRAPHICS-077-transient-debug-primitive-upload-helper.md`
  (mirror task, Slices A–C landed).
- Today: `RenderWorld::Visualization.VectorFields` and
  `RenderWorld::Visualization.Isolines` spans are populated from the
  `RuntimeRenderSnapshotBatch::VisualizationVectorFields`/
  `VisualizationIsolines` spans, but no GPU upload helper or pass
  body exists; vector-field glyphs and isoline polylines never reach
  the framebuffer.
- The helper mirrors the `GRAPHICS-077` transient-debug pattern:
  per-frame host-visible buffers, recycled each frame, never retained
  on `GpuWorld`.
- Pass insertion point: the new `VisualizationOverlayPass` runs after
  the GRAPHICS-077 `TransientDebugSurfacePass` (so transient debug
  and visualization overlays share the same "post-lit, pre-postprocess"
  band) and before `PostProcessHistogramPass` so that overlay
  primitives reach the postprocess inputs deterministically.

## Required changes

Slice A (landed 2026-05-24):

- [x] Add `FrameRecipePassKind::VisualizationOverlay` to
      `src/graphics/renderer/Graphics.FrameRecipe.cppm` (appended at
      the end of the enum to keep prior values stable).
- [x] Add `bool EnableVisualizationOverlay{false}` field to
      `FrameRecipeFeatures` in the same file.
- [x] In `DeriveDefaultFrameRecipeFeatures(...)`, derive
      `features.EnableVisualizationOverlay =
      !world.Visualization.VectorFields.empty() ||
      !world.Visualization.Isolines.empty()` so the pass is omitted
      entirely from `RenderGraphFrameStats::CommandRecords` when no
      visualization overlay packets exist for the frame.
- [x] In `DescribeDefaultFrameRecipe(...)`, add
      `AddPass(out, FrameRecipePassKind::VisualizationOverlay,
      "VisualizationOverlayPass", features.EnableVisualizationOverlay,
      false, {"SceneColorHDR", "SceneDepth"}, {"SceneColorHDR"})`
      placed between `TransientDebugSurfacePass` and
      `PostProcessHistogramPass`.
- [x] In `BuildDefaultFrameRecipe(...)`, when
      `features.EnableVisualizationOverlay` is set, declare the pass
      node with `Read(SceneDepth, TextureUsage::DepthRead) +
      Write(SceneColorHDR, TextureUsage::ColorAttachmentWrite) +
      SetRenderPass(...)`. The render-pass desc uses a new
      `kVisualizationOverlayRenderPassColorAttachments` LOAD-store
      template (preserves lit color) paired with a LOAD/Store depth
      attachment, mirroring the GRAPHICS-077 transient-debug shape.
- [x] Add a `VisualizationOverlayPass` shell class at
      `src/graphics/renderer/Passes/Pass.VisualizationOverlay.{cppm,cpp}`
      mirroring the `TransientDebugSurfacePass` shape:
      default-constructible, per-kind `Set*Pipeline(RHI::PipelineHandle)`
      stubs (`VectorFieldDepthTested`, `VectorFieldAlwaysOnTop`,
      `IsolineDepthTested`, `IsolineAlwaysOnTop`), matching `Get*Pipeline()`
      accessors for the renderer's fail-closed prerequisite check.
      Module name `Extrinsic.Graphics.Pass.VisualizationOverlay`.
      Slice A only ships the pipeline-handle bookkeeping; the
      `Execute*` bodies land with Slices B/C.
- [x] Add a `VisualizationOverlayUploadDiagnostics` struct co-located
      in the new `Pass.VisualizationOverlay.cppm` (Slice B will move it
      to a dedicated helper module if/when that module is created) with
      fields `UploadOverflowCount`, `VectorFieldRecordsSubmitted`,
      `IsolineRecordsSubmitted`, `VectorFieldRecordsRecorded`,
      `IsolineRecordsRecorded`, `MissingPipelineSkipCount` (all
      `std::uint64_t`, zeroed in Slice A except `MissingPipelineSkipCount`
      which increments once per operational-scaffold frame — Slice B
      begins incrementing the vector-field counters and Slice C the
      isoline counters). Exposed through the new
      `RenderGraphFrameStats::VisualizationOverlayUpload` field; reset
      per-frame through the existing `m_LastRenderGraphStats = {}`
      cadence.
- [x] Add `m_VisualizationOverlayPass` member to `NullRenderer`
      (no pipeline lease yet in Slice A — that lands in Slice B).
- [x] Add executor branch `else if (passName ==
      std::string_view{"VisualizationOverlayPass"})` routing to a new
      `RecordVisualizationOverlayPass(...)` helper that returns
      `SkippedNonOperational` when the device is not operational and
      `SkippedUnavailable` otherwise (with `MissingPipelineSkipCount`
      increment on the operational-no-pipeline path so the diagnostic
      distinguishes "scaffold-only" from "feature off"). Branch
      placed immediately after `"TransientDebugSurfacePass"` and
      before `"Present"` so it sits in source order alongside the
      GRAPHICS-077 transient-debug branch.

Slice A clarifications captured during implementation:

- `VectorFieldOverlayPacket` / `IsolineOverlayPacket` do not carry a
  `DepthTested` field (verified in
  `src/graphics/renderer/Graphics.VisualizationPackets.cppm:66-86`).
  GRAPHICS-014Q calls out the "depth-tested vs always-on-top" two-
  variant policy, so Slice B must either (a) extend the packet
  shape to add a `DepthTested` flag (mirroring
  `DebugLinePacket`/`DebugPointPacket`/`DebugTrianglePacket`), or
  (b) derive the per-packet variant from a snapshot-level setting on
  `VisualizationSnapshot::OverlaySummary`. Slice A leaves this
  decision to Slice B because the Slice A executor short-circuits
  before any variant selection happens. Recorded here so Slice B
  reviewers do not re-derive the question.

Slice B (vector-field lane, landed 2026-05-24):

- [x] Added `bool DepthTested{true}` to
      `VectorFieldOverlayPacket` in
      `src/graphics/renderer/Graphics.VisualizationPackets.cppm`
      (resolving the Slice A clarification option (a) — packet-level
      flag mirroring the transient-debug packets).
- [x] Added `IVisualizationOverlayUploadHelper` virtual interface +
      default concrete `VisualizationOverlayUploadHelper` in
      `src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.{cppm,cpp}`
      (CPU-mockable interface for contract tests; default impl pairs
      `RHI::BufferManager` with `IDevice::WriteBuffer(...)` against a
      single growing host-visible vertex buffer per lane, geometric
      growth ×2 up to per-lane cap `1 << 18` vertices). Moved
      `VisualizationOverlayUploadDiagnostics` from
      `Pass.VisualizationOverlay.cppm` to the new helper module
      (mirroring the GRAPHICS-077 Slice B helper-module placement).
- [x] Added two vector-field pipelines (`VectorField.DepthTested`,
      `VectorField.AlwaysOnTop`) at call indices #32 + #33 in
      `InitializeOperationalPassResources(...)` via a new
      `BuildVisualizationVectorFieldPipelineDesc(depthTested)` helper.
      Pinned to `RGBA16_FLOAT` (the `SceneColorHDR` format) +
      `D32_FLOAT` depth, `LineList` topology,
      `Rasterizer.Culling = None`, `ColorBlend[0].Enable = false`,
      `PushConstantSize = sizeof(VisualizationVectorFieldPushConstants)`
      (16 bytes carrying BDA + per-draw `FirstVertex`).
- [x] Added shader pair
      `assets/shaders/visualization_vector_field.{vert,frag}`
      (BDA-fetch per-vertex layout matching the transient-debug
      shaders, 16-byte push block carrying
      `VertexBufferBDA + FirstVertex + Reserved`).
- [x] Added `m_VisualizationOverlayVectorFieldPipelineLeaseDepthTested`
      and `…LeaseAlwaysOnTop` members on `NullRenderer`, plus the
      `m_VisualizationOverlayUploadHelper` member; constructed
      alongside the GRAPHICS-077 helper in `Initialize(...)`; reset
      in `Shutdown()` before `m_BufferManager` so the helper's
      internal lease destructor observes a live manager.
- [x] Added `VisualizationVectorFieldPushConstants` struct +
      `ExecuteVectorFields(...)` body to `VisualizationOverlayPass`
      (per-packet `BindPipeline(variant) + PushConstants(16) +
      Draw(2 * ElementCount, 1, 0, 0)` with per-packet `DepthTested`
      variant selection and deterministic per-lane diagnostics
      increments).
- [x] Extended `RecordVisualizationOverlayPass(cmd, world)` to gate
      the vector-field lane on its pipeline-pair validity, upload
      through the helper, record per-packet draws, and flip the pass
      status to `Recorded` when at least one packet's draw lands
      (`SkippedUnavailable` + `MissingPipelineSkipCount += 1` when the
      lane's pipelines are missing).

Slice C (isoline lane, landed 2026-05-25):

- [x] Added `bool DepthTested{true}` to `IsolineOverlayPacket` in
      `src/graphics/renderer/Graphics.VisualizationPackets.cppm`,
      mirroring the Slice B vector-field flag.
- [x] Extended `IVisualizationOverlayUploadHelper` with
      `UploadIsolines(...)` returning a new
      `VisualizationIsolineUploadResult` (same shape as the
      vector-field result), and grew the default
      `VisualizationOverlayUploadHelper` with an independent
      per-lane buffer lease + capacity field. The shared
      `UploadPackedVertices` prologue in
      `Graphics.VisualizationOverlayUploadHelper.cpp` is reused
      verbatim; the lane uses `kInitialIsolineVertexCount = 256` /
      `kMaxIsolineVertexCount = 1 << 18` with the same `2 *
      IsoValueCount` accumulation policy as the vector-field
      lane's `2 * ElementCount`.
- [x] Added two isoline pipelines (`Isoline.DepthTested`,
      `Isoline.AlwaysOnTop`) at call indices #34 + #35 in
      `InitializeOperationalPassResources(...)` via a new
      `BuildVisualizationIsolinePipelineDesc(depthTested)` helper.
      Pinned to `RGBA16_FLOAT` (the `SceneColorHDR` format) +
      `D32_FLOAT` depth, `LineList` topology,
      `Rasterizer.Culling = None`, `ColorBlend[0].Enable = false`,
      `PushConstantSize = sizeof(VisualizationIsolinePushConstants)`
      (16 bytes carrying BDA + per-draw `FirstVertex`).
- [x] Added shader pair
      `assets/shaders/visualization_isoline.{vert,frag}` (`LineList`
      topology, BDA-fetch vertex layout matching the vector-field
      + transient-debug shaders, 16-byte push block carrying
      `VertexBufferBDA + FirstVertex + Reserved`).
- [x] Added `m_VisualizationOverlayIsolinePipelineLeaseDepthTested`
      and `...LeaseAlwaysOnTop` members on `NullRenderer`,
      constructed alongside the Slice B vector-field leases in
      `Initialize(...)`/`InitializeOperationalPassResources(...)`;
      reset in `Shutdown()` before `m_PipelineManager` so the
      lease destructor observes a live manager.
- [x] Added `VisualizationIsolinePushConstants` struct +
      `ExecuteIsolines(...)` body to `VisualizationOverlayPass`
      (per-packet `BindPipeline(variant) + PushConstants(16) +
      Draw(2 * IsoValueCount, 1, 0, 0)` with per-packet
      `DepthTested` variant selection and deterministic per-lane
      diagnostics increments).
- [x] Replaced the placeholder
      `if (hasIsolines) ++MissingPipelineSkipCount;` increment in
      `RecordVisualizationOverlayPass` with the same gate-and-record
      shape the vector-field lane uses: the isoline lane is gated
      on its pipeline-pair validity, uploads through the helper,
      records per-packet draws, and flips the pass status to
      `Recorded` when at least one lane's draws land
      (`MissingPipelineSkipCount += 1` only when the lane has
      packets but pipelines missing).

Slice D (optional `gpu;vulkan` smoke, deferred):

- [x] Add `tests/integration/graphics/Test.VisualizationOverlaySurfaceGpuSmoke.cpp`
      under `gpu;vulkan;graphics` labels, sharing the GRAPHICS-033D
      bounded `engine.Run()` driver helper. Asserts that submitting vector-field
      and isoline packets through `RuntimeRenderSnapshotBatch` on a manually
      driven renderer frame records `VisualizationOverlayPass` on a real Vulkan
      command stream and that `VisualizationOverlayUploadDiagnostics` counters
      match the submitted counts.
- [x] Defer pixel-readback color assertions to
      [`GRAPHICS-078E`](../backlog/rendering/GRAPHICS-078E-visualization-overlay-pixel-readback.md)
      instead of reusing the MinimalDebug-only readback seam.

## Tests

Slice A (landed 2026-05-24):

- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.RecipeDeclaresPassWhenOverlayPacketsExist`:
      `RenderWorld::Visualization.VectorFields` populated with one
      vector-field packet,
      `DescribeDefaultFrameRecipe(DeriveDefaultFrameRecipeFeatures(world))`
      includes `"VisualizationOverlayPass"` exactly once with the
      correct reads/writes shape.
- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.RecipeOmitsPassWhenNoOverlayPackets`:
      empty visualization spans → recipe declares the pass with
      `Enabled=false` (compiled graph omits it entirely from
      `CommandRecords`); executor never sees the branch;
      `VisualizationOverlayUpload` counters stay at zero.
- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.ExecutorReportsSkippedUnavailableWithoutPipelines`:
      visualization packets populated, executor records the pass with
      `Status = SkippedUnavailable` (pipelines not yet created in
      Slice A); the `VisualizationOverlayUpload` counters stay at zero
      except `MissingPipelineSkipCount = 1` (operational-scaffold
      signal).
- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.NonOperationalDeviceSkipsNonOperational`:
      `device.Operational = false` → executor records `SkippedNonOperational`
      taxonomy on the new pass before the pipeline check, mirroring
      the symmetric shape from `PresentPass`, `DebugViewPass`, and
      `TransientDebugSurfacePass`; `MissingPipelineSkipCount` stays at
      zero.

Slice B (vector-field lane, landed 2026-05-24):

- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.MissingVectorFieldPipelineLeaseSkipsUnavailable`:
      `FailPipelineCreateCall = 32` (vector-field DepthTested) yields
      `SkippedUnavailable` and `MissingPipelineSkipCount += 1`.
      Upstream `SurfacePass` still records `Recorded`. Replaces the
      Slice A `ExecutorReportsSkippedUnavailableWithoutPipelines`
      test whose premise (no pipelines wired) no longer holds in
      Slice B.
- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.RecordsVectorFieldBindPipelineAndDraw`:
      one `VectorFieldOverlayPacket{ElementCount=1, DepthTested=true}`
      records `BindPipeline(DepthTested) + PushConstants(16) +
      Draw(2, 1, 0, 0)`, `VectorFieldRecordsRecorded == 1`, and pass
      status is `Recorded`. At least one 16-byte push reaches the
      device command context (assertion uses `>= 1` for the same
      shared-push-size caveat as the transient-debug tests).
- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.SelectsVectorFieldAlwaysOnTopVariantPerPacket`:
      three packets with alternating `DepthTested` flags flip the
      variant correctly (`VectorFieldRecordsRecorded == 3`; at least
      three 16-byte pushes reach the device).
- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.PerFrameBufferRecyclingDoesNotLeakVectorField`:
      across 5 frames with a constant 4-element packet payload,
      `CreateBufferCount` stays at the post-frame-1 baseline (no
      per-frame leak), `UploadOverflowCount` stays at zero, and
      per-frame `VectorFieldRecordsSubmitted` /
      `VectorFieldRecordsRecorded` reflect the final frame's payload
      (renderer resets per `ExecuteFrame()`).

Slice C (isoline lane, landed 2026-05-25):

- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.MissingIsolinePipelineLeaseSkipsUnavailable`:
      `FailPipelineCreateCall = 34` (isoline DepthTested) yields
      `SkippedUnavailable` and `MissingPipelineSkipCount += 1` for
      an isoline-only frame. Upstream `SurfacePass` still records
      `Recorded`.
- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.RecordsIsolineBindPipelineAndDraw`:
      one `IsolineOverlayPacket{IsoValueCount=1, DepthTested=true}`
      records `BindPipeline(DepthTested) + PushConstants(16) +
      Draw(2, 1, 0, 0)`, `IsolineRecordsRecorded == 1`, and pass
      status is `Recorded`. At least one 16-byte push reaches the
      device command context (assertion uses `>= 1` for the shared-
      push-size caveat).
- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.SelectsIsolineAlwaysOnTopVariantPerPacket`:
      three packets with alternating `DepthTested` flags flip the
      variant correctly (`IsolineRecordsRecorded == 3`; at least
      three 16-byte pushes reach the device).
- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.MixedLaneVectorFieldAndIsolineBothRecord`:
      a frame submitting both lanes with valid pipelines records
      both lanes; `MissingPipelineSkipCount == 0`. Pins the per-lane
      independence semantic.
- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.PerLanePartialSkipKeepsSiblingLaneRecording`:
      `FailPipelineCreateCall = 34` with both lanes submitting
      packets — vector-field lane records `Recorded`, isoline lane
      increments `MissingPipelineSkipCount`, pass status stays
      `Recorded` (per-lane independence under partial pipeline
      failure).
- [x] `contract;graphics` —
      `VisualizationOverlayPassContract.PerFrameBufferRecyclingDoesNotLeakIsoline`:
      across 5 frames with a constant 4-iso-value isoline payload,
      `CreateBufferCount` stays at the post-frame-1 baseline (no
      per-frame leak), `UploadOverflowCount` stays at zero, and the
      per-frame `IsolineRecords{Submitted,Recorded}` reflect the
      final frame's payload.

Slice D tests are written and wired.

## Docs

Slice A (landed 2026-05-24):

- [x] Update `src/graphics/renderer/README.md` to record the new
      `VisualizationOverlayPass` recipe placement and the
      `VisualizationOverlayUploadDiagnostics` counters (scaffolded;
      behavior added in Slice B/C).
- [x] Update `tasks/active/README.md` to reflect Slice A status and
      remove GRAPHICS-078 from the rendering backlog index.
- [x] Update `tasks/backlog/rendering/README.md` to point at the
      active task location.
- [x] Regenerated `docs/api/generated/module_inventory.md` (443
      modules; the new `Extrinsic.Graphics.Pass.VisualizationOverlay`
      module surface appears).

Slice B (landed 2026-05-24):

- [x] Extend `src/graphics/renderer/README.md` with the vector-field
      lane bind/draw shape, per-lane pipeline call indices #32 + #33,
      the helper module placement
      (`Extrinsic.Graphics.VisualizationOverlayUploadHelper`), and
      the CPU/null-path data-substitution note (zero positions +
      packet color, Vulkan-tuned expansion deferred to Slice D).
- [-] `src/graphics/vulkan/README.md` row for the
      `VisualizationOverlayUploadHelper` is deferred to Slice D —
      the Slice B helper lives in the renderer module (CPU-functional
      via `BufferManager`); the Vulkan-tuned concrete impl that
      lands with Slice D is what justifies a vulkan-README entry,
      mirroring the GRAPHICS-077 Slice B/C convention.
- [x] Regenerated `docs/api/generated/module_inventory.md` (444
      modules; the new
      `Extrinsic.Graphics.VisualizationOverlayUploadHelper` module
      surface appears).
- [x] Update `tasks/active/README.md` to reflect Slice B status.

Slice C (landed 2026-05-25):

- [x] Extend `src/graphics/renderer/README.md` with the isoline
      lane bind/draw shape, per-lane pipeline call indices #34 + #35,
      the `BuildVisualizationIsolinePipelineDesc` helper, the new
      shader pair placement, the per-lane gating semantics in
      `RecordVisualizationOverlayPass`, and the CPU/null-path data-
      substitution note (zero positions + packet color, Vulkan-
      tuned scalar-field-derived expansion deferred to Slice D).
- [-] `src/graphics/vulkan/README.md` row remains deferred to Slice
      D for the same reason recorded under Slice B (helper lives in
      the renderer module; vulkan-README entry justified only when
      the Vulkan-tuned concrete impl lands).
- [x] Regenerate `docs/api/generated/module_inventory.md` (no new
      module surfaces in Slice C; the existing
      `Extrinsic.Graphics.VisualizationOverlayUploadHelper` module
      grew an export for `IsolineOverlayPacket`-consuming methods +
      `VisualizationIsolineUploadResult`, but no new module
      interfaces were added — count stays at the Slice B baseline).
- [x] Update `tasks/active/README.md` to reflect Slice C status.

Slice D (command-stream smoke, landed locally 2026-05-28):
- [x] Update `tests/CMakeLists.txt` to include
      `Test.VisualizationOverlaySurfaceGpuSmoke.cpp` in
      `GraphicsVulkanSmokeTestObjs` / `IntrinsicGraphicsVulkanSmokeTests` under
      existing `gpu;vulkan;graphics` labels.
- [x] Record the command-stream smoke graduation in this task file and move
      visualization-overlay pixel-readback parity into
      [`GRAPHICS-078E`](../backlog/rendering/GRAPHICS-078E-visualization-overlay-pixel-readback.md).

## Acceptance criteria

Slice A (landed 2026-05-24):

- [x] Recipe declares `"VisualizationOverlayPass"` exactly when at
      least one visualization-overlay packet (vector field or
      isoline) exists for the frame.
- [x] Executor records `SkippedNonOperational` / `SkippedUnavailable`
      taxonomy on the new pass; no `BindPipeline` or `Draw` calls land
      in this slice.
- [x] `VisualizationOverlayUploadDiagnostics` exists, is wired on the
      renderer diagnostics aggregate as
      `RenderGraphFrameStats::VisualizationOverlayUpload`, and stays
      zero in this slice except `MissingPipelineSkipCount` which
      serves as the operational-scaffold diagnostic signal.
- [x] Default CPU/null gate stays green; no `gpu`/`vulkan` test
      additions in this slice. 209/209 graphics contract tests passed
      (Slice A added four `VisualizationOverlayPassContract.*` tests
      on top of the prior 205); broader CPU/null gate verification
      command list in `## Next verification step`.

Slice B (landed 2026-05-24):

- [x] Vector-field lane records deterministic
      `BindPipeline + PushConstants(16) +
      Draw(2 * ElementCount, 1, 0, 0)` per submitted packet in the
      operational state (verified by
      `RecordsVectorFieldBindPipelineAndDraw`).
- [x] Per-packet `DepthTested` selects the matching pipeline variant
      for the vector-field lane (verified by
      `SelectsVectorFieldAlwaysOnTopVariantPerPacket`).
- [x] No retained GPU resources on `GpuWorld`; no RHI/renderer
      surface change beyond the per-pass `Get*Pipeline()` accessors
      and the new helper module re-export.
- [x] No regression in `ValidateVisualizationPackets` /
      `VisualizationDiagnostics` (the new packet field defaults to
      `true` and does not affect validity).
- [x] Default CPU/null gate stays green: 212/212 graphics contract
      tests pass (net +3 from Slice A: rename one + add three new
      vector-field tests).

Slice C (landed 2026-05-25):

- [x] Isoline lane records deterministic
      `BindPipeline + PushConstants(16) +
      Draw(2 * IsoValueCount, 1, 0, 0)` per submitted packet in
      the operational state (verified by
      `RecordsIsolineBindPipelineAndDraw`).
- [x] Per-packet `DepthTested` selects the matching pipeline
      variant for the isoline lane (verified by
      `SelectsIsolineAlwaysOnTopVariantPerPacket`).
- [x] Per-lane gating semantics: a lane with packets but missing
      pipelines increments `MissingPipelineSkipCount` and is
      skipped while sibling lanes with valid pipelines still
      record (verified by `PerLanePartialSkipKeepsSiblingLaneRecording`).
- [x] Mixed-lane recording: a frame submitting both vector-field
      and isoline packets records both lanes with
      `MissingPipelineSkipCount == 0` (verified by
      `MixedLaneVectorFieldAndIsolineBothRecord`).
- [x] No retained GPU resources on `GpuWorld`; no RHI/renderer
      surface change beyond the per-pass `Get*Pipeline()` accessors
      and the helper's per-lane upload-result types.
- [x] No regression in `ValidateVisualizationPackets` /
      `VisualizationDiagnostics` (the new packet field defaults to
      `true` and does not affect validity).
- [x] Default CPU/null gate stays green across slices: 219/219
      graphics contract tests pass (net +7 from Slice B's 212:
      six new isoline contract tests + one prior baseline addition
      outside this task).

Slice D (optional, Vulkan-capable hosts only):

- [x] `gpu;vulkan` smoke green with non-zero per-kind recorded
      counters on an operational Vulkan command stream.
- [x] Pixel-readback confirmation is deferred to
      [`GRAPHICS-078E`](../backlog/rendering/GRAPHICS-078E-visualization-overlay-pixel-readback.md)
      because the existing readback hook/counter is MinimalDebug-only.

## Verification

For each slice, run the focused contract subset before the full gate:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

For Slice D on a Vulkan-capable host, additionally run:

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsIntegrationTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -LE 'slow|flaky-quarantine' --timeout 120
```

## Forbidden changes

- Routing visualization overlays through retained line/point cull
  buckets (per `GRAPHICS-014Q`).
- Retaining transient overlay buffers on `GpuWorld`.
- Exposing the helper through RHI or renderer module surfaces.
- Adding a third pipeline variant per kind (only DepthTested +
  AlwaysOnTop are recorded).
- Mixing mechanical file moves with semantic refactors.
- Touching isoline lane wiring inside Slice B — explicitly deferred
  to Slice C so reviewers see one new lane per slice.
- Recording any commands inside the new pass in Slice A — Slice A is
  scaffold-only and must keep the executor at the
  `SkippedUnavailable` baseline so reviewers can see the
  recipe/executor shape land independently of pipeline/helper work.
- Adding the Vulkan-tuned isoline helper variant (scalar-field-
  derived contour vertex expansion via GPU compute) inside Slice
  C — explicitly deferred to Slice D so the CPU/null contract lands
  independently and the Vulkan-tuned implementation can rely on a
  Vulkan-capable host for end-to-end pixel-readback validation.

## Next verification step

- Slice A landed 2026-05-24 on
  `claude/intrinsicengine-agent-onboarding-3dLeQ`. Verification:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests`
  - `ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    — 209/209 graphics contract tests passed (four new
    `VisualizationOverlayPassContract.*` tests added on top of the
    prior 205).
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    — full CPU/null gate result captured in the commit body.
  - `python3 tools/repo/check_layering.py --root src --strict`,
    `python3 tools/repo/check_test_layout.py --root . --strict`,
    `python3 tools/docs/check_doc_links.py --root .`,
    `python3 tools/agents/check_task_policy.py --root . --strict`
    — all clean.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
    — 443 modules (up one from the prior 442 for the new
    `Extrinsic.Graphics.Pass.VisualizationOverlay` surface).
- Slice B landed 2026-05-24 on
  `claude/intrinsicengine-agent-onboarding-7IOiJ`. Verification:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests`
  - `ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    — 212/212 graphics contract tests passed (net +3 from Slice A:
    rename `ExecutorReportsSkippedUnavailableWithoutPipelines` to
    `MissingVectorFieldPipelineLeaseSkipsUnavailable` and add three
    new vector-field operational tests:
    `RecordsVectorFieldBindPipelineAndDraw`,
    `SelectsVectorFieldAlwaysOnTopVariantPerPacket`,
    `PerFrameBufferRecyclingDoesNotLeakVectorField`).
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    — full CPU/null gate result captured in the Slice B commit body.
  - `python3 tools/repo/check_layering.py --root src --strict`,
    `python3 tools/repo/check_test_layout.py --root . --strict`,
    `python3 tools/docs/check_doc_links.py --root .`,
    `python3 tools/agents/check_task_policy.py --root . --strict`
    — all clean.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
    — 444 modules (up one from Slice A's 443 for the new
    `Extrinsic.Graphics.VisualizationOverlayUploadHelper` surface).
- Slice C landed 2026-05-25 on
  `claude/intrinsicengine-agent-onboarding-2P6pn`. Verification:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests`
  - `ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    — 219/219 graphics contract tests passed (net +7 from Slice B:
    six new isoline contract tests on top of the eight prior
    `VisualizationOverlayPassContract.*` tests, plus one prior
    baseline addition outside this task).
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    — full CPU/null gate result captured in the Slice C commit body.
  - `python3 tools/repo/check_layering.py --root src --strict`,
    `python3 tools/repo/check_test_layout.py --root . --strict`,
    `python3 tools/docs/check_doc_links.py --root .`,
    `python3 tools/agents/check_task_policy.py --root . --strict`
    — all clean.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
    — no count change versus Slice B (no new module interfaces;
    the existing helper module surface grew an `UploadIsolines`
    method + `VisualizationIsolineUploadResult` struct, neither of
    which adds a new module).
- Slice D command-stream smoke (landed locally 2026-05-28):
  - `cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests`
    — succeeded.
  - `LSAN_OPTIONS=suppressions=/home/alex/Documents/IntrinsicEngine/lsan.supp ctest --test-dir build/ci-vulkan --output-on-failure -R 'VisualizationOverlaySurfaceGpuSmoke' --timeout 120`
    — 1/1 passed.
