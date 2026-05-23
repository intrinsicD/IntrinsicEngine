# GRAPHICS-077 — Backend transient-debug-primitive upload helper

## Status

- Status: in-progress (planning landed; Slice A not started). Promoted from
  `tasks/backlog/rendering/` on 2026-05-23 because GRAPHICS-076 Slice D is
  parked on a Vulkan-capable host blocker and GRAPHICS-077 is the next
  earliest unblocked Theme A leaf per `tasks/backlog/README.md` (all of its
  rendering-DAG upstream gates — GRAPHICS-010Q, GRAPHICS-072, GRAPHICS-031A —
  are retired in `tasks/done/`).
- Owner/agent: unassigned; next pick-up by any agent. Branch for the
  promotion: `claude/intrinsicengine-agent-onboarding-utAFW`.
- Branch: subsequent slices will be developed on new
  `claude/intrinsicengine-agent-onboarding-*` branches.
- Started: 2026-05-23 (promotion only; no implementation yet).
- Next verification step: when Slice A is picked up, configure with the
  `ci` preset, build `IntrinsicGraphicsContractTests`
  + `IntrinsicGraphicsContractCpuTests`, and run the contract CPU/null
  gate (`ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`).
  See Slice A in `## Slice plan` and `## Verification` for the exact
  test/file additions to validate against.

## Slice plan

The task spans (a) a backend-local per-frame upload helper, (b) two
pipeline variants per primitive lane (line, point, triangle) for six
pipelines total, (c) a new pass that draws after lit composition into
`SceneColorHDR`/`SceneDepth`, (d) executor routing through the
`Recorded`/`SkippedNonOperational`/`SkippedUnavailable` taxonomy, and
(e) deterministic CPU diagnostics. Each slice below is independently
reviewable and preserves the CPU/null correctness gate; only the final
slice exercises an opt-in `gpu;vulkan` smoke.

- **Slice A (next pick-up).** Pin the recipe/executor scaffold for the
  new `TransientDebugSurfacePass` without committing to pipelines or the
  helper yet. Adds `FrameRecipePassKind::TransientDebugSurface` plus the
  matching `features.EnableTransientDebugSurface` derivation
  (`!world.DebugPrimitives.Lines.empty() ||
  !world.DebugPrimitives.Points.empty() ||
  !world.DebugPrimitives.Triangles.empty()`), declares the recipe pass
  node with `Read(SceneColorHDR, ColorAttachmentReadWrite) +
  Read(SceneDepth, DepthRead) + SetRenderPass(...)` against the existing
  `SceneColorHDR`/`SceneDepth` LOAD-store attachments (placed between
  lit composition and `PostProcessHistogramPass` so it observes the
  current scene color but does not interfere with the postprocess
  chain), adds a `TransientDebugSurfacePass` shell class (`SetPipeline`
  / `Execute` no-op stubs with the three-state taxonomy), wires the
  executor branch `"TransientDebugSurfacePass"` to a new
  `RecordTransientDebugSurfacePass(...)` helper that returns
  `SkippedUnavailable` (no pipelines yet) when the feature is enabled
  and is absent from `CommandRecords` when the feature is disabled,
  and adds a zeroed `TransientDebugUploadDiagnostics` struct on the
  renderer diagnostics aggregate (fields: `UploadOverflowCount`,
  `LineRecordsSubmitted`, `PointRecordsSubmitted`,
  `TriangleRecordsSubmitted`, `LineRecordsRecorded`,
  `PointRecordsRecorded`, `TriangleRecordsRecorded`,
  `MissingPipelineSkipCount`). Updates
  `Test.RendererFrameLifecycle.cpp` to add
  `"TransientDebugSurfacePass"` to `kSoftSkippedPasses` so the
  baseline lifecycle assertion stays deterministic.
  No new pipelines, no helper, no shader files in Slice A.
- **Slice B.** Triangle-lane operational wiring. Adds two triangle
  pipelines (`DepthTested` + `AlwaysOnTop`) via a
  `BuildTransientDebugTrianglePipelineDesc(depthTested)` helper, two
  pipeline-lease members on `NullRenderer`, the new shader pair
  `assets/shaders/transient_debug_triangle.{vert,frag}` (positions in,
  RGBA8 color in, drawn into `SceneColorHDR`), the `Pass.Present`
  pattern of `Initialize`/`Shutdown`/`Rebuild`, and the per-frame
  triangle-lane upload through a new `TransientDebugUploadHelper`
  (the helper itself is backend-local under `src/graphics/vulkan` but
  exposes a CPU-mockable interface so contract tests can assert the
  shape without a real Vulkan device). Records `BindPipeline +
  BindVertexBuffer + Draw(3 * N, 1, 0, 0)` per triangle packet, with
  per-packet `DepthTested` selecting the matching pipeline variant
  and `RecordTransientDebugSurfacePass` flipping from
  `SkippedUnavailable` to `Recorded` when at least one triangle
  packet records. Increments `TriangleRecordsSubmitted` /
  `TriangleRecordsRecorded` deterministically.
- **Slice C.** Line + Point lane operational wiring. Mirrors Slice B
  for line and point primitives: four more pipelines
  (`Line.DepthTested`, `Line.AlwaysOnTop`, `Point.DepthTested`,
  `Point.AlwaysOnTop`), the matching pipeline-desc helpers, the
  shader pairs `assets/shaders/transient_debug_line.{vert,frag}` and
  `assets/shaders/transient_debug_point.{vert,frag}`, and per-lane
  upload + recording paths through `TransientDebugUploadHelper`. After
  this slice all three lanes record their respective primitive draws
  with deterministic CPU diagnostics, closing `Scaffolded →
  CPUContracted` for the full transient-debug surface.
- **Slice D (optional, deferred).** Opt-in `gpu;vulkan;graphics` smoke
  asserting the transient-debug surface pass actually rasterizes
  through a real Vulkan device on a Vulkan-capable host. Mirrors the
  GRAPHICS-033D bounded `engine.Run()` driver helper and the
  GRAPHICS-076 Slice D pattern. Deferred behind the same Vulkan-host
  gate as GRAPHICS-076 Slice D; CPU-only hosts ship A/B/C and leave
  this slice for a later agent.

## Maturity

- Target: `CPUContracted` after Slice C on every host (full
  three-lane recording shape verified through the CPU/null gate);
  `Operational` after the optional Slice D on Vulkan-capable hosts.
- Slice A closes `Scaffolded → Scaffolded` for the executor + recipe
  shape only — no recording behavior is added, so the slice
  intentionally lands at the `Scaffolded` taxonomy level. Per the
  `Scaffolded` closure rule (see `docs/agent/task-maturity.md`), a
  `Scaffolded`-only slice is permissible iff the next slice that
  promotes it to `CPUContracted` is named and in-scope. Slice B is
  named here and remains in-scope for this task, so the closure rule
  is honored.
- Slice B closes triangle lane `Scaffolded → CPUContracted`.
- Slice C closes line + point lanes `Scaffolded → CPUContracted` and
  closes the task at `CPUContracted` on CPU-only hosts.
- Slice D closes `CPUContracted → Operational` on Vulkan-capable
  hosts.

## Goal
- Implement the per-frame host-visible upload helper for transient `DebugLinePacket`/`DebugPointPacket`/`DebugTrianglePacket` spans declared by `GRAPHICS-010Q`: a backend-local helper under `src/graphics/vulkan` that expands the sanitized packet spans into per-frame transient vertex/index buffers, recycled each frame, never retained on `GpuWorld` and never exposed through RHI or renderer module surfaces. Add the dedicated debug-surface overlay pass that draws debug triangles into `SceneColorHDR`/`SceneDepth` after lit composition.

## Non-goals
- No routing through retained `GpuRender_Line` / `GpuRender_Point` cull buckets (rejected per `GRAPHICS-010Q`).
- No `GpuWorld` exposure of these resources.
- No new cull buckets or frame-graph resources.
- No visualization-overlay pipeline (vector-field / isoline / Htex / UV bake — those are `GRAPHICS-078`).
- No RHI module surface change.

## Context
- Status: Slice A not started; planning landed on
  `claude/intrinsicengine-agent-onboarding-utAFW`.
- Owner/layer: `graphics/vulkan` for the helper + pipelines; `graphics/renderer` for the new pass class, recipe declaration, and executor route.
- Planning anchors: `tasks/done/GRAPHICS-010-lines-points-debug-primitives.md`, `tasks/done/GRAPHICS-010Q-transient-debug-backend-clarifications.md`.
- Today: `NullRenderer` accepts and sanitizes `DebugLinePacket`/`DebugPointPacket`/`DebugTrianglePacket` spans (`InvalidSnapshotRecordCount` is the only diagnostic), but no GPU upload helper exists; debug triangles never reach the framebuffer. The packet spans flow into `RenderWorld::DebugPrimitives.{Lines,Points,Triangles}` via `m_DebugLinePackets`/`m_DebugPointPackets`/`m_DebugTrianglePackets` and `HasTransientDebug` reflects the union of overlay-on or any non-empty packet span.
- The depth-tested vs always-on-top behavior is two pipeline variants per primitive lane (per packet `DepthTested` field), not separate cull buckets or frame-graph resources.
- Pass insertion point: the new `TransientDebugSurfacePass` must run after lit composition (`CompositionPass` for deferred, `SurfacePass` for forward) and before the postprocess chain so that transient primitives reach the postprocess inputs deterministically. Slice A places the new pass between the lighting-path surface/composition family and `PostProcessHistogramPass`.

## Required changes

Slice A (next pick-up):
- [ ] Add `FrameRecipePassKind::TransientDebugSurface` to
      `src/graphics/renderer/Graphics.FrameRecipe.cppm` (append at the
      end of the enum to keep prior values stable).
- [ ] Add `bool EnableTransientDebugSurface{false}` field to
      `FrameRecipeFeatures` in the same file.
- [ ] In `DeriveDefaultFrameRecipeFeatures(...)`, derive
      `features.EnableTransientDebugSurface =
      !world.DebugPrimitives.Lines.empty() ||
      !world.DebugPrimitives.Points.empty() ||
      !world.DebugPrimitives.Triangles.empty()` so the pass is
      omitted entirely from `RenderGraphFrameStats::CommandRecords`
      when no transient debug primitives exist for the frame.
- [ ] In `DescribeDefaultFrameRecipe(...)`, add
      `AddPass(out, FrameRecipePassKind::TransientDebugSurface,
      "TransientDebugSurfacePass", features.EnableTransientDebugSurface,
      false, {"SceneColorHDR", "SceneDepth"}, {"SceneColorHDR"})`
      placed between the lit-composition family and
      `PostProcessHistogramPass`.
- [ ] In `BuildDefaultFrameRecipe(...)`, when
      `features.EnableTransientDebugSurface` is set, declare the pass
      node with `Read(SceneColorHDR, TextureUsage::ColorAttachmentReadWrite)
      + Read(SceneDepth, TextureUsage::DepthRead) +
      Write(SceneColorHDR, TextureUsage::ColorAttachmentWrite) +
      SetRenderPass(builder.BuildColorAttachmentRenderPass({SceneColorHDR}, SceneDepth))`
      (LOAD-store, depth-read-only) so the framegraph compiler emits a
      real render-pass attachment set even though Slice A records no
      commands inside it. Use the existing helper pattern from
      `"PostProcessPass"` for ordering.
- [ ] Add a `TransientDebugSurfacePass` shell class at
      `src/graphics/renderer/Passes/Pass.TransientDebug.Surface.{cppm,cpp}`
      mirroring the `PresentPass` / `MinimalDebugPresentPass` shape:
      default-constructible, `SetPipeline(RHI::PipelineHandle)`,
      `Execute(RHI::ICommandContext&)` that no-ops when no pipeline is
      bound, and a `GetPipeline()` accessor for the renderer's
      fail-closed prerequisite check. Module name
      `Extrinsic.Graphics.Pass.TransientDebug.Surface`.
- [ ] Add `m_TransientDebugSurfacePass` member to `NullRenderer`
      (no pipeline lease yet in Slice A — that lands in Slice B).
- [ ] Add executor branch `else if (passName == std::string_view{"TransientDebugSurfacePass"})`
      routing to a new `RecordTransientDebugSurfacePass(...)` helper
      that returns `SkippedNonOperational` when the device is not
      operational and `SkippedUnavailable` otherwise (no pipelines
      yet in Slice A). Place the branch between the
      `"PostProcessAAResolvePass"` branch and the `"DebugViewPass"`
      branch so executor branch order matches recipe order.
- [ ] Add a `TransientDebugUploadDiagnostics` struct on the renderer
      diagnostics aggregate with fields `UploadOverflowCount`,
      `LineRecordsSubmitted`, `PointRecordsSubmitted`,
      `TriangleRecordsSubmitted`, `LineRecordsRecorded`,
      `PointRecordsRecorded`, `TriangleRecordsRecorded`,
      `MissingPipelineSkipCount` (all `std::uint64_t`, zeroed in
      Slice A — Slice B begins incrementing the triangle counters and
      Slice C begins incrementing the line + point counters).

Slice B (triangle lane, future slice):
- [ ] Add `BuildTransientDebugTrianglePipelineDesc(depthTested)` helper
      pinned to `SceneColorHDR` format with depth-test variant,
      `Rasterizer.Culling = None`, `Topology = TriangleList`,
      `PushConstantSize = 0`, color-write all channels,
      `BlendEnabled = false`.
- [ ] Add `assets/shaders/transient_debug_triangle.{vert,frag}` shader
      pair: vertex shader reads position (`vec3`) + packed RGBA8
      color (`uint32`) per vertex, transforms by camera UBO; fragment
      shader writes unpacked RGBA color directly.
- [ ] Add `m_TransientDebugTrianglePipelineLeaseDepthTested` +
      `m_TransientDebugTrianglePipelineLeaseAlwaysOnTop` to
      `NullRenderer`. Create after present at call indices #26 + #27
      (immediately after the GRAPHICS-076 Slice B DebugView pipeline
      at #25) so upstream `FailPipelineCreateCall` indices stay
      stable.
- [ ] Add `TransientDebugUploadHelper` interface declaration under
      `src/graphics/renderer/` (CPU-mockable through a virtual
      interface so contract tests can substitute) with concrete
      implementation under `src/graphics/vulkan/` returning
      RHI-internal vertex buffer views per lane.
- [ ] Wire `RecordTransientDebugSurfacePass(...)` to consume the
      sanitized triangle span from `RenderWorld::DebugPrimitives.Triangles`,
      upload through the helper, and record per-packet
      `BindPipeline(DepthTested or AlwaysOnTop) +
      BindVertexBuffer + Draw(3 * triangleCount, 1, 0, 0)` shape.
      Flip the pass to `Recorded` when at least one triangle packet
      records; `SkippedUnavailable` when either pipeline lease is
      missing.

Slice C (line + point lanes, future slice):
- [ ] Add `BuildTransientDebugLinePipelineDesc(depthTested)` +
      `BuildTransientDebugPointPipelineDesc(depthTested)` helpers
      with `Topology = LineList` and `Topology = PointList`
      respectively.
- [ ] Add `assets/shaders/transient_debug_line.{vert,frag}` +
      `assets/shaders/transient_debug_point.{vert,frag}` shader
      pairs.
- [ ] Add four more pipeline-lease members on `NullRenderer`
      (`Line.DepthTested`, `Line.AlwaysOnTop`, `Point.DepthTested`,
      `Point.AlwaysOnTop`) created at call indices #28-#31.
- [ ] Extend `TransientDebugUploadHelper` for line + point lanes.
- [ ] Extend `RecordTransientDebugSurfacePass(...)` to record per-lane
      bind + draw shape with per-packet `DepthTested` selecting the
      variant.

Slice D (optional `gpu;vulkan` smoke, deferred):
- [ ] Add `tests/integration/graphics/Test.TransientDebugSurfaceGpuSmoke.cpp`
      under `gpu;vulkan;graphics` labels, sharing the GRAPHICS-033D
      bounded `engine.Run()` driver helper. Asserts that submitting a
      mixed-lane debug primitive set through `RenderFrameInput` results
      in a frame where pixel-readback shows the expected debug
      triangle/line/point colors, and `TransientDebugUploadDiagnostics`
      counters match the submitted counts. Requires Vulkan-capable host.

## Tests

Slice A (next pick-up):
- [ ] `contract;graphics` — `TransientDebugSurfacePassContract.RecipeDeclaresPassWhenTransientPrimitivesExist`:
      `RenderWorld::DebugPrimitives.Triangles` populated with one
      triangle packet, `DescribeDefaultFrameRecipe(DeriveDefaultFrameRecipeFeatures(world))`
      includes `"TransientDebugSurfacePass"` exactly once with the
      correct reads/writes/render-pass shape.
- [ ] `contract;graphics` — `TransientDebugSurfacePassContract.RecipeOmitsPassWhenNoTransientPrimitives`:
      empty debug primitive spans → recipe omits the pass entirely;
      executor never sees the branch.
- [ ] `contract;graphics` — `TransientDebugSurfacePassContract.ExecutorReportsSkippedUnavailableWithoutPipelines`:
      transient primitives populated, executor records the pass with
      `Status = SkippedUnavailable` (pipelines not yet created in
      Slice A), and the `TransientDebugUploadDiagnostics` counters
      stay at zero in this slice.
- [ ] `contract;graphics` — `TransientDebugSurfacePassContract.NonOperationalDeviceSkipsNonOperational`:
      `device.Operational = false` → executor records `SkippedNonOperational`
      taxonomy on the new pass, mirroring the symmetric shape from
      `PresentPass` and `DebugViewPass`.
- [ ] `contract;graphics` — `Test.RendererFrameLifecycle` refreshed:
      `kSoftSkippedPasses` gains `"TransientDebugSurfacePass"` so the
      baseline lifecycle assertion stays deterministic; no
      `BindPipelineCalls` delta in Slice A.

Slice B (triangle lane, future slice):
- [ ] `contract;graphics` — `TransientDebugSurfacePassContract.RecordsTriangleBindPipelineAndDraw`:
      one `DebugTrianglePacket{DepthTested=true, VertexCount=3}`
      records `BindPipeline(DepthTested) + BindVertexBuffer + Draw(3, 1, 0, 0)`,
      `TriangleRecordsRecorded == 1`, and pass status is `Recorded`.
- [ ] `contract;graphics` — `TransientDebugSurfacePassContract.SelectsAlwaysOnTopVariantPerPacket`:
      mixed `DepthTested` flags across packets cause the correct
      pipeline variant to bind per packet.
- [ ] `contract;graphics` — `TransientDebugSurfacePassContract.MissingTrianglePipelineLeaseSkipsUnavailable`:
      `FailPipelineCreateCall = 26` (triangle DepthTested) yields
      `SkippedUnavailable` and `MissingPipelineSkipCount += 1`.
- [ ] `contract;graphics` — `TransientDebugSurfacePassContract.PerFrameBufferRecycling`:
      across N frames the upload-helper's transient buffer
      allocator counters return to baseline (no per-frame leak).

Slice C (line + point lanes, future slice):
- [ ] `contract;graphics` — line + point lane bind/draw shape
      assertions mirroring the triangle Slice B tests.
- [ ] `contract;graphics` — variant selection per `DepthTested` packet
      field for line + point lanes.
- [ ] `contract;graphics` — missing pipeline lease per lane yields
      `SkippedUnavailable` with the matching counter increment.

Slice D (optional `gpu;vulkan` smoke, deferred):
- [ ] `gpu;vulkan;graphics` — pixel-readback assertion that the
      transient triangle/line/point primitives reach the swapchain
      with the expected colors when promoted Vulkan is enabled.

## Docs

Slice A (next pick-up):
- [ ] Update `src/graphics/renderer/README.md` to record the new
      `TransientDebugSurfacePass` recipe placement and the
      `TransientDebugUploadDiagnostics` counters (scaffolded; behavior
      added in Slice B/C).
- [ ] Update `tasks/active/README.md` to reflect Slice A status after
      Slice A lands.
- [ ] Keep `tasks/backlog/rendering/README.md` pointing at the
      active task location (already done as part of the promotion
      slice that lands this planning artifact).

Slice B (triangle lane, future slice):
- [ ] Extend `src/graphics/renderer/README.md` with the triangle lane
      bind/draw shape and the
      `TriangleRecordsSubmitted`/`TriangleRecordsRecorded` counters.
- [ ] Add a row to `src/graphics/vulkan/README.md` for the
      `TransientDebugUploadHelper` (backend-local).

Slice C (line + point lanes, future slice):
- [ ] Extend both READMEs above with line + point lane equivalents.

## Acceptance criteria

Slice A (next pick-up):
- [ ] Recipe declares `"TransientDebugSurfacePass"` exactly when at
      least one transient debug primitive exists for the frame.
- [ ] Executor records `SkippedNonOperational` /
      `SkippedUnavailable` taxonomy on the new pass; no `BindPipeline`
      or `Draw` calls land in this slice.
- [ ] `TransientDebugUploadDiagnostics` exists, is wired on the
      renderer diagnostics aggregate, and stays zero in this slice.
- [ ] Default CPU/null gate stays green; no `gpu`/`vulkan` test
      additions in this slice.

Full task (after Slice C):
- [ ] Submitted debug primitives across all three lanes produce
      deterministic `BindPipeline + Draw(...)` records in the
      operational state.
- [ ] Per-packet `DepthTested` selects the matching pipeline variant
      for each lane.
- [ ] No retained GPU resources on `GpuWorld`; no RHI/renderer surface
      change beyond the per-pass `Get*Pipeline()` accessors.
- [ ] No regression in transient-debug rejection counters or
      `InvalidSnapshotRecordCount` semantics.
- [ ] Default CPU/null gate stays green across slices.

Slice D (optional, Vulkan-capable hosts only):
- [ ] `gpu;vulkan` smoke green with non-zero per-lane recorded
      counters and pixel-readback confirming the transient primitives
      reach the swapchain.

## Verification

For each slice, run the focused contract subset before the full gate:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicGraphicsContractCpuTests IntrinsicGraphicsVulkanContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

(Note: the original backlog task's `Verification` block named
`ExtrinsicBackendsVulkanContractTests` — the actual repository target
is `IntrinsicGraphicsVulkanContractTests` per
`tests/CMakeLists.txt:967-973`; the corrected name is used above and
any slice landing should update this task file accordingly.)

For Slice D on a Vulkan-capable host, additionally run:

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsIntegrationTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -LE 'slow|flaky-quarantine' --timeout 120
```

## Forbidden changes
- Routing transient debug through retained cull buckets.
- Retaining transient buffers on `GpuWorld`.
- Exposing the helper through RHI or renderer module surfaces.
- Adding a third pipeline variant per lane (only DepthTested + AlwaysOnTop are recorded).
- Mixing mechanical file moves with semantic refactors.
- Touching line/point lane wiring inside Slice B — explicitly deferred
  to Slice C so reviewers see one new lane per slice.
- Recording any commands inside the new pass in Slice A — Slice A is
  scaffold-only and must keep the executor at the `SkippedUnavailable`
  baseline so reviewers can see the recipe/executor shape land
  independently of pipeline/helper work.

## Next verification step
- Slice A pick-up: configure with the `ci` preset on a `clang-20`
  host, build `IntrinsicGraphicsContractTests` +
  `IntrinsicGraphicsContractCpuTests`, and run the contract CPU/null
  gate (`ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine'`).
- Slice B/C pick-up: as Slice A, plus rebuild
  `IntrinsicGraphicsVulkanContractTests` so the helper's
  backend-local CPU-mock surface is exercised.
- Slice D pick-up (Vulkan-capable host): configure with `ci-vulkan`,
  build `IntrinsicGraphicsIntegrationTests`, and run the opt-in
  `gpu;vulkan` smoke.
