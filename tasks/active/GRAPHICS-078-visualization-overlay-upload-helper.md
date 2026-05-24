# GRAPHICS-078 — Backend visualization-overlay upload helper (vector field + isoline)

## Status

- Status: in-progress (Slice A — scaffold-only — landing on this branch).
  Slices B/C wire the operational lanes for vector fields and isolines
  respectively; Slice D is the optional `gpu;vulkan` smoke deferred
  behind a Vulkan-capable host gate.
- Owner/agent: in-progress on `claude/intrinsicengine-agent-onboarding-3dLeQ`
  for Slice A; Slice B/C/D unassigned.
- Branch: Slice A lands on `claude/intrinsicengine-agent-onboarding-3dLeQ`.
- Started: 2026-05-24. Promoted from
  `tasks/backlog/rendering/GRAPHICS-078-visualization-overlay-upload-helper.md`
  as the next earliest unblocked Theme A leaf after GRAPHICS-076 and
  GRAPHICS-077 each parked on their Vulkan-host blockers (only their
  respective Slice D opt-in `gpu;vulkan` smokes remain). GRAPHICS-078
  directly mirrors the GRAPHICS-077 transient-debug-primitive upload
  pattern: per-frame host-visible buffers, two pipeline variants per
  kind (depth-tested + always-on-top), one consolidated overlay pass
  drawing into `SceneColorHDR`/`SceneDepth` after lit composition.
- Next verification step: after Slice A lands, run the contract CPU/null
  gate (`cmake --preset ci` →
  `cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests` →
  `ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`)
  and confirm the new `VisualizationOverlayPassContract.*` tests pass
  alongside the existing graphics contract suite. The CPU/null gate
  must stay green.

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
- **Slice B.** Vector-field lane operational wiring. Adds two
  vector-field pipelines (`DepthTested` + `AlwaysOnTop`) via a
  `BuildVisualizationVectorFieldPipelineDesc(depthTested)` helper, two
  pipeline-lease members on `NullRenderer` (created at call indices
  after the GRAPHICS-077 point lane at #31, so #32 + #33), the new
  shader pair `assets/shaders/visualization_vector_field.{vert,frag}`
  (BDA-fetched glyph vertices via push constants, drawn into
  `SceneColorHDR`), and the per-frame vector-field-lane upload through
  a new `VisualizationOverlayUploadHelper` (declared as the virtual
  `IVisualizationOverlayUploadHelper` in
  `Extrinsic.Graphics.VisualizationOverlayUploadHelper` so contract
  tests can substitute; the default concrete impl lives in the renderer
  module and uses `BufferManager` + `IDevice::WriteBuffer(...)` —
  the Vulkan-tuned variant is deferred to Slice D per the
  backend-locality non-goal that GRAPHICS-077 Slice D inherits).
  Records `BindPipeline(variant) + PushConstants(16) + Draw(N, 1, 0,
  0)` per vector-field packet (N = glyph vertex count per packet, BDA-
  based vertex fetch via push constants). Per-packet `DepthTested`
  selects the matching pipeline variant;
  `RecordVisualizationOverlayPass` flips from `SkippedUnavailable` to
  `Recorded` when at least one vector-field packet records. Increments
  `VectorFieldRecordsSubmitted` / `VectorFieldRecordsRecorded`
  deterministically.
- **Slice C.** Isoline lane operational wiring. Mirrors Slice B for
  isoline polylines: two more pipelines (`Isoline.DepthTested`,
  `Isoline.AlwaysOnTop`, call indices #34 + #35), the matching
  pipeline-desc helper, the shader pair
  `assets/shaders/visualization_isoline.{vert,frag}` (`LineList`
  topology), and per-lane upload + recording paths through
  `VisualizationOverlayUploadHelper`.
  `RecordVisualizationOverlayPass(...)` gates each lane independently:
  a lane with packets but a missing or invalid pipeline pair
  increments `MissingPipelineSkipCount` and is skipped, while sibling
  lanes with valid pipelines still record; the pass status is
  `Recorded` when at least one lane recorded, and `SkippedUnavailable`
  when every submitted lane failed its gate. Both lanes now record
  their respective overlay draws with deterministic CPU diagnostics,
  closing `Scaffolded → CPUContracted` for the full visualization
  overlay on CPU-only hosts. Mirrors GRAPHICS-077 Slice C's per-lane
  independence semantics exactly.
- **Slice D (optional, deferred).** Opt-in `gpu;vulkan;graphics` smoke
  asserting the visualization overlay pass actually rasterizes through
  a real Vulkan device on a Vulkan-capable host. Mirrors the
  GRAPHICS-033D bounded `engine.Run()` driver helper and the
  GRAPHICS-076 Slice D / GRAPHICS-077 Slice D pattern. Deferred behind
  the same Vulkan-host gate as GRAPHICS-076 Slice D; CPU-only hosts
  ship A/B/C and leave this slice for a later agent.

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
- Slice C closes both lanes at `CPUContracted` and closes the task at
  `CPUContracted` on CPU-only hosts.
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

Slice A (this slice):

- [ ] Add `FrameRecipePassKind::VisualizationOverlay` to
      `src/graphics/renderer/Graphics.FrameRecipe.cppm` (appended at
      the end of the enum to keep prior values stable).
- [ ] Add `bool EnableVisualizationOverlay{false}` field to
      `FrameRecipeFeatures` in the same file.
- [ ] In `DeriveDefaultFrameRecipeFeatures(...)`, derive
      `features.EnableVisualizationOverlay =
      !world.Visualization.VectorFields.empty() ||
      !world.Visualization.Isolines.empty()` so the pass is omitted
      entirely from `RenderGraphFrameStats::CommandRecords` when no
      visualization overlay packets exist for the frame.
- [ ] In `DescribeDefaultFrameRecipe(...)`, add
      `AddPass(out, FrameRecipePassKind::VisualizationOverlay,
      "VisualizationOverlayPass", features.EnableVisualizationOverlay,
      false, {"SceneColorHDR", "SceneDepth"}, {"SceneColorHDR"})`
      placed between `TransientDebugSurfacePass` and
      `PostProcessHistogramPass`.
- [ ] In `BuildDefaultFrameRecipe(...)`, when
      `features.EnableVisualizationOverlay` is set, declare the pass
      node with `Read(SceneDepth, TextureUsage::DepthRead) +
      Write(SceneColorHDR, TextureUsage::ColorAttachmentWrite) +
      SetRenderPass(...)`. The render-pass desc uses a new
      `kVisualizationOverlayRenderPassColorAttachments` LOAD-store
      template (preserves lit color) paired with a LOAD/Store depth
      attachment, mirroring the GRAPHICS-077 transient-debug shape.
- [ ] Add a `VisualizationOverlayPass` shell class at
      `src/graphics/renderer/Passes/Pass.VisualizationOverlay.{cppm,cpp}`
      mirroring the `TransientDebugSurfacePass` shape:
      default-constructible, per-kind `Set*Pipeline(RHI::PipelineHandle)`
      stubs (`VectorFieldDepthTested`, `VectorFieldAlwaysOnTop`,
      `IsolineDepthTested`, `IsolineAlwaysOnTop`), matching `Get*Pipeline()`
      accessors for the renderer's fail-closed prerequisite check.
      Module name `Extrinsic.Graphics.Pass.VisualizationOverlay`.
      Slice A only ships the pipeline-handle bookkeeping; the
      `Execute*` bodies land with Slices B/C.
- [ ] Add a `VisualizationOverlayUploadDiagnostics` struct co-located
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
- [ ] Add `m_VisualizationOverlayPass` member to `NullRenderer`
      (no pipeline lease yet in Slice A — that lands in Slice B).
- [ ] Add executor branch `else if (passName ==
      std::string_view{"VisualizationOverlayPass"})` routing to a new
      `RecordVisualizationOverlayPass(...)` helper that returns
      `SkippedNonOperational` when the device is not operational and
      `SkippedUnavailable` otherwise (with `MissingPipelineSkipCount`
      increment on the operational-no-pipeline path so the diagnostic
      distinguishes "scaffold-only" from "feature off"). Branch
      placed immediately after `"TransientDebugSurfacePass"` and
      before `"Present"` so it sits in source order alongside the
      GRAPHICS-077 transient-debug branch.

Slice B (vector-field lane, deferred):

- [ ] Vector-field upload helper interface + concrete impl, two
      pipelines (`VectorField.DepthTested`, `VectorField.AlwaysOnTop`)
      at call indices #32 + #33, shader pair
      `assets/shaders/visualization_vector_field.{vert,frag}`.

Slice C (isoline lane, deferred):

- [ ] Isoline upload helper extension, two more pipelines
      (`Isoline.DepthTested`, `Isoline.AlwaysOnTop`) at call
      indices #34 + #35, shader pair
      `assets/shaders/visualization_isoline.{vert,frag}`, per-lane
      gating semantics matching GRAPHICS-077 Slice C.

Slice D (optional `gpu;vulkan` smoke, deferred):

- [ ] Add `tests/integration/graphics/Test.VisualizationOverlaySurfaceGpuSmoke.cpp`
      under `gpu;vulkan;graphics` labels, sharing the GRAPHICS-033D
      bounded `engine.Run()` driver helper.

## Tests

Slice A (this slice):

- [ ] `contract;graphics` —
      `VisualizationOverlayPassContract.RecipeDeclaresPassWhenOverlayPacketsExist`:
      `RenderWorld::Visualization.VectorFields` populated with one
      vector-field packet,
      `DescribeDefaultFrameRecipe(DeriveDefaultFrameRecipeFeatures(world))`
      includes `"VisualizationOverlayPass"` exactly once with the
      correct reads/writes shape.
- [ ] `contract;graphics` —
      `VisualizationOverlayPassContract.RecipeOmitsPassWhenNoOverlayPackets`:
      empty visualization spans → recipe declares the pass with
      `Enabled=false` (compiled graph omits it entirely from
      `CommandRecords`); executor never sees the branch;
      `VisualizationOverlayUpload` counters stay at zero.
- [ ] `contract;graphics` —
      `VisualizationOverlayPassContract.ExecutorReportsSkippedUnavailableWithoutPipelines`:
      visualization packets populated, executor records the pass with
      `Status = SkippedUnavailable` (pipelines not yet created in
      Slice A); the `VisualizationOverlayUpload` counters stay at zero
      except `MissingPipelineSkipCount = 1` (operational-scaffold
      signal).
- [ ] `contract;graphics` —
      `VisualizationOverlayPassContract.NonOperationalDeviceSkipsNonOperational`:
      `device.Operational = false` → executor records `SkippedNonOperational`
      taxonomy on the new pass before the pipeline check, mirroring
      the symmetric shape from `PresentPass`, `DebugViewPass`, and
      `TransientDebugSurfacePass`; `MissingPipelineSkipCount` stays at
      zero.

Slice B/C/D tests are written when those slices land.

## Docs

Slice A (this slice):

- [ ] Update `src/graphics/renderer/README.md` to record the new
      `VisualizationOverlayPass` recipe placement and the
      `VisualizationOverlayUploadDiagnostics` counters (scaffolded;
      behavior added in Slice B/C).
- [ ] Update `tasks/active/README.md` to reflect Slice A status and
      remove GRAPHICS-078 from the rendering backlog index.
- [ ] Update `tasks/backlog/rendering/README.md` to point at the
      active task location.
- [ ] Regenerate `docs/api/generated/module_inventory.md` after the
      new `Extrinsic.Graphics.Pass.VisualizationOverlay` module is
      added.

## Acceptance criteria

Slice A (this slice):

- [ ] Recipe declares `"VisualizationOverlayPass"` exactly when at
      least one visualization-overlay packet (vector field or
      isoline) exists for the frame.
- [ ] Executor records `SkippedNonOperational` / `SkippedUnavailable`
      taxonomy on the new pass; no `BindPipeline` or `Draw` calls land
      in this slice.
- [ ] `VisualizationOverlayUploadDiagnostics` exists, is wired on the
      renderer diagnostics aggregate as
      `RenderGraphFrameStats::VisualizationOverlayUpload`, and stays
      zero in this slice except `MissingPipelineSkipCount` which
      serves as the operational-scaffold diagnostic signal.
- [ ] Default CPU/null gate stays green; no `gpu`/`vulkan` test
      additions in this slice.

Full task (after Slice C):

- [ ] Submitted visualization-overlay packets across both kinds
      produce deterministic `BindPipeline + Draw(...)` records in the
      operational state.
- [ ] Per-packet `DepthTested` selects the matching pipeline variant
      for each kind.
- [ ] No retained GPU resources on `GpuWorld`; no RHI/renderer
      surface change beyond the per-pass `Get*Pipeline()` accessors.
- [ ] No regression in `ValidateVisualizationPackets` /
      `VisualizationDiagnostics`.
- [ ] Default CPU/null gate stays green across slices.

Slice D (optional, Vulkan-capable hosts only):

- [ ] `gpu;vulkan` smoke green with non-zero per-kind recorded
      counters and pixel-readback confirming the overlay primitives
      reach the swapchain.

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

## Next verification step

- After Slice A lands: build `IntrinsicGraphicsContractCpuTests` on a
  `clang-20` host and run the default-recipe CPU/null gate
  (`ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine'`).
  Confirm the four new `VisualizationOverlayPassContract.*` tests
  pass and no existing tests regress.
- Slice B/C pick-up: a CPU-only host can pick up Slice B (vector-field
  operational wiring) immediately after Slice A merges, then Slice C
  (isoline operational wiring) after Slice B.
- Slice D pick-up (Vulkan-capable host): configure with `ci-vulkan`,
  build `IntrinsicGraphicsIntegrationTests`, and run the opt-in
  `gpu;vulkan` smoke.
