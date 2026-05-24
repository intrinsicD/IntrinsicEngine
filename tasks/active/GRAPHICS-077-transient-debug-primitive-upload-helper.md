# GRAPHICS-077 — Backend transient-debug-primitive upload helper

## Status

- Status: in-progress (Slices A + B + C landed; only the optional
  Slice D remains, blocked on a Vulkan-capable host).
  Slice A scaffolded the recipe + executor shape on
  `claude/intrinsicengine-agent-onboarding-p1J2P` on 2026-05-23
  (224/224 graphics contract tests pass). Slice B promoted the
  triangle lane from `SkippedUnavailable` to `Recorded` on
  `claude/intrinsicengine-agent-onboarding-MnHl0` on 2026-05-24 and
  verified through the contract CPU/null gate (227/227 graphics
  contract tests pass; 2201/2201 full CPU/null gate green). Slice C
  promoted the line + point lanes from `SkippedUnavailable` to
  `Recorded` on
  `claude/intrinsicengine-agent-onboarding-WbeR9` on 2026-05-24
  and verified through the contract CPU/null gate (235/235
  graphics contract tests pass; 2209/2209 full CPU/null gate
  green). The task is now at `CPUContracted` on CPU-only hosts;
  only the optional Slice D `gpu;vulkan` smoke remains.
- Owner/agent: unassigned for Slice D; next pick-up by any agent
  on a Vulkan-capable host.
- Branch: Slice A landed on
  `claude/intrinsicengine-agent-onboarding-p1J2P`; Slice B landed on
  `claude/intrinsicengine-agent-onboarding-MnHl0`; Slice C landed
  on `claude/intrinsicengine-agent-onboarding-WbeR9`;
  Slice D will land on a future
  `claude/intrinsicengine-agent-onboarding-*` branch.
- Started: 2026-05-23.
- Next verification step: when the optional Slice D is picked up
  on a Vulkan-capable host, configure with the `ci-vulkan` preset,
  build `IntrinsicGraphicsIntegrationTests`, and run the opt-in
  `gpu;vulkan` smoke (`ctest --test-dir build/ci-vulkan -L gpu -LE
  'slow|flaky-quarantine' --timeout 120`). The CPU-only Slice C
  pin is `Test.TransientDebugSurfacePass.cpp` (now fifteen contract
  tests covering recipe declaration, executor taxonomy, all three
  lanes' bind/draw shapes, per-packet variant selection per lane,
  per-lane missing-pipeline gating, mixed-lane all-three-record,
  per-lane partial-skip independence, and per-frame buffer
  recycling); the full CPU/null gate is 2209/2209 green on
  `claude/intrinsicengine-agent-onboarding-WbeR9`.

## Slice plan

The task spans (a) a backend-local per-frame upload helper, (b) two
pipeline variants per primitive lane (line, point, triangle) for six
pipelines total, (c) a new pass that draws after lit composition into
`SceneColorHDR`/`SceneDepth`, (d) executor routing through the
`Recorded`/`SkippedNonOperational`/`SkippedUnavailable` taxonomy, and
(e) deterministic CPU diagnostics. Each slice below is independently
reviewable and preserves the CPU/null correctness gate; only the final
slice exercises an opt-in `gpu;vulkan` smoke.

- **Slice A (landed 2026-05-23 on `claude/intrinsicengine-agent-onboarding-p1J2P`).** Pin the recipe/executor scaffold for the
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
- **Slice B (landed 2026-05-24 on `claude/intrinsicengine-agent-onboarding-MnHl0`).**
  Triangle-lane operational wiring. Adds two triangle pipelines
  (`DepthTested` + `AlwaysOnTop`) via a
  `BuildTransientDebugTrianglePipelineDesc(depthTested)` helper, two
  pipeline-lease members on `NullRenderer` (created at call indices
  #26 + #27 after the GRAPHICS-076 debug-view at #25), the new shader
  pair `assets/shaders/transient_debug_triangle.{vert,frag}` (BDA-
  fetched positions + packed RGBA8 color via push constants, drawn
  into `SceneColorHDR`), and the per-frame triangle-lane upload
  through a new `TransientDebugUploadHelper` (declared as the
  virtual `ITransientDebugUploadHelper` in
  `Extrinsic.Graphics.TransientDebugUploadHelper` so contract tests
  can substitute; the default concrete impl lives in the renderer
  module and uses `BufferManager` + `IDevice::WriteBuffer(...)` —
  the Vulkan-tuned variant is deferred to Slice D per the
  backend-locality non-goal). Records `BindPipeline(variant) +
  PushConstants(16) + Draw(3, 1, 0, 0)` per triangle packet (rather
  than the spec's `BindVertexBuffer + Draw(3 * N)` shape — the RHI
  has no `BindVertexBuffer` API by design, so vertex fetch is BDA-
  based via push constants; see the canonical `line.vert` precedent
  in `assets/shaders/`). Per-packet `DepthTested` selects the matching
  pipeline variant; `RecordTransientDebugSurfacePass` flips from
  `SkippedUnavailable` to `Recorded` when at least one triangle
  packet records. Increments `TriangleRecordsSubmitted` /
  `TriangleRecordsRecorded` deterministically.
- **Slice C (landed 2026-05-24 on `claude/intrinsicengine-agent-onboarding-WbeR9`).**
  Line + Point lane operational wiring. Mirrors Slice B for line and
  point primitives: four more pipelines (`Line.DepthTested`,
  `Line.AlwaysOnTop`, `Point.DepthTested`, `Point.AlwaysOnTop`),
  the matching pipeline-desc helpers, the shader pairs
  `assets/shaders/transient_debug_line.{vert,frag}` and
  `assets/shaders/transient_debug_point.{vert,frag}`, and per-lane
  upload + recording paths through `TransientDebugUploadHelper`.
  `RecordTransientDebugSurfacePass(...)` now gates each lane
  independently: a lane with packets but a missing or invalid
  pipeline pair increments `MissingPipelineSkipCount` and is
  skipped, while sibling lanes with valid pipelines still record;
  the pass status is `Recorded` when at least one lane recorded,
  and `SkippedUnavailable` when every submitted lane failed its
  gate. Per-lane independence preserves the Slice B
  `MissingTrianglePipelineLeaseSkipsUnavailable` test exactly. All
  three lanes now record their respective primitive draws with
  deterministic CPU diagnostics, closing `Scaffolded →
  CPUContracted` for the full transient-debug surface on CPU-only
  hosts.
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
- Slice B (landed) closes triangle lane `Scaffolded → CPUContracted`.
- Slice C (landed) closes line + point lanes `Scaffolded →
  CPUContracted` and closes the task at `CPUContracted` on CPU-only
  hosts.
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

Slice A (landed 2026-05-23):
- [x] Add `FrameRecipePassKind::TransientDebugSurface` to
      `src/graphics/renderer/Graphics.FrameRecipe.cppm` (appended at
      the end of the enum to keep prior values stable).
- [x] Add `bool EnableTransientDebugSurface{false}` field to
      `FrameRecipeFeatures` in the same file.
- [x] In `DeriveDefaultFrameRecipeFeatures(...)`, derive
      `features.EnableTransientDebugSurface =
      !world.DebugPrimitives.Lines.empty() ||
      !world.DebugPrimitives.Points.empty() ||
      !world.DebugPrimitives.Triangles.empty()` so the pass is
      omitted entirely from `RenderGraphFrameStats::CommandRecords`
      when no transient debug primitives exist for the frame.
- [x] In `DescribeDefaultFrameRecipe(...)`, add
      `AddPass(out, FrameRecipePassKind::TransientDebugSurface,
      "TransientDebugSurfacePass", features.EnableTransientDebugSurface,
      false, {"SceneColorHDR", "SceneDepth"}, {"SceneColorHDR"})`
      placed between `PointPass` and `PostProcessHistogramPass`.
- [x] In `BuildDefaultFrameRecipe(...)`, when
      `features.EnableTransientDebugSurface` is set, declare the pass
      node with `Read(SceneDepth, TextureUsage::DepthRead) +
      Write(SceneColorHDR, TextureUsage::ColorAttachmentWrite) +
      SetRenderPass(...)`. The render-pass desc uses a new
      `kTransientDebugSurfaceRenderPassColorAttachments` LOAD-store
      template (preserves lit color) paired with a LOAD/dontcare depth
      attachment. Deviations from the original spec text: (a) the spec
      named `TextureUsage::ColorAttachmentReadWrite` which does not
      exist in `Graphics.RenderGraph:Pass`; the LOAD-store color
      attachment + `Write(SceneColorHDR, ColorAttachmentWrite)`
      idiomatically expresses the same blended-overlay semantics
      (matches `LinePass`/`PointPass` and the canonical
      `Pass.Present` LOAD-store wiring rationale documented inline);
      (b) the spec named `builder.BuildColorAttachmentRenderPass(...)`
      which does not exist; the recipe instead constructs the
      `RHI::RenderPassDesc` inline with the LOAD-store color +
      LOAD/dontcare depth attachment template, mirroring how
      `Pass.Present` declares its color attachment with the
      `RenderPassAttachmentToken()` placeholder the compiler resolves
      from the compiled-graph attachment metadata.
- [x] Add a `TransientDebugSurfacePass` shell class at
      `src/graphics/renderer/Passes/Pass.TransientDebug.Surface.{cppm,cpp}`
      mirroring the `PresentPass` / `MinimalDebugPresentPass` shape:
      default-constructible, `SetPipeline(RHI::PipelineHandle)`,
      `Execute(RHI::ICommandContext&)` that no-ops when no pipeline is
      bound, and a `GetPipeline()` accessor for the renderer's
      fail-closed prerequisite check. Module name
      `Extrinsic.Graphics.Pass.TransientDebug.Surface`.
- [x] Add `m_TransientDebugSurfacePass` member to `NullRenderer`
      (no pipeline lease yet in Slice A — that lands in Slice B).
- [x] Add executor branch `else if (passName == std::string_view{"TransientDebugSurfacePass"})`
      routing to a new `RecordTransientDebugSurfacePass(...)` helper
      that returns `SkippedNonOperational` when the device is not
      operational and `SkippedUnavailable` otherwise (with
      `MissingPipelineSkipCount` increment on the
      operational-no-pipeline path so the diagnostic distinguishes
      "scaffold-only" from "feature off"). Branch placed immediately
      after `"PostProcessAAResolvePass"` and before `"Present"` so it
      sits between AAResolve and DebugView in source order per the
      spec.
- [x] Add a `TransientDebugUploadDiagnostics` struct on the renderer
      diagnostics aggregate with fields `UploadOverflowCount`,
      `LineRecordsSubmitted`, `PointRecordsSubmitted`,
      `TriangleRecordsSubmitted`, `LineRecordsRecorded`,
      `PointRecordsRecorded`, `TriangleRecordsRecorded`,
      `MissingPipelineSkipCount` (all `std::uint64_t`, zeroed in
      Slice A except `MissingPipelineSkipCount` which increments
      once per operational-scaffold frame — Slice B begins
      incrementing the triangle counters and Slice C begins
      incrementing the line + point counters). Exposed through the
      new `RenderGraphFrameStats::TransientDebugUpload` field; reset
      per-frame through the existing `m_LastRenderGraphStats = {}`
      cadence.

Slice B (triangle lane, landed 2026-05-24):
- [x] Add `BuildTransientDebugTrianglePipelineDesc(depthTested)` helper
      pinned to `RGBA16_FLOAT` (the `SceneColorHDR` format) with the
      depth-test variant, `Rasterizer.Culling = None`,
      `Topology = TriangleList`,
      `PushConstantSize = sizeof(TransientDebugTrianglePushConstants)`
      (16 bytes carrying BDA + per-draw `FirstVertex`),
      `ColorBlend[0].Enable = false`. Deviation from the original
      spec text: `PushConstantSize = 0` is not viable because vertex
      fetch is BDA-based in this RHI (no `BindVertexBuffer` API by
      design); the 16-byte push block carries the
      `TransientDebugUploadHelper`'s vertex buffer BDA + the per-draw
      `FirstVertex` so the BDA-fetch vertex shader can address the
      right triangle. The shader contract pin is documented inline
      in `assets/shaders/transient_debug_triangle.vert`.
- [x] Add `assets/shaders/transient_debug_triangle.{vert,frag}` shader
      pair: vertex shader reads position (`vec3`) + packed RGBA8
      color (`uint32`) per vertex via BDA, transforms by camera UBO;
      fragment shader writes unpacked RGBA color directly.
- [x] Add `m_TransientDebugTrianglePipelineLeaseDepthTested` +
      `m_TransientDebugTrianglePipelineLeaseAlwaysOnTop` to
      `NullRenderer`. Created at call indices #26 + #27 (immediately
      after the GRAPHICS-076 Slice B DebugView pipeline at #25) so
      upstream `FailPipelineCreateCall` indices stay stable.
- [x] Add `TransientDebugUploadHelper` interface declaration in
      `src/graphics/renderer/Graphics.TransientDebugUploadHelper.cppm`
      as the abstract `ITransientDebugUploadHelper` (CPU-mockable
      virtual interface so contract tests can substitute) plus the
      default concrete `TransientDebugUploadHelper` that uses
      `BufferManager` + `IDevice::WriteBuffer(...)` to recycle a
      single host-visible vertex buffer across frames. Deviation
      from the original spec text: the concrete impl lives in the
      renderer module (not `src/graphics/vulkan`) for Slice B so the
      CPU/null gate verifies the helper without a real Vulkan device;
      the Vulkan-tuned concrete impl is deferred to Slice D where it
      ships alongside the `gpu;vulkan` smoke. The interface is in
      `src/graphics/renderer/` per the spec.
- [x] Wire `RecordTransientDebugSurfacePass(cmd, world)` to consume
      the sanitized triangle span from
      `world.DebugPrimitives.Triangles`, upload through the helper,
      and record per-packet `BindPipeline(DepthTested or
      AlwaysOnTop) + PushConstants(16) + Draw(3, 1, 0, 0)` shape.
      Flips the pass to `Recorded` when at least one triangle packet
      records; `SkippedUnavailable` when either pipeline lease is
      missing (with `MissingPipelineSkipCount += 1`).

Slice C (line + point lanes, landed 2026-05-24):
- [x] Add `BuildTransientDebugLinePipelineDesc(depthTested)` +
      `BuildTransientDebugPointPipelineDesc(depthTested)` helpers
      with `Topology = LineList` and `Topology = PointList`
      respectively. Both pipelines pin
      `Rasterizer.Culling = None`, `ColorBlend[0].Enable = false`,
      `ColorTargetFormats[0] = RGBA16_FLOAT`,
      `DepthTargetFormat = D32_FLOAT`, and a 16-byte push block.
- [x] Add `assets/shaders/transient_debug_line.{vert,frag}` +
      `assets/shaders/transient_debug_point.{vert,frag}` shader
      pairs. Both lanes share the per-vertex
      `position(vec3) + packed RGBA8 color(uint32)` 16-byte layout
      with the triangle lane and BDA-fetch per-vertex through the
      same `VertexBuf` buffer-reference shape.
- [x] Add four more pipeline-lease members on `NullRenderer`
      (`Line.DepthTested`, `Line.AlwaysOnTop`, `Point.DepthTested`,
      `Point.AlwaysOnTop`) created at call indices #28-#31.
- [x] Extend `TransientDebugUploadHelper` for line + point lanes.
      Three independent per-lane host-visible buffer leases with
      geometric growth (cap 256 K vertices each) routed through a
      shared `UploadPackedVertices(...)` prologue so the per-lane
      `UploadLines(...)`/`UploadPoints(...)` bodies just pack their
      lane's source spans.
- [x] Extend `RecordTransientDebugSurfacePass(...)` to record per-lane
      bind + draw shape with per-packet `DepthTested` selecting the
      variant. Per-lane gating: a lane with packets but a missing or
      invalid pipeline pair increments `MissingPipelineSkipCount`
      and is skipped, while sibling lanes with valid pipelines still
      record. Pass status is `Recorded` when at least one lane
      recorded, `SkippedUnavailable` when every submitted lane
      failed its gate. Preserves the Slice B
      `MissingTrianglePipelineLeaseSkipsUnavailable` semantics
      exactly (a triangle-only frame with the triangle DepthTested
      pipeline failed → triangle lane skipped → no other lanes
      submitted → pass status `SkippedUnavailable` and
      `MissingPipelineSkipCount = 1`).

Slice D (optional `gpu;vulkan` smoke, deferred):
- [ ] Add `tests/integration/graphics/Test.TransientDebugSurfaceGpuSmoke.cpp`
      under `gpu;vulkan;graphics` labels, sharing the GRAPHICS-033D
      bounded `engine.Run()` driver helper. Asserts that submitting a
      mixed-lane debug primitive set through `RenderFrameInput` results
      in a frame where pixel-readback shows the expected debug
      triangle/line/point colors, and `TransientDebugUploadDiagnostics`
      counters match the submitted counts. Requires Vulkan-capable host.

## Tests

Slice A (landed 2026-05-23):
- [x] `contract;graphics` — `TransientDebugSurfacePassContract.RecipeDeclaresPassWhenTransientPrimitivesExist`:
      `RenderWorld::DebugPrimitives.Triangles` populated with one
      triangle packet, `DescribeDefaultFrameRecipe(DeriveDefaultFrameRecipeFeatures(world))`
      includes `"TransientDebugSurfacePass"` exactly once with the
      correct reads/writes shape.
- [x] `contract;graphics` — `TransientDebugSurfacePassContract.RecipeOmitsPassWhenNoTransientPrimitives`:
      empty debug primitive spans → recipe declares the pass with
      `Enabled=false` (compiled graph omits it entirely from
      `CommandRecords`); executor never sees the branch.
- [x] `contract;graphics` — `TransientDebugSurfacePassContract.ExecutorReportsSkippedUnavailableWithoutPipelines`:
      transient primitives populated, executor records the pass with
      `Status = SkippedUnavailable` (pipelines not yet created in
      Slice A); the `TransientDebugUpload` counters stay at zero
      except `MissingPipelineSkipCount = 1` (operational-scaffold
      signal).
- [x] `contract;graphics` — `TransientDebugSurfacePassContract.NonOperationalDeviceSkipsNonOperational`:
      `device.Operational = false` → executor records `SkippedNonOperational`
      taxonomy on the new pass before the pipeline check, mirroring
      the symmetric shape from `PresentPass` and `DebugViewPass`;
      `MissingPipelineSkipCount` stays at zero.
- [-] `contract;graphics` — `Test.RendererFrameLifecycle` baseline
      assertion. Deviation from the spec: the test's `RenderWorld`
      submits no transient debug primitives, so
      `EnableTransientDebugSurface` derives to `false`, the recipe
      omits the new pass entirely from `CommandRecords`, and
      `FindCommandPass(stats, "TransientDebugSurfacePass")` returns
      `nullptr`. Adding `"TransientDebugSurfacePass"` to
      `kSoftSkippedPasses` would make the lifecycle test fail on the
      `ASSERT_NE(FindCommandPass(...), nullptr)` precondition.
      `Test.TransientDebugSurfacePass.cpp` covers the deterministic
      behaviour for both the populated and empty cases, so the
      lifecycle baseline stays clean without modification.

Slice B (triangle lane, landed 2026-05-24):
- [x] `contract;graphics` — `TransientDebugSurfacePassContract.RecordsTriangleBindPipelineAndDraw`:
      one `DebugTrianglePacket{DepthTested=true, VertexCount=3}`
      records `BindPipeline(DepthTested) + PushConstants(16) +
      Draw(3, 1, 0, 0)` (BDA-based vertex fetch, no
      `BindVertexBuffer` API), `TriangleRecordsRecorded == 1`, and
      pass status is `Recorded`. At least one 16-byte push reaches
      the device command context (assertion deliberately uses
      `>= 1` because `DebugViewPushConstants` also pushes 16 bytes
      and is enabled in the same frame by the transient debug
      submission flipping `HasTransientDebug`).
- [x] `contract;graphics` — `TransientDebugSurfacePassContract.SelectsAlwaysOnTopVariantPerPacket`:
      mixed `DepthTested` flags across three packets cause the
      correct pipeline variant to bind per packet
      (`TriangleRecordsRecorded == 3`; at least three 16-byte pushes
      reach the device under the same shared-push-size caveat).
- [x] `contract;graphics` — `TransientDebugSurfacePassContract.MissingTrianglePipelineLeaseSkipsUnavailable`:
      `FailPipelineCreateCall = 26` (triangle DepthTested) yields
      `SkippedUnavailable` and `MissingPipelineSkipCount += 1`.
      Upstream `SurfacePass` still records `Recorded`.
- [x] `contract;graphics` — `TransientDebugSurfacePassContract.PerFrameBufferRecyclingDoesNotLeak`:
      across 5 frames with constant payload, `CreateBufferCount`
      stays at the post-frame-1 baseline (no per-frame leak),
      `UploadOverflowCount` stays at zero, and per-frame
      `TriangleRecordsSubmitted` / `TriangleRecordsRecorded` reflect
      the final frame's payload (renderer resets per
      `ExecuteFrame()`).

Slice C (line + point lanes, landed 2026-05-24):
- [x] `contract;graphics` —
      `TransientDebugSurfacePassContract.RecordsLineBindPipelineAndDraw`:
      one `DebugLinePacket{DepthTested=true, VertexCount=2}` records
      `BindPipeline(Line.DepthTested) + PushConstants(16) +
      Draw(2, 1, 0, 0)` and increments
      `LineRecordsSubmitted`/`LineRecordsRecorded` to 1.
- [x] `contract;graphics` —
      `TransientDebugSurfacePassContract.RecordsPointBindPipelineAndDraw`:
      one `DebugPointPacket{DepthTested=true, VertexCount=1}`
      records `BindPipeline(Point.DepthTested) + PushConstants(16)
      + Draw(1, 1, 0, 0)` and increments
      `PointRecordsSubmitted`/`PointRecordsRecorded` to 1.
- [x] `contract;graphics` —
      `TransientDebugSurfacePassContract.SelectsLineAlwaysOnTopVariantPerPacket`
      and
      `TransientDebugSurfacePassContract.SelectsPointAlwaysOnTopVariantPerPacket`:
      three packets with alternating `DepthTested` flags per lane
      flip the variant correctly and reach the device with the
      matching number of 16-byte pushes.
- [x] `contract;graphics` —
      `TransientDebugSurfacePassContract.MissingLinePipelineLeaseSkipsUnavailable`
      (`FailPipelineCreateCall = 28`) and
      `TransientDebugSurfacePassContract.MissingPointPipelineLeaseSkipsUnavailable`
      (`FailPipelineCreateCall = 30`): each lane's pipeline failure
      yields `SkippedUnavailable` with
      `MissingPipelineSkipCount += 1` on a single-lane-submission
      frame.
- [x] `contract;graphics` —
      `TransientDebugSurfacePassContract.RecordsAllThreeLanesWhenAllSubmitted`:
      a mixed-lane frame (one triangle + one line + one point)
      records `Recorded` and increments all three per-lane recorded
      counters to 1 with `MissingPipelineSkipCount` at zero.
- [x] `contract;graphics` —
      `TransientDebugSurfacePassContract.MixedLanePartialPipelineSkipRecordsRemainingLanes`:
      per-lane gating independence — failing the point pipeline
      (`FailPipelineCreateCall = 30`) while submitting one line and
      one point keeps the line lane recording (`Recorded`),
      increments `LineRecordsRecorded` to 1, and increments
      `MissingPipelineSkipCount` by exactly 1 for the skipped point
      lane.
- [x] `contract;graphics` —
      `TransientDebugSurfacePassContract.UploadOverflowSkipsUnavailableWithoutFalseRecorded`:
      regression pin for the upload-vs-pipeline gate split.
      Submitting 87 382 triangle packets (> the per-lane vertex cap
      of `1 << 18`) trips `UploadOverflowCount` and yields
      `SkippedUnavailable` rather than masking the failure as
      `Recorded`. `MissingPipelineSkipCount` stays at zero — the
      lane's pipelines are healthy and the upload gate is
      independent of the pipeline gate.

Slice D (optional `gpu;vulkan` smoke, deferred):
- [ ] `gpu;vulkan;graphics` — pixel-readback assertion that the
      transient triangle/line/point primitives reach the swapchain
      with the expected colors when promoted Vulkan is enabled.

## Docs

Slice A (landed 2026-05-23):
- [x] Update `src/graphics/renderer/README.md` to record the new
      `TransientDebugSurfacePass` recipe placement and the
      `TransientDebugUploadDiagnostics` counters (scaffolded; behavior
      added in Slice B/C).
- [x] Update `tasks/active/README.md` to reflect Slice A status.
- [x] Keep `tasks/backlog/rendering/README.md` pointing at the
      active task location (already done as part of the promotion
      slice that landed the planning artifact).
- [x] Regenerated `docs/api/generated/module_inventory.md` (441
      modules, the new `Extrinsic.Graphics.Pass.TransientDebug.Surface`
      module surface appears).

Slice B (triangle lane, landed 2026-05-24):
- [x] Extend `src/graphics/renderer/README.md` with the triangle lane
      bind/draw shape and the
      `TriangleRecordsSubmitted`/`TriangleRecordsRecorded` counters.
- [-] `src/graphics/vulkan/README.md` row for the
      `TransientDebugUploadHelper` is deferred to Slice D — the
      Slice B helper lives in the renderer module (CPU-functional
      via `BufferManager`); the Vulkan-tuned concrete impl that
      lands with Slice D is what justifies a vulkan-README entry.
- [x] Regenerated `docs/api/generated/module_inventory.md` (442
      modules including the new `Extrinsic.Graphics.TransientDebugUploadHelper`
      module surface).
- [x] Update `tasks/active/README.md` to reflect Slice B status.

Slice C (line + point lanes, landed 2026-05-24):
- [x] Extend `src/graphics/renderer/README.md` with the line + point
      lane bind/draw shapes, per-lane pipeline call indices #28-#31,
      per-lane counters (`{Line,Point}RecordsSubmitted`/`Recorded`),
      and the per-lane gating semantics in
      `RecordTransientDebugSurfacePass`.
- [-] `src/graphics/vulkan/README.md` row for the
      `TransientDebugUploadHelper` remains deferred to Slice D for
      the same reason as Slice B — the Slice C helper lives in the
      renderer module (CPU-functional via `BufferManager`); the
      Vulkan-tuned concrete impl that lands with Slice D is what
      justifies a vulkan-README entry.
- [x] Regenerated `docs/api/generated/module_inventory.md` (442
      modules; no new module surfaces — Slice C extends existing
      modules with new exported types).

## Acceptance criteria

Slice A (landed 2026-05-23):
- [x] Recipe declares `"TransientDebugSurfacePass"` exactly when at
      least one transient debug primitive exists for the frame.
- [x] Executor records `SkippedNonOperational` /
      `SkippedUnavailable` taxonomy on the new pass; no `BindPipeline`
      or `Draw` calls land in this slice.
- [x] `TransientDebugUploadDiagnostics` exists, is wired on the
      renderer diagnostics aggregate as `RenderGraphFrameStats::TransientDebugUpload`,
      and stays zero in this slice except `MissingPipelineSkipCount`
      which serves as the operational-scaffold diagnostic signal.
- [x] Default CPU/null gate stays green (224/224 graphics contract
      tests pass on `claude/intrinsicengine-agent-onboarding-p1J2P`);
      no `gpu`/`vulkan` test additions in this slice.

Full task (after Slice C, landed 2026-05-24):
- [x] Submitted debug primitives across all three lanes produce
      deterministic `BindPipeline + Draw(...)` records in the
      operational state (verified by
      `TransientDebugSurfacePassContract.RecordsAllThreeLanesWhenAllSubmitted`).
- [x] Per-packet `DepthTested` selects the matching pipeline variant
      for each lane (verified by the three
      `Selects*AlwaysOnTopVariantPerPacket` tests, one per lane).
- [x] No retained GPU resources on `GpuWorld`; no RHI/renderer
      surface change beyond the per-pass `Get*Pipeline()` accessors.
- [x] No regression in transient-debug rejection counters or
      `InvalidSnapshotRecordCount` semantics (full CPU/null gate
      green; 2209/2209 vs Slice B's 2201/2201 — the +8 delta is the
      new Slice C tests).
- [x] Default CPU/null gate stays green across slices (full
      CPU/null gate green; see `## Next verification step`).

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
- Slice A landed 2026-05-23 on
  `claude/intrinsicengine-agent-onboarding-p1J2P`. Verification: `cmake
  --preset ci`, `cmake --build --preset ci --target
  IntrinsicGraphicsContractTests IntrinsicGraphicsContractCpuTests`,
  `ctest --test-dir build/ci -L contract -LE
  'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — 224/224 graphics
  contract tests passed.
- Slice B landed 2026-05-24 on
  `claude/intrinsicengine-agent-onboarding-MnHl0`. Verification:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicGraphicsContractCpuTests`
  - `ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    — 227/227 graphics contract tests passed.
  - `cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke`
  - `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    — 2201/2201 tests passed.
  - `python3 tools/repo/check_layering.py --root src --strict`,
    `python3 tools/repo/check_test_layout.py --root . --strict`,
    `python3 tools/docs/check_doc_links.py --root .`,
    `python3 tools/agents/check_task_policy.py --root . --strict`
    — all clean.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
    — 442 modules.
- Slice C landed 2026-05-24 on
  `claude/intrinsicengine-agent-onboarding-WbeR9`. Verification:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicGraphicsContractCpuTests`
  - `ctest --test-dir build/ci -L contract -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    — 235/235 graphics contract tests passed.
  - `cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke`
  - `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
    — 2209/2209 tests passed.
  - `python3 tools/repo/check_layering.py --root src --strict`,
    `python3 tools/repo/check_test_layout.py --root . --strict`,
    `python3 tools/docs/check_doc_links.py --root .`,
    `python3 tools/agents/check_task_policy.py --root . --strict`
    — all clean.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
    — 442 modules (no new module surfaces; Slice C extends existing
    modules with new exported types).
- Slice D pick-up (Vulkan-capable host): configure with `ci-vulkan`,
  build `IntrinsicGraphicsIntegrationTests`, and run the opt-in
  `gpu;vulkan` smoke.
